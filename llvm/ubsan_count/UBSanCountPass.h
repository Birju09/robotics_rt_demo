#pragma once

#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include <map>
#include <string>

struct UBSanCountResult {
  // Maps each __ubsan_handle_* callee name to its call-site count.
  std::map<std::string, unsigned> CountsByKind;

  // Sum of all entries in CountsByKind.
  unsigned total() const {
    unsigned n = 0;
    for (auto &[_, c] : CountsByKind)
      n += c;
    return n;
  }

  bool invalidate(llvm::Module &, const llvm::PreservedAnalyses &,
                  llvm::ModuleAnalysisManager::Invalidator &) {
    return false;
  }
};

// Function analysis pass: counts UBSan instrumentation call sites per
// function.
struct UBSanCountAnalysis : public llvm::AnalysisInfoMixin<UBSanCountAnalysis> {
  using Result = UBSanCountResult;

  Result run(llvm::Module &F, llvm::ModuleAnalysisManager &);

private:
  friend struct llvm::AnalysisInfoMixin<UBSanCountAnalysis>;
  static llvm::AnalysisKey Key;
};

// Optional printer pass: prints per-function UBSan site counts to stderr.
class UBSanCountPrinterPass
    : public llvm::PassInfoMixin<UBSanCountPrinterPass> {
public:
  explicit UBSanCountPrinterPass(llvm::raw_ostream &OS) : OS(OS) {}

  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &FAM);

private:
  llvm::raw_ostream &OS;
};
