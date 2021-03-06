//=== lib/CodeGen/GlobalISel/AArch64PreLegalizerCombiner.cpp --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass does combining of machine instructions at the generic MI level,
// before the legalizer.
//
//===----------------------------------------------------------------------===//

#include "AArch64TargetMachine.h"
#include "llvm/CodeGen/GlobalISel/Combiner.h"
#include "llvm/CodeGen/GlobalISel/CombinerHelper.h"
#include "llvm/CodeGen/GlobalISel/CombinerInfo.h"
#include "llvm/CodeGen/GlobalISel/GISelKnownBits.h"
#include "llvm/CodeGen/GlobalISel/MIPatternMatch.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "aarch64-prelegalizer-combiner"

using namespace llvm;
using namespace MIPatternMatch;

#define AARCH64PRELEGALIZERCOMBINERHELPER_GENCOMBINERHELPER_DEPS
#include "AArch64GenGICombiner.inc"
#undef AARCH64PRELEGALIZERCOMBINERHELPER_GENCOMBINERHELPER_DEPS

namespace {
#define AARCH64PRELEGALIZERCOMBINERHELPER_GENCOMBINERHELPER_H
#include "AArch64GenGICombiner.inc"
#undef AARCH64PRELEGALIZERCOMBINERHELPER_GENCOMBINERHELPER_H

class AArch64PreLegalizerCombinerInfo : public CombinerInfo {
  GISelKnownBits *KB;
  MachineDominatorTree *MDT;

public:
  AArch64GenPreLegalizerCombinerHelper Generated;

  AArch64PreLegalizerCombinerInfo(bool EnableOpt, bool OptSize, bool MinSize,
                                  GISelKnownBits *KB, MachineDominatorTree *MDT)
      : CombinerInfo(/*AllowIllegalOps*/ true, /*ShouldLegalizeIllegal*/ false,
                     /*LegalizerInfo*/ nullptr, EnableOpt, OptSize, MinSize),
        KB(KB), MDT(MDT) {
    if (!Generated.parseCommandLineOption())
      report_fatal_error("Invalid rule identifier");
  }

  virtual bool combine(GISelChangeObserver &Observer, MachineInstr &MI,
                       MachineIRBuilder &B) const override;
};

bool AArch64PreLegalizerCombinerInfo::combine(GISelChangeObserver &Observer,
                                              MachineInstr &MI,
                                              MachineIRBuilder &B) const {
  CombinerHelper Helper(Observer, B, KB, MDT);

  switch (MI.getOpcode()) {
  case TargetOpcode::G_CONCAT_VECTORS:
    return Helper.tryCombineConcatVectors(MI);
  case TargetOpcode::G_SHUFFLE_VECTOR:
    return Helper.tryCombineShuffleVector(MI);
  case TargetOpcode::G_LOAD:
  case TargetOpcode::G_SEXTLOAD:
  case TargetOpcode::G_ZEXTLOAD: {
    bool Changed = false;
    Changed |= Helper.tryCombineExtendingLoads(MI);
    Changed |= Helper.tryCombineIndexedLoadStore(MI);
    return Changed;
  }
  case TargetOpcode::G_STORE:
    return Helper.tryCombineIndexedLoadStore(MI);
  case TargetOpcode::G_INTRINSIC_W_SIDE_EFFECTS:
    switch (MI.getIntrinsicID()) {
    case Intrinsic::memcpy:
    case Intrinsic::memmove:
    case Intrinsic::memset: {
      // If we're at -O0 set a maxlen of 32 to inline, otherwise let the other
      // heuristics decide.
      unsigned MaxLen = EnableOpt ? 0 : 32;
      // Try to inline memcpy type calls if optimizations are enabled.
      return (!EnableMinSize) ? Helper.tryCombineMemCpyFamily(MI, MaxLen)
                              : false;
    }
    default:
      break;
    }
  }

  if (Generated.tryCombineAll(Observer, MI, B))
    return true;

  return false;
}

#define AARCH64PRELEGALIZERCOMBINERHELPER_GENCOMBINERHELPER_CPP
#include "AArch64GenGICombiner.inc"
#undef AARCH64PRELEGALIZERCOMBINERHELPER_GENCOMBINERHELPER_CPP

// Pass boilerplate
// ================

class AArch64PreLegalizerCombiner : public MachineFunctionPass {
public:
  static char ID;

  AArch64PreLegalizerCombiner(bool IsOptNone = false);

  StringRef getPassName() const override { return "AArch64PreLegalizerCombiner"; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;
private:
  bool IsOptNone;
};
} // end anonymous namespace

void AArch64PreLegalizerCombiner::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetPassConfig>();
  AU.setPreservesCFG();
  getSelectionDAGFallbackAnalysisUsage(AU);
  AU.addRequired<GISelKnownBitsAnalysis>();
  AU.addPreserved<GISelKnownBitsAnalysis>();
  if (!IsOptNone) {
    AU.addRequired<MachineDominatorTree>();
    AU.addPreserved<MachineDominatorTree>();
  }
  MachineFunctionPass::getAnalysisUsage(AU);
}

AArch64PreLegalizerCombiner::AArch64PreLegalizerCombiner(bool IsOptNone)
    : MachineFunctionPass(ID), IsOptNone(IsOptNone) {
  initializeAArch64PreLegalizerCombinerPass(*PassRegistry::getPassRegistry());
}

bool AArch64PreLegalizerCombiner::runOnMachineFunction(MachineFunction &MF) {
  if (MF.getProperties().hasProperty(
          MachineFunctionProperties::Property::FailedISel))
    return false;
  auto *TPC = &getAnalysis<TargetPassConfig>();
  const Function &F = MF.getFunction();
  bool EnableOpt =
      MF.getTarget().getOptLevel() != CodeGenOpt::None && !skipFunction(F);
  GISelKnownBits *KB = &getAnalysis<GISelKnownBitsAnalysis>().get(MF);
  MachineDominatorTree *MDT =
      IsOptNone ? nullptr : &getAnalysis<MachineDominatorTree>();
  AArch64PreLegalizerCombinerInfo PCInfo(EnableOpt, F.hasOptSize(),
                                         F.hasMinSize(), KB, MDT);
  Combiner C(PCInfo, TPC);
  return C.combineMachineInstrs(MF, /*CSEInfo*/ nullptr);
}

char AArch64PreLegalizerCombiner::ID = 0;
INITIALIZE_PASS_BEGIN(AArch64PreLegalizerCombiner, DEBUG_TYPE,
                      "Combine AArch64 machine instrs before legalization",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_DEPENDENCY(GISelKnownBitsAnalysis)
INITIALIZE_PASS_END(AArch64PreLegalizerCombiner, DEBUG_TYPE,
                    "Combine AArch64 machine instrs before legalization", false,
                    false)


namespace llvm {
FunctionPass *createAArch64PreLegalizeCombiner(bool IsOptNone) {
  return new AArch64PreLegalizerCombiner(IsOptNone);
}
} // end namespace llvm
