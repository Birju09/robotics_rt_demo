#include "UBSanCountPass.h"

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/LazyValueInfo.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/IR/Analysis.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <cstddef>
#include <llvm-22/llvm/ADT/APInt.h>
#include <llvm-22/llvm/IR/Constant.h>
#include <llvm-22/llvm/Support/Casting.h>

using namespace llvm;

AnalysisKey UBSanCountAnalysis::Key;

namespace {
constexpr StringLiteral kUBSanPrefix = "__ubsan_handle_";

static const std::set<StringRef> kOverFlowInstrumentation{{
    "__ubsan_handle_sub_overflow",
    "__ubsan_handle_add_overflow",
}};

static bool isUBSanSite(const CallBase &CB) {
  const Function *Callee = CB.getCalledFunction();
  if (!Callee) {
    return false;
  }
  return Callee->getName().starts_with(kUBSanPrefix);
}

static bool IsSignedOverflowSite(const CallBase &CB) {
  const Function *Callee = CB.getCalledFunction();
  if (!Callee) {
    return false;
  }
  return kOverFlowInstrumentation.find(Callee->getName()) !=
         kOverFlowInstrumentation.end();
}

static bool rangesProveNoOverflow(WithOverflowInst *WO,
                                  const ConstantRange &Lhs,
                                  const ConstantRange &Rhs) {
  auto Res = ConstantRange::OverflowResult::MayOverflow;
  const bool Signed = WO->isSigned();
  switch (WO->getBinaryOp()) {
  case Instruction::Add:
    Res = Signed ? Lhs.signedAddMayOverflow(Rhs)
                 : Lhs.unsignedAddMayOverflow(Rhs);
    break;
  case Instruction::Sub:
    Res = Signed ? Lhs.signedSubMayOverflow(Rhs)
                 : Lhs.unsignedSubMayOverflow(Rhs);
    break;
  case Instruction::Mul:
    if (!Signed) {
      Res = Lhs.unsignedMulMayOverflow(Rhs);
    }
    break;
  default:
    break;
  }
  return Res == ConstantRange::OverflowResult::NeverOverflows;
}

// Given an overflow-checked arithmetic intrinsic (the IR that UBSan's
// signed-integer-overflow check expands to), use value-range facts from BOTH
// ScalarEvolution and LazyValueInfo to decide whether the operation can ever
// actually overflow. Prints the ranges it derived. Returns true when the
// overflow is provably impossible, i.e. the UBSan check is dead and removable.
static bool checkIsRemovable(WithOverflowInst *WO, ScalarEvolution &SE,
                             LazyValueInfo &LVI) {
  Value *LHS = WO->getLHS();
  Value *RHS = WO->getRHS();

  // Identify the operands. printAsOperand prints the SSA name (e.g. %count.0.i)
  // for named values, or the literal (e.g. i32 -1) for constants/unnamed ones.
  errs() << "    LHS = ";
  LHS->printAsOperand(errs(), false);
  errs() << "   RHS = ";
  RHS->printAsOperand(errs(), false);
  errs() << "\n";

  // (1) LazyValueInfo: range of each operand *at this instruction*. LVI is
  //     flow/branch-sensitive, so the context instruction (WO) matters.
  //     UndefAllowed=true keeps the result a sound over-approximation.
  ConstantRange LhsLVI = LVI.getConstantRange(LHS, WO, true);
  ConstantRange RhsLVI = LVI.getConstantRange(RHS, WO, true);

  // (2) ScalarEvolution: signed range derived from the symbolic recurrence.
  //     getSCEV is valid for any integer Value; getSignedRange bounds it.
  ConstantRange LhsSE = SE.getSignedRange(SE.getSCEV(LHS));
  ConstantRange RhsSE = SE.getSignedRange(SE.getSCEV(RHS));

  // (3) A fact proven by *either* analysis is valid, so intersecting the two
  //     ranges is sound and at least as precise as either one alone.
  ConstantRange LhsR = LhsLVI.intersectWith(LhsSE);
  ConstantRange RhsR = RhsLVI.intersectWith(RhsSE);

  errs() << "    LHS range: LVI=" << LhsLVI << " SCEV=" << LhsSE << " -> "
         << LhsR << "\n";
  errs() << "    RHS range: LVI=" << RhsLVI << " SCEV=" << RhsSE << " -> "
         << RhsR << "\n";

  // (4) Ask ConstantRange whether the operation can overflow given the ranges.
  return rangesProveNoOverflow(WO, LhsR, RhsR);
}

// Speculative rewrite (used ONLY on a throwaway clone): for each with.overflow
// intrinsic, find the conditional branch guarding the __ubsan_handle_* call and
// replace it with an unconditional jump to the non-handler ("continue")
// successor, deleting the now-dead handler block. Removing that branch heals
// the CFG so the loop's exiting block again dominates its latch, which lets
// SCEV compute the trip count (and hence bound the counter).
static void straightenOverflowBranches(Function &F) {
  SmallVector<WithOverflowInst *, 8> WOs;
  for (BasicBlock &BB : F)
    for (Instruction &I : BB)
      if (auto *WO = dyn_cast<WithOverflowInst>(&I))
        WOs.push_back(WO);

  for (WithOverflowInst *WO : WOs) {
    // The branch guarding the check is the terminator of the intrinsic's block.
    auto *Br = dyn_cast<BranchInst>(WO->getParent()->getTerminator());
    if (!Br || !Br->isConditional()) {
      continue;
    }

    // Of the two successors, the "handler" one contains a __ubsan_handle_*
    // call.
    BasicBlock *HandlerBB = nullptr, *ContBB = nullptr;
    for (BasicBlock *Succ : Br->successors()) {
      bool IsHandler = false;
      for (Instruction &I : *Succ) {
        if (auto *CB = dyn_cast<CallBase>(&I)) {
          if (isUBSanSite(*CB)) {
            IsHandler = true;
            break;
          }
        }
      }
      if (IsHandler) {
        HandlerBB = Succ;
      } else {
        ContBB = Succ;
      }
    }
    if (!HandlerBB || !ContBB) {
      continue;
    }

    // Rewire: jump unconditionally to the continue block.
    ReplaceInstWithInst(Br, BranchInst::Create(ContBB));

    // If the handler block is now unreachable, delete it (this also fixes any
    // PHI nodes in its successors that referenced it).
    if (pred_empty(HandlerBB)) {
      DeleteDeadBlock(HandlerBB);
    }
  }
}

// Clone-based analysis: prove a check removable WITHOUT mutating the real IR.
// We clone F, straighten the overflow branches on the clone, build SCEV on the
// clone, and read the operand ranges there. The original module is left intact,
// so this behaves as a pure analysis that happens to reason about the
// branchless form of each loop.
struct UBSanSignedOverflowAnalysis
    : public llvm::PassInfoMixin<UBSanSignedOverflowAnalysis> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    // (1) Collect the overflow checks in the ORIGINAL function.
    constexpr size_t kMaxSize = 32;
    SmallVector<WithOverflowInst *, kMaxSize> OrigChecks;
    if (F.isDeclaration()) {
      return PreservedAnalyses::none();
    }
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        if (auto *WO = dyn_cast<WithOverflowInst>(&I)) {
          OrigChecks.push_back(WO);
        }
      }
    }

    if (OrigChecks.empty()) {
      return PreservedAnalyses::all();
    }

    // (2) Clone F. VMap maps each original Value to its corresponding clone.
    ValueToValueMapTy VMap;
    Function *Clone = CloneFunction(&F, VMap);

    // (3) Apply the speculative rewrite to the CLONE only.
    straightenOverflowBranches(*Clone);

    DominatorTree DT(*Clone);
    LoopInfo LI(DT);
    AssumptionCache AC(*Clone);
    TargetLibraryInfo TLI = FAM.getResult<TargetLibraryAnalysis>(F);
    ScalarEvolution SE(*Clone, TLI, AC, DT, LI);

    // (4) For each original check, query SCEV on its clone twin via VMap.
    for (WithOverflowInst *Orig : OrigChecks) {
      Value *CV = VMap.lookup(Orig);
      auto *Twin = dyn_cast_or_null<WithOverflowInst>(CV);
      if (!Twin) {
        continue;
      }

      ConstantRange Lhs = SE.getSignedRange(SE.getSCEV(Twin->getLHS()));
      ConstantRange Rhs = SE.getSignedRange(SE.getSCEV(Twin->getRHS()));

      errs() << "[ubsan-clone] check in @" << demangle(F.getName()) << " ("
             << Instruction::getOpcodeName(Orig->getBinaryOp())
             << "): LHS=" << Lhs << " RHS=" << Rhs << "\n";
      if (rangesProveNoOverflow(Twin, Lhs, Rhs))
        errs() << "  => CAN BE REMOVED (proven on branchless clone)\n";
      else
        errs() << "  => keep (overflow not proven impossible)\n";
    }

    // (5) Discard the clone; the real IR is untouched.
    Clone->eraseFromParent();
    return PreservedAnalyses::all();
  }
};

