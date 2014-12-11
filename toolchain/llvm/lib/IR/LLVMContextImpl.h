//===-- LLVMContextImpl.h - The LLVMContextImpl opaque class ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file declares LLVMContextImpl, the opaque implementation 
//  of LLVMContext.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_IR_LLVMCONTEXTIMPL_H
#define LLVM_LIB_IR_LLVMCONTEXTIMPL_H

#include "AttributeImpl.h"
#include "ConstantsContext.h"
#include "LeaksContext.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/ValueHandle.h"
#include <vector>

namespace llvm {

class ConstantInt;
class ConstantFP;
class DiagnosticInfoOptimizationRemark;
class DiagnosticInfoOptimizationRemarkMissed;
class DiagnosticInfoOptimizationRemarkAnalysis;
class LLVMContext;
class Type;
class Value;

struct DenseMapAPIntKeyInfo {
  struct KeyTy {
    APInt val;
    Type* type;
    KeyTy(const APInt& V, Type* Ty) : val(V), type(Ty) {}
    bool operator==(const KeyTy& that) const {
      return type == that.type && this->val == that.val;
    }
    bool operator!=(const KeyTy& that) const {
      return !this->operator==(that);
    }
    friend hash_code hash_value(const KeyTy &Key) {
      return hash_combine(Key.type, Key.val);
    }
  };
  static inline KeyTy getEmptyKey() { return KeyTy(APInt(1,0), nullptr); }
  static inline KeyTy getTombstoneKey() { return KeyTy(APInt(1,1), nullptr); }
  static unsigned getHashValue(const KeyTy &Key) {
    return static_cast<unsigned>(hash_value(Key));
  }
  static bool isEqual(const KeyTy &LHS, const KeyTy &RHS) {
    return LHS == RHS;
  }
};

struct DenseMapAPFloatKeyInfo {
  struct KeyTy {
    APFloat val;
    KeyTy(const APFloat& V) : val(V){}
    bool operator==(const KeyTy& that) const {
      return this->val.bitwiseIsEqual(that.val);
    }
    bool operator!=(const KeyTy& that) const {
      return !this->operator==(that);
    }
    friend hash_code hash_value(const KeyTy &Key) {
      return hash_combine(Key.val);
    }
  };
  static inline KeyTy getEmptyKey() { 
    return KeyTy(APFloat(APFloat::Bogus,1));
  }
  static inline KeyTy getTombstoneKey() { 
    return KeyTy(APFloat(APFloat::Bogus,2)); 
  }
  static unsigned getHashValue(const KeyTy &Key) {
    return static_cast<unsigned>(hash_value(Key));
  }
  static bool isEqual(const KeyTy &LHS, const KeyTy &RHS) {
    return LHS == RHS;
  }
};

struct AnonStructTypeKeyInfo {
  struct KeyTy {
    ArrayRef<Type*> ETypes;
    bool isPacked;
    KeyTy(const ArrayRef<Type*>& E, bool P) :
      ETypes(E), isPacked(P) {}
    KeyTy(const StructType *ST)
        : ETypes(ST->elements()), isPacked(ST->isPacked()) {}
    bool operator==(const KeyTy& that) const {
      if (isPacked != that.isPacked)
        return false;
      if (ETypes != that.ETypes)
        return false;
      return true;
    }
    bool operator!=(const KeyTy& that) const {
      return !this->operator==(that);
    }
  };
  static inline StructType* getEmptyKey() {
    return DenseMapInfo<StructType*>::getEmptyKey();
  }
  static inline StructType* getTombstoneKey() {
    return DenseMapInfo<StructType*>::getTombstoneKey();
  }
  static unsigned getHashValue(const KeyTy& Key) {
    return hash_combine(hash_combine_range(Key.ETypes.begin(),
                                           Key.ETypes.end()),
                        Key.isPacked);
  }
  static unsigned getHashValue(const StructType *ST) {
    return getHashValue(KeyTy(ST));
  }
  static bool isEqual(const KeyTy& LHS, const StructType *RHS) {
    if (RHS == getEmptyKey() || RHS == getTombstoneKey())
      return false;
    return LHS == KeyTy(RHS);
  }
  static bool isEqual(const StructType *LHS, const StructType *RHS) {
    return LHS == RHS;
  }
};

struct FunctionTypeKeyInfo {
  struct KeyTy {
    const Type *ReturnType;
    ArrayRef<Type*> Params;
    bool isVarArg;
    KeyTy(const Type* R, const ArrayRef<Type*>& P, bool V) :
      ReturnType(R), Params(P), isVarArg(V) {}
    KeyTy(const FunctionType *FT)
        : ReturnType(FT->getReturnType()), Params(FT->params()),
          isVarArg(FT->isVarArg()) {}
    bool operator==(const KeyTy& that) const {
      if (ReturnType != that.ReturnType)
        return false;
      if (isVarArg != that.isVarArg)
        return false;
      if (Params != that.Params)
        return false;
      return true;
    }
    bool operator!=(const KeyTy& that) const {
      return !this->operator==(that);
    }
  };
  static inline FunctionType* getEmptyKey() {
    return DenseMapInfo<FunctionType*>::getEmptyKey();
  }
  static inline FunctionType* getTombstoneKey() {
    return DenseMapInfo<FunctionType*>::getTombstoneKey();
  }
  static unsigned getHashValue(const KeyTy& Key) {
    return hash_combine(Key.ReturnType,
                        hash_combine_range(Key.Params.begin(),
                                           Key.Params.end()),
                        Key.isVarArg);
  }
  static unsigned getHashValue(const FunctionType *FT) {
    return getHashValue(KeyTy(FT));
  }
  static bool isEqual(const KeyTy& LHS, const FunctionType *RHS) {
    if (RHS == getEmptyKey() || RHS == getTombstoneKey())
      return false;
    return LHS == KeyTy(RHS);
  }
  static bool isEqual(const FunctionType *LHS, const FunctionType *RHS) {
    return LHS == RHS;
  }
};

/// \brief DenseMapInfo for GenericMDNode.
///
/// Note that we don't need the is-function-local bit, since that's implicit in
/// the operands.
struct GenericMDNodeInfo {
  struct KeyTy {
    ArrayRef<Value *> Ops;
    unsigned Hash;

