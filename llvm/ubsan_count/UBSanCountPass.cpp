#include "UBSanCountPass.h"

#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

AnalysisKey UBSanCountAnalysis::Key;

namespace {
// UBSan runtime handler functions all start with this prefix.
static constexpr StringLiteral kUBSanPrefix = "__ubsan_handle_";

static bool isUBSanSite(const CallBase &CB) {
  const Function *Callee = CB.getCalledFunction();
  if (!Callee) {
    return false;
  }
  return Callee->getName().starts_with(kUBSanPrefix);
}

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