constexpr size_t kMaxInstructions = 32;
using OverflowInstructions = SmallVector<WithOverflowInst *, kMaxInstructions>;

OverflowInstructions CollectOverflowInstructions(Function &F) {
  OverflowInstructions Instructions;

  for (auto &BB : F) {
    for (auto &I : BB) {
      if (auto WO = dyn_cast<WithOverflowInst>(&I)) {
        Instructions.push_back(WO);
      }
    }
  }
  return Instructions;
}

using WOInstructionSet = SmallPtrSet<WithOverflowInst *, kMaxInstructions>;

static ConstantRange boundOverflowOperand(Value *V, ScalarEvolution &SE,
                                          LoopInfo &LI, LazyValueInfo &LVI) {
  const SCEV *S = SE.getSCEV(V);
  auto *AR = dyn_cast<SCEVAddRecExpr>(S);

  // The enclosing loop (for guard application), if any.
  const Loop *L = nullptr;
  if (AR) {
    L = AR->getLoop();
  } else if (auto *I = dyn_cast<Instruction>(V)) {
    L = LI.getLoopFor(I->getParent());
  }

  ConstantRange R = SE.getSignedRange(S);
  if (L) {
    R = R.intersectWith(SE.getSignedRange(SE.applyLoopGuards(S, L)));
  }

  if (AR && AR->isAffine()) {
    const SCEV *BTC = SE.getBackedgeTakenCount(L);
    auto *StepC = dyn_cast<SCEVConstant>(AR->getStepRecurrence(SE));
    if (StepC && !isa<SCEVCouldNotCompute>(BTC)) {
      ConstantRange StartR = SE.getSignedRange(AR->getStart());
      if (StartR.isFullSet())
        if (auto *SU = dyn_cast<SCEVUnknown>(AR->getStart())) {
          BasicBlock *Header = L->getHeader();
          BasicBlock *Entry = nullptr;
          for (BasicBlock *P : predecessors(Header))
            if (!L->contains(P)) {
              Entry = P;
              break;
            }
          if (Entry)
            StartR = LVI.getConstantRangeOnEdge(SU->getValue(), Entry, Header);
        }

      ConstantRange EndR = SE.getSignedRange(AR->evaluateAtIteration(BTC, SE));

      if (!StartR.isFullSet() && !EndR.isFullSet()) {
        bool Decreasing = StepC->getAPInt().isNegative();
        bool Monotone = Decreasing
                            ? StartR.getSignedMin().sge(EndR.getSignedMax())
                            : StartR.getSignedMax().sle(EndR.getSignedMin());
        if (Monotone)
          R = R.intersectWith(StartR.unionWith(EndR));
      }
    }
  }
  return R;
}