    KeyTy(ArrayRef<Value *> Ops)
        : Ops(Ops), Hash(hash_combine_range(Ops.begin(), Ops.end())) {}

    KeyTy(GenericMDNode *N, SmallVectorImpl<Value *> &Storage) {
      Storage.resize(N->getNumOperands());
      for (unsigned I = 0, E = N->getNumOperands(); I != E; ++I)
        Storage[I] = N->getOperand(I);
      Ops = Storage;
      Hash = hash_combine_range(Ops.begin(), Ops.end());
    }

    bool operator==(const GenericMDNode *RHS) const {
      if (RHS == getEmptyKey() || RHS == getTombstoneKey())
        return false;
      if (Hash != RHS->getHash() || Ops.size() != RHS->getNumOperands())
        return false;
      for (unsigned I = 0, E = Ops.size(); I != E; ++I)
        if (Ops[I] != RHS->getOperand(I))
          return false;
      return true;
    }
  };
  static inline GenericMDNode *getEmptyKey() {
    return DenseMapInfo<GenericMDNode *>::getEmptyKey();
  }
  static inline GenericMDNode *getTombstoneKey() {
    return DenseMapInfo<GenericMDNode *>::getTombstoneKey();
  }
  static unsigned getHashValue(const KeyTy &Key) { return Key.Hash; }
  static unsigned getHashValue(const GenericMDNode *U) {
    return U->getHash();
  }
  static bool isEqual(const KeyTy &LHS, const GenericMDNode *RHS) {
    return LHS == RHS;
  }
  static bool isEqual(const GenericMDNode *LHS, const GenericMDNode *RHS) {
    return LHS == RHS;
  }
};

/// DebugRecVH - This is a CallbackVH used to keep the Scope -> index maps
/// up to date as MDNodes mutate.  This class is implemented in DebugLoc.cpp.
class DebugRecVH : public CallbackVH {
  /// Ctx - This is the LLVM Context being referenced.
  LLVMContextImpl *Ctx;
  
