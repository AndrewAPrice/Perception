//===-- AMDGPUTargetMachine.h - AMDGPU TargetMachine Interface --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// \brief The AMDGPU TargetMachine interface definition for hw codgen targets.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_R600_AMDGPUTARGETMACHINE_H
#define LLVM_LIB_TARGET_R600_AMDGPUTARGETMACHINE_H

#include "AMDGPUFrameLowering.h"
#include "AMDGPUInstrInfo.h"
#include "AMDGPUIntrinsicInfo.h"
#include "AMDGPUSubtarget.h"
#include "R600ISelLowering.h"
#include "llvm/IR/DataLayout.h"

namespace llvm {

class AMDGPUTargetMachine : public LLVMTargetMachine {
  TargetLoweringObjectFile *TLOF;
  AMDGPUSubtarget Subtarget;
  AMDGPUIntrinsicInfo IntrinsicInfo;

public:
  AMDGPUTargetMachine(const Target &T, StringRef TT, StringRef FS,
                      StringRef CPU, TargetOptions Options, Reloc::Model RM,
                      CodeModel::Model CM, CodeGenOpt::Level OL);
  ~AMDGPUTargetMachine();
  const AMDGPUSubtarget *getSubtargetImpl() const override {
    return &Subtarget;
  }
  const AMDGPUIntrinsicInfo *getIntrinsicInfo() const override {
    return &IntrinsicInfo;
  }
  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;

  /// \brief Register R600 analysis passes with a pass manager.
  void addAnalysisPasses(PassManagerBase &PM) override;
  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF;
  }
};

} // End namespace llvm

#endif