WOInstructionSet
CollectRemovableFromClone(Function &Orig,
                          OverflowInstructions &OriginalChecks) {
  WOInstructionSet Removable;

  ValueToValueMapTy VMap;
  Function *Clone = CloneFunction(&Orig, VMap);

  straightenOverflowBranches(*Clone);
  DominatorTree DT(*Clone);
  LoopInfo LI(DT);
  AssumptionCache AC(*Clone);
  TargetLibraryInfoImpl TLII(Orig.getParent()->getTargetTriple());
  TargetLibraryInfo TLI(TLII);
  ScalarEvolution SE(*Clone, TLI, AC, DT, LI);
  LazyValueInfo LVI(&AC, &Clone->getDataLayout());
  for (WithOverflowInst *Orig : OriginalChecks) {
    auto *Twin = dyn_cast_or_null<WithOverflowInst>(VMap.lookup(Orig));
    if (!Twin) {
      continue;
    }
    ConstantRange Lhs = boundOverflowOperand(Twin->getLHS(), SE, LI, LVI);
    ConstantRange Rhs = boundOverflowOperand(Twin->getRHS(), SE, LI, LVI);

    if (rangesProveNoOverflow(Twin, Lhs, Rhs)) {
      Removable.insert(Orig);
    }
  }
  Clone->eraseFromParent();

  return Removable;
}