  /// Idx - The index into either ScopeRecordIdx or ScopeInlinedAtRecords that
  /// this reference lives in.  If this is zero, then it represents a
  /// non-canonical entry that has no DenseMap value.  This can happen due to
  /// RAUW.
  int Idx;
public:
  DebugRecVH(MDNode *n, LLVMContextImpl *ctx, int idx)
    : CallbackVH(n), Ctx(ctx), Idx(idx) {}
  
  MDNode *get() const {
    return cast_or_null<MDNode>(getValPtr());
  }

  void deleted() override;
  void allUsesReplacedWith(Value *VNew) override;
};
  
class LLVMContextImpl {
public:
  /// OwnedModules - The set of modules instantiated in this context, and which
  /// will be automatically deleted if this context is deleted.
  SmallPtrSet<Module*, 4> OwnedModules;
  
  LLVMContext::InlineAsmDiagHandlerTy InlineAsmDiagHandler;
  void *InlineAsmDiagContext;

  LLVMContext::DiagnosticHandlerTy DiagnosticHandler;
  void *DiagnosticContext;
  bool RespectDiagnosticFilters;

  LLVMContext::YieldCallbackTy YieldCallback;
  void *YieldOpaqueHandle;

  typedef DenseMap<DenseMapAPIntKeyInfo::KeyTy, ConstantInt *,
                   DenseMapAPIntKeyInfo> IntMapTy;
  IntMapTy IntConstants;
  
  typedef DenseMap<DenseMapAPFloatKeyInfo::KeyTy, ConstantFP*, 
                         DenseMapAPFloatKeyInfo> FPMapTy;
  FPMapTy FPConstants;

  FoldingSet<AttributeImpl> AttrsSet;
  FoldingSet<AttributeSetImpl> AttrsLists;
  FoldingSet<AttributeSetNode> AttrsSetNodes;

  StringMap<MDString> MDStringCache;

  DenseSet<GenericMDNode *, GenericMDNodeInfo> MDNodeSet;

  // MDNodes may be uniqued or not uniqued.  When they're not uniqued, they
  // aren't in the MDNodeSet, but they're still shared between objects, so no
  // one object can destroy them.  This set allows us to at least destroy them
  // on Context destruction.
  SmallPtrSet<GenericMDNode *, 1> NonUniquedMDNodes;

  DenseMap<Type*, ConstantAggregateZero*> CAZConstants;

  typedef ConstantUniqueMap<ConstantArray> ArrayConstantsTy;
  ArrayConstantsTy ArrayConstants;
  
  typedef ConstantUniqueMap<ConstantStruct> StructConstantsTy;
  StructConstantsTy StructConstants;
  
  typedef ConstantUniqueMap<ConstantVector> VectorConstantsTy;
  VectorConstantsTy VectorConstants;
  
  DenseMap<PointerType*, ConstantPointerNull*> CPNConstants;

  DenseMap<Type*, UndefValue*> UVConstants;
  
  StringMap<ConstantDataSequential*> CDSConstants;

  DenseMap<std::pair<const Function *, const BasicBlock *>, BlockAddress *>
    BlockAddresses;
  ConstantUniqueMap<ConstantExpr> ExprConstants;

  ConstantUniqueMap<InlineAsm> InlineAsms;

  ConstantInt *TheTrueVal;
  ConstantInt *TheFalseVal;
  
  LeakDetectorImpl<Value> LLVMObjects;
  
  // Basic type instances.
  Type VoidTy, LabelTy, HalfTy, FloatTy, DoubleTy, MetadataTy;
  Type X86_FP80Ty, FP128Ty, PPC_FP128Ty, X86_MMXTy;
  IntegerType Int1Ty, Int8Ty, Int16Ty, Int32Ty, Int64Ty;

  
  /// TypeAllocator - All dynamically allocated types are allocated from this.
  /// They live forever until the context is torn down.
  BumpPtrAllocator TypeAllocator;
  
  DenseMap<unsigned, IntegerType*> IntegerTypes;
  
  typedef DenseMap<FunctionType*, bool, FunctionTypeKeyInfo> FunctionTypeMap;
  FunctionTypeMap FunctionTypes;
  typedef DenseMap<StructType*, bool, AnonStructTypeKeyInfo> StructTypeMap;
  StructTypeMap AnonStructTypes;
  StringMap<StructType*> NamedStructTypes;
  unsigned NamedStructTypesUniqueID;
    
  DenseMap<std::pair<Type *, uint64_t>, ArrayType*> ArrayTypes;
  DenseMap<std::pair<Type *, unsigned>, VectorType*> VectorTypes;
  DenseMap<Type*, PointerType*> PointerTypes;  // Pointers in AddrSpace = 0
  DenseMap<std::pair<Type*, unsigned>, PointerType*> ASPointerTypes;


  /// ValueHandles - This map keeps track of all of the value handles that are
  /// watching a Value*.  The Value::HasValueHandle bit is used to know
  /// whether or not a value has an entry in this map.
  typedef DenseMap<Value*, ValueHandleBase*> ValueHandlesTy;
  ValueHandlesTy ValueHandles;
  
  /// CustomMDKindNames - Map to hold the metadata string to ID mapping.
  StringMap<unsigned> CustomMDKindNames;
  
  typedef std::pair<unsigned, TrackingVH<MDNode> > MDPairTy;
  typedef SmallVector<MDPairTy, 2> MDMapTy;

  /// MetadataStore - Collection of per-instruction metadata used in this
  /// context.
  DenseMap<const Instruction *, MDMapTy> MetadataStore;
  
  /// ScopeRecordIdx - This is the index in ScopeRecords for an MDNode scope
  /// entry with no "inlined at" element.
  DenseMap<MDNode*, int> ScopeRecordIdx;
  
  /// ScopeRecords - These are the actual mdnodes (in a value handle) for an
  /// index.  The ValueHandle ensures that ScopeRecordIdx stays up to date if
  /// the MDNode is RAUW'd.
  std::vector<DebugRecVH> ScopeRecords;
  
  /// ScopeInlinedAtIdx - This is the index in ScopeInlinedAtRecords for an
  /// scope/inlined-at pair.
  DenseMap<std::pair<MDNode*, MDNode*>, int> ScopeInlinedAtIdx;
  
  /// ScopeInlinedAtRecords - These are the actual mdnodes (in value handles)
  /// for an index.  The ValueHandle ensures that ScopeINlinedAtIdx stays up
  /// to date.
  std::vector<std::pair<DebugRecVH, DebugRecVH> > ScopeInlinedAtRecords;

  /// DiscriminatorTable - This table maps file:line locations to an
  /// integer representing the next DWARF path discriminator to assign to
  /// instructions in different blocks at the same location.
  DenseMap<std::pair<const char *, unsigned>, unsigned> DiscriminatorTable;

  /// IntrinsicIDCache - Cache of intrinsic name (string) to numeric ID mappings
  /// requested in this context
  typedef DenseMap<const Function*, unsigned> IntrinsicIDCacheTy;
  IntrinsicIDCacheTy IntrinsicIDCache;

  /// \brief Mapping from a function to its prefix data, which is stored as the
  /// operand of an unparented ReturnInst so that the prefix data has a Use.
  typedef DenseMap<const Function *, ReturnInst *> PrefixDataMapTy;
  PrefixDataMapTy PrefixDataMap;

  /// \brief Mapping from a function to its prologue data, which is stored as
  /// the operand of an unparented ReturnInst so that the prologue data has a
  /// Use.
  typedef DenseMap<const Function *, ReturnInst *> PrologueDataMapTy;
  PrologueDataMapTy PrologueDataMap;

  int getOrAddScopeRecordIdxEntry(MDNode *N, int ExistingIdx);
  int getOrAddScopeInlinedAtIdxEntry(MDNode *Scope, MDNode *IA,int ExistingIdx);
  
  LLVMContextImpl(LLVMContext &C);
  ~LLVMContextImpl();
};

}

#endif