class UBSanSignedOverflowOptimize
    : public PassInfoMixin<UBSanSignedOverflowOptimize> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    if (F.isDeclaration()) {
      return PreservedAnalyses::all();
    }

    auto OriginalChecks = CollectOverflowInstructions(F);
    if (OriginalChecks.empty()) {
      return PreservedAnalyses::all();
    }

    auto Removable = CollectRemovableFromClone(F, OriginalChecks);

    if (Removable.empty()) {
      return PreservedAnalyses::all();
    }

    bool Changed = false;
    for (WithOverflowInst *WO : OriginalChecks) {
      if (!Removable.contains(WO)) {
        continue;
      }

      for (User *U : llvm::make_early_inc_range(WO->users())) {
        auto *EV = dyn_cast<ExtractValueInst>(U);
        if (EV && EV->getNumIndices() == 1 && EV->getIndices()[0] == 1) {
          EV->replaceAllUsesWith(ConstantInt::getFalse(F.getContext()));
          EV->eraseFromParent();
          Changed = true;
        }
      }
    }

    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }
};

} // namespace

UBSanCountResult UBSanCountAnalysis::run(Module &M, ModuleAnalysisManager &) {
  UBSanCountResult Res;

  for (Function &F : M) {
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        if (auto *CB = dyn_cast<CallBase>(&I)) {
          if (isUBSanSite(*CB)) {
            std::string Name = CB->getCalledFunction()->getName().str();
            ++Res.CountsByKind[Name];
          }
        }
      }
    }
  }

  return Res;
}

PreservedAnalyses UBSanCountPrinterPass::run(Module &M,
                                             ModuleAnalysisManager &FAM) {
  auto &Res = FAM.getResult<UBSanCountAnalysis>(M);

  if (Res.total() == 0)
    return PreservedAnalyses::all();

  OS << "[ubsan-count] " << demangle(M.getName()) << ": " << Res.total()
     << " site(s)\n";

  for (auto &[Kind, Count] : Res.CountsByKind)
    OS << "  " << Kind << ": " << Count << "\n";

  return PreservedAnalyses::all();
}

llvm::PassPluginLibraryInfo getUBSanCountPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "UBSanCount", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            // 1. Register the analysis so other passes can query it via FAM.
            PB.registerAnalysisRegistrationCallback(
                [](ModuleAnalysisManager &MAM) {
                  MAM.registerPass([&] { return UBSanCountAnalysis(); });
                });

            // 2. Register "print<ubsan-count>" as a pipeline name for opt.
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "print<ubsan-count>") {
                    MPM.addPass(UBSanCountPrinterPass(errs()));
                    return true;
                  }
                  return false;
                });

            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "ubsan-signed-overflow-optimize") {
                    FPM.addPass(UBSanSignedOverflowOptimize());
                    return true;
                  }
                  if (Name == "ubsan-signed-overflow-check") {
                    FPM.addPass(UBSanSignedOverflowAnalysis());
                    return true;
                  }
                  return false;
                });

            PB.registerOptimizerLastEPCallback([](ModulePassManager &MPM,
                                                  OptimizationLevel O,
                                                  ThinOrFullLTOPhase C) {
              MPM.addPass(UBSanCountPrinterPass(errs()));
            });
          }};
}

extern "C" LLVM_ATTRIBUTE_WEAK llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getUBSanCountPluginInfo();
}
