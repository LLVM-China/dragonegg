//===-------------- Debug.cpp - Debug information gathering ---------------===//
//
// Copyright (C) 2005 to 2014  Jim Laskey, Duncan Sands et al.
//
// This file is part of DragonEgg.
//
// DragonEgg is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation; either version 2, or (at your option) any later version.
//
// DragonEgg is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// DragonEgg; see the file COPYING.  If not, write to the Free Software
// Foundation, 51 Franklin Street, Suite 500, Boston, MA 02110-1335, USA.
//
//===----------------------------------------------------------------------===//
// This file implements debug information gathering.
//===----------------------------------------------------------------------===//

// Plugin headers
#include "dragonegg/Debug.h"

// LLVM headers
#include "llvm/IR/Module.h"
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/IR/IntrinsicInst.h"
#endif

// System headers
#include <gmp.h>

// GCC headers
#include "auto-host.h"
#ifndef ENABLE_BUILD_WITH_CXX
#include <cstring> // Otherwise included by system.h with C linkage.
extern "C" {
#endif
#include "config.h"
// Stop GCC declaring 'getopt' as it can clash with the system's declaration.
#undef HAVE_DECL_GETOPT
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"

#include "flags.h"
#include "langhooks.h"
#include "toplev.h"
#include "version.h"
#ifndef ENABLE_BUILD_WITH_CXX
} // extern "C"
#endif

// Trees header.
#include "dragonegg/Trees.h"

using namespace llvm;
using namespace llvm::dwarf;

/// DirectoryAndFile - Extract the directory and file name from a path.  If no
/// directory is specified, then use the source working directory.
static void DirectoryAndFile(const std::string &FullPath,
                             std::string &Directory, std::string &FileName) {
  // Look for the directory slash.
  size_t Slash = FullPath.rfind('/');

  // If no slash
  if (Slash == std::string::npos) {
    // The entire path is the file name.
    Directory = "";
    FileName = FullPath;
  } else {
    // Separate the directory from the file name.
    Directory = FullPath.substr(0, Slash);
    FileName = FullPath.substr(Slash + 1);
  }

  // If no directory present then use source working directory.
  if (Directory.empty() || Directory[0] != '/') {
    Directory = std::string(get_src_pwd()) + "/" + Directory;
  }
}

/// NodeSizeInBits - Returns the size in bits stored in a tree node regardless
/// of whether the node is a TYPE or DECL.
static uint64_t NodeSizeInBits(tree Node) {
  if (isa<ERROR_MARK>(Node)) {
    return BITS_PER_WORD;
  } else if (isa<TYPE>(Node)) {
    if (TYPE_SIZE(Node) == NULL_TREE)
      return 0;
    else if (isInt64(TYPE_SIZE(Node), true))
      return getInt64(TYPE_SIZE(Node), true);
    else
      return TYPE_ALIGN(Node);
  } else if (DECL_P(Node)) {
    if (DECL_SIZE(Node) == NULL_TREE)
      return 0;
    else if (isInt64(DECL_SIZE(Node), 1))
      return getInt64(DECL_SIZE(Node), 1);
    else
      return DECL_ALIGN(Node);
  }

  return 0;
}

/// NodeAlignInBits - Returns the alignment in bits stored in a tree node
/// regardless of whether the node is a TYPE or DECL.
static uint64_t NodeAlignInBits(tree Node) {
  if (isa<ERROR_MARK>(Node))
    return BITS_PER_WORD;
  if (isa<TYPE>(Node))
    return TYPE_ALIGN(Node);
  if (DECL_P(Node))
    return DECL_ALIGN(Node);
  return BITS_PER_WORD;
}

/// FieldType - Returns the type node of a structure member field.
///
static tree FieldType(tree Field) {
  if (isa<ERROR_MARK>(Field))
    return integer_type_node;
  return DECL_BIT_FIELD_TYPE(Field) ? DECL_BIT_FIELD_TYPE(Field)
                                    : TREE_TYPE(Field);
}

/// GetNodeName - Returns the name stored in a node regardless of whether the
/// node is a TYPE or DECL.
static StringRef GetNodeName(tree Node) {
  tree Name = NULL;

  if (DECL_P(Node)) {
    Name = DECL_NAME(Node);
  } else if (isa<TYPE>(Node)) {
    Name = TYPE_NAME(Node);
  }

  if (Name) {
    if (isa<IDENTIFIER_NODE>(Name)) {
      return IDENTIFIER_POINTER(Name);
    } else if (isa<TYPE_DECL>(Name) && DECL_NAME(Name) &&
               !DECL_IGNORED_P(Name)) {
      return StringRef(IDENTIFIER_POINTER(DECL_NAME(Name)));
    }
  }

  return StringRef();
}

/// GetNodeLocation - Returns the location stored in a node  regardless of
/// whether the node is a TYPE or DECL.  UseStub is true if we should consider
/// the type stub as the actually location (ignored in struct/unions/enums.)
static expanded_location GetNodeLocation(tree Node, bool UseStub = true) {
  expanded_location Location = {};

  if (Node == NULL_TREE)
    return Location;

  tree Name = NULL;

  if (DECL_P(Node)) {
    Name = DECL_NAME(Node);
  } else if (isa<TYPE>(Node)) {
    Name = TYPE_NAME(Node);
  }

  if (Name) {
    if (isa<TYPE>(Name) && TYPE_STUB_DECL(Name)) {
      tree Stub = TYPE_STUB_DECL(Name);
      Location = expand_location(DECL_SOURCE_LOCATION(Stub));
    } else if (DECL_P(Name)) {
      Location = expand_location(DECL_SOURCE_LOCATION(Name));
    }
  }

  if (!Location.line) {
    if (UseStub && isa<TYPE>(Node) && TYPE_STUB_DECL(Node)) {
      tree Stub = TYPE_STUB_DECL(Node);
      Location = expand_location(DECL_SOURCE_LOCATION(Stub));
    } else if (DECL_P(Node)) {
      Location = expand_location(DECL_SOURCE_LOCATION(Node));
    }
  }

  return Location;
}

static StringRef getLinkageName(tree Node) {

  // Use llvm value name as linkage name if it is available.
  if (DECL_LLVM_SET_P(Node)) {
    Value *V = DECL_LLVM(Node);
    return V->getName();
  }

  tree decl_name = DECL_NAME(Node);
  if (decl_name != NULL && IDENTIFIER_POINTER(decl_name) != NULL) {
    if (TREE_PUBLIC(Node) && DECL_ASSEMBLER_NAME(Node) != DECL_NAME(Node) &&
#if (GCC_MAJOR > 4)
        !DECL_ABSTRACT_P(Node)) {
#else
        !DECL_ABSTRACT(Node)) {
#endif
      return StringRef(IDENTIFIER_POINTER(DECL_ASSEMBLER_NAME(Node)));
    }
  }
  return StringRef();
}

DebugInfo::DebugInfo(Module *m)
    : M(*m), VMContext(M.getContext()), Builder(M), DeclareFn(0),
    ValueFn(0), CurFullPath(""), PrevFullPath(""), CurLineNo(0), PrevLineNo(0),
    PrevBB(NULL) {}

/// getFunctionName - Get function name for the given FnDecl. If the
/// name is constructred on demand (e.g. C++ destructor) then the name
/// is stored on the side.
StringRef DebugInfo::getFunctionName(tree FnDecl) {
  StringRef FnNodeName = GetNodeName(FnDecl);
  // Use dwarf_name to construct function names. In C++ this is used to
  // create human readable destructor names.
  StringRef FnName = lang_hooks.dwarf_name(FnDecl, 0);
  if (FnNodeName.equals(FnName))
    return FnNodeName;

  // Use name returned by dwarf_name. It is in a temp. storage so make a
  // copy first.
  char *StrPtr = FunctionNames.Allocate<char>(FnName.size() + 1);
  strncpy(StrPtr, FnName.data(), FnName.size());
  StrPtr[FnName.size()] = 0;
  return StringRef(StrPtr);
}

/// EmitFunctionStart - Constructs the debug code for entering a function.
void DebugInfo::EmitFunctionStart(tree FnDecl, Function *Fn) {
  MigDIType FNType = getOrCreateType(TREE_TYPE(FnDecl));

  unsigned lineno = CurLineNo;

  std::map<tree_node *, MigTrackingMDRefType>::iterator I = SPCache.find(FnDecl);
  if (I != SPCache.end()) {
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
    DISubprogram *SPDecl = llvm::getDISubprogram(cast<MDNode>(I->second));
#else
    DISubprogram SPDecl(cast<MDNode>(I->second));
#endif
    MigDISubprogram SP = CreateSubprogramDefinition(SPDecl, lineno, Fn);
    SPDecl->replaceAllUsesWith(SP);

    // Push function on region stack.
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
    RegionStack.push_back(SP);
    RegionMap[FnDecl].reset(SP);
#else
    RegionStack.push_back(WeakVH(SP));
    RegionMap[FnDecl] = WeakVH(SP);
#endif
    return;
  }

  bool ArtificialFnWithAbstractOrigin = false;
  // If this artificial function has abstract origin then put this function
  // at module scope. The abstract copy will be placed in appropriate region.
  if (DECL_ARTIFICIAL(FnDecl) && DECL_ABSTRACT_ORIGIN(FnDecl) &&
      DECL_ABSTRACT_ORIGIN(FnDecl) != FnDecl)
    ArtificialFnWithAbstractOrigin = true;

  MigDIScope SPContext =
      ArtificialFnWithAbstractOrigin ?
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
      dyn_cast_or_null<DIScope>(getOrCreateFile(main_input_filename))
#else
      getOrCreateFile(main_input_filename)
#endif
      : findRegion(DECL_CONTEXT(FnDecl));

  // Creating context may have triggered creation of this SP descriptor. So
  // check the cache again.
  I = SPCache.find(FnDecl);
  if (I != SPCache.end()) {
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
    DISubprogram *SPDecl = llvm::getDISubprogram(cast<MDNode>(I->second));
#else
    DISubprogram SPDecl(cast<MDNode>(I->second));
#endif
    MigDISubprogram SP = CreateSubprogramDefinition(SPDecl, lineno, Fn);
    SPDecl->replaceAllUsesWith(SP);

    // Push function on region stack.
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
    RegionStack.push_back(SP);
    RegionMap[FnDecl].reset(SP);
#else
    RegionStack.push_back(WeakVH(SP));
    RegionMap[FnDecl] = WeakVH(SP);
#endif
    return;
  }

  // Gather location information.
  expanded_location Loc = GetNodeLocation(FnDecl, false);
  StringRef LinkageName = getLinkageName(FnDecl);

  unsigned Virtuality = 0;
  unsigned VIndex = 0;
  MigDIType ContainingType;
  if (DECL_VINDEX(FnDecl) && DECL_CONTEXT(FnDecl) &&
      isa<TYPE>((DECL_CONTEXT(FnDecl)))) { // Workaround GCC PR42653
#if (GCC_MAJOR > 4)
    if (tree_fits_uhwi_p(DECL_VINDEX(FnDecl)))
      VIndex = tree_to_shwi(DECL_VINDEX(FnDecl));
#else
    if (host_integerp(DECL_VINDEX(FnDecl), 0))
      VIndex = tree_low_cst(DECL_VINDEX(FnDecl), 0);
#endif
    Virtuality = dwarf::DW_VIRTUALITY_virtual;
    ContainingType = getOrCreateType(DECL_CONTEXT(FnDecl));
  }

  StringRef FnName = getFunctionName(FnDecl);

  MigDISubprogram SP = CreateSubprogram(
          SPContext, FnName, FnName, LinkageName, getOrCreateFile(Loc.file),
          lineno, FNType, Fn->hasInternalLinkage(), true /*definition*/,
          ContainingType, Virtuality, VIndex, DECL_ARTIFICIAL(FnDecl),
          optimize, Fn);

#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
  SPCache[FnDecl].reset(SP);

  // Push function on region stack.
  RegionStack.push_back(SP);
  RegionMap[FnDecl].reset(SP);
#else
  SPCache[FnDecl] = WeakVH(SP);
  RegionStack.push_back(WeakVH(SP));
  RegionMap[FnDecl] = WeakVH(SP);
#endif
}

/// getOrCreateNameSpace - Get name space descriptor for the tree node.
MigDINamespace DebugInfo::getOrCreateNameSpace(tree Node, MigDIScope Context) {
  std::map<tree_node *, MigTrackingMDRefType>::iterator I = NameSpaceCache.find(Node);
  if (I != NameSpaceCache.end())
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
    return dyn_cast_or_null<DINamespace>(cast<MDNode>(I->second));
#else
    return DINameSpace(cast<MDNode>(I->second));
#endif

  expanded_location Loc = GetNodeLocation(Node, false);
  MigDINamespace DNS = Builder.createNameSpace(
      Context, GetNodeName(Node)
#if LLVM_VERSION_CODE < LLVM_VERSION(5, 0)
      , getOrCreateFile(Loc.file),
      Loc.line
#endif
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 9)
      , true
#endif
      );

#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
  NameSpaceCache[Node].reset(DNS);
#else
  NameSpaceCache[Node] = WeakVH(DNS);
#endif
  return DNS;
}

/// findRegion - Find tree_node N's region.
MigDIScope DebugInfo::findRegion(tree Node) {
  if (Node == NULL_TREE)
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
    return dyn_cast_or_null<DIScope>(getOrCreateFile(main_input_filename));
#else
    return getOrCreateFile(main_input_filename);
#endif

  std::map<tree_node *, MigTrackingMDRefType>::iterator I = RegionMap.find(Node);
  if (I != RegionMap.end())
    if (MDNode *R = cast<MDNode>(&*I->second))
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
      return dyn_cast_or_null<DIScope>(R);
#else
      return DIDescriptor(R);
#endif

  if (isa<TYPE>(Node)) {
    MigDIType Ty = getOrCreateType(Node);
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
    return dyn_cast_or_null<DIScope>(Ty);
#else
    return DIDescriptor(Ty);
#endif
  } else if (DECL_P(Node)) {
    if (isa<NAMESPACE_DECL>(Node)) {
      MigDIScope NSContext = findRegion(DECL_CONTEXT(Node));
      MigDINamespace NS = getOrCreateNameSpace(Node, NSContext);
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
      return dyn_cast_or_null<DIScope>(NS);
#else
      return DIDescriptor(NS);
#endif
    }
    return findRegion(DECL_CONTEXT(Node));
  }

  // Otherwise main compile unit covers everything.
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
  return dyn_cast_or_null<DIScope>(getOrCreateFile(main_input_filename));
#else
  return getOrCreateFile(main_input_filename);
#endif
}

/// EmitFunctionEnd - Pop the region stack and reset current lexical block.
void DebugInfo::EmitFunctionEnd(bool EndFunction) {
  assert(!RegionStack.empty() && "Region stack mismatch, stack empty!");
  RegionStack.pop_back();
  // Blocks get erased; clearing these is needed for determinism, and also
  // a good idea if the next function gets inlined.
  if (EndFunction) {
    PrevBB = NULL;
    PrevLineNo = 0;
    PrevFullPath = NULL;
  }
}

/// EmitDeclare - Constructs the debug code for allocation of a new variable.
void DebugInfo::EmitDeclare(tree decl, unsigned Tag, StringRef Name, tree type,
                            Value *AI, LLVMBuilder &IRBuilder) {

  // Ignore compiler generated temporaries.
  if (DECL_IGNORED_P(decl))
    return;

  assert(!RegionStack.empty() && "Region stack mismatch, stack empty!");

  expanded_location Loc = GetNodeLocation(decl, false);

  // Construct variable.
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
  DIScope *VarScope = dyn_cast_or_null<DIScope>(cast<MDNode>(RegionStack.back()));
#else
  DIScope VarScope = DIScope(cast<MDNode>(RegionStack.back()));
#endif
  MigDIType Ty = getOrCreateType(type);
  if (Ty && DECL_ARTIFICIAL(decl))
    Ty = Builder.createArtificialType(Ty);
  // If type info is not available then do not emit debug info for this var.
  if (!Ty)
    return;

  // https://reviews.llvm.org/rL243764
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
  llvm::DILocalVariable *D = Builder.createAutoVariable(
      VarScope, Name, getOrCreateFile(Loc.file), Loc.line, Ty, optimize);
  DbgDeclareInst *DbgDecl = FindAllocaDbgDeclare(AI);
#else
  llvm::DIVariable D = Builder.createLocalVariable(
      Tag, VarScope, Name, getOrCreateFile(Loc.file), Loc.line, Ty, optimize);
#endif

  Instruction *Call = Builder.insertDeclare(AI, D,
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 3)
                                            Builder.createExpression(),
#endif
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
                                            DbgDecl->getDebugLoc(),
#endif
                                            IRBuilder.GetInsertBlock());

  Call->setDebugLoc(DebugLoc::get(Loc.line, 0, VarScope));
}

/// EmitStopPoint - Set current source location.
void DebugInfo::EmitStopPoint(BasicBlock *CurBB, LLVMBuilder &Builder) {
  // Don't bother if things are the same as last time.
  if (PrevLineNo == CurLineNo && PrevBB == CurBB &&
      (PrevFullPath == CurFullPath || !strcmp(PrevFullPath, CurFullPath)))
    return;
  if (!CurFullPath[0] || CurLineNo == 0)
    return;

  // Update last state.
  PrevFullPath = CurFullPath;
  PrevLineNo = CurLineNo;
  PrevBB = CurBB;

  if (RegionStack.empty())
    return;
  MDNode *Scope = cast<MDNode>(RegionStack.back());
  Builder.SetCurrentDebugLocation(DebugLoc::get(CurLineNo, 0 /*col*/, Scope));
}

/// EmitGlobalVariable - Emit information about a global variable.
///
void DebugInfo::EmitGlobalVariable(GlobalVariable *GV, tree decl) {
  if (DECL_ARTIFICIAL(decl) || DECL_IGNORED_P(decl))
    return;
  // Gather location information.
  expanded_location Loc = expand_location(DECL_SOURCE_LOCATION(decl));
  MigDIType TyD = getOrCreateType(TREE_TYPE(decl));
  StringRef DispName = GV->getName();
  if (DispName.empty())
    DispName = "__unknown__";
  if (DECL_NAME(decl)) {
    if (IDENTIFIER_POINTER(DECL_NAME(decl)))
      DispName = IDENTIFIER_POINTER(DECL_NAME(decl));
  }
  StringRef LinkageName;
  // The gdb does not expect linkage names for function local statics.
  if (DECL_CONTEXT(decl))
    if (!isa<FUNCTION_DECL>(DECL_CONTEXT(decl)))
      LinkageName = GV->getName();
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 9)
  Builder.createGlobalVariableExpression(
#elif LLVM_VERSION_CODE > LLVM_VERSION(3, 3)
  Builder.createGlobalVariable(
#else
  Builder.createStaticVariable(
#endif
      findRegion(DECL_CONTEXT(decl)), DispName, LinkageName,
      getOrCreateFile(Loc.file), Loc.line, TyD, GV->hasInternalLinkage(),
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 9)
      nullptr
#else
      GV
#endif
  );
}

/// createBasicType - Create BasicType.
MigDIType DebugInfo::createBasicType(tree type) {

  StringRef TypeName = GetNodeName(type);
  if (TypeName.empty())
    TypeName = "__unknown__";
  uint64_t Size = NodeSizeInBits(type);
  uint64_t Align = NodeAlignInBits(type);

  unsigned Encoding = 0;

  switch (TREE_CODE(type)) {
  case INTEGER_TYPE:
    if (TYPE_STRING_FLAG(type)) {
      if (TYPE_UNSIGNED(type))
        Encoding = DW_ATE_unsigned_char;
      else
        Encoding = DW_ATE_signed_char;
    } else if (TYPE_UNSIGNED(type))
      Encoding = DW_ATE_unsigned;
    else
      Encoding = DW_ATE_signed;
    break;
  case REAL_TYPE:
    Encoding = DW_ATE_float;
    break;
  case COMPLEX_TYPE:
    Encoding =
        isa<REAL_TYPE>(TREE_TYPE(type)) ? DW_ATE_complex_float : DW_ATE_lo_user;
    break;
  case BOOLEAN_TYPE:
    Encoding = DW_ATE_boolean;
    break;
  default:
    llvm_unreachable("Basic type case missing");
  }

  return Builder.createBasicType(TypeName, Size,
#if LLVM_VERSION_CODE < LLVM_VERSION(4, 0)
                                 Align,
#endif
                                 Encoding);
}

/// isArtificialArgumentType - Return true if arg_type represents artificial,
/// i.e. "this" in c++, argument.
static bool isArtificialArgumentType(tree arg_type, tree method_type) {
  if (!isa<METHOD_TYPE>(method_type))
    return false;
  if (!isa<POINTER_TYPE>(arg_type))
    return false;
  if (TREE_TYPE(arg_type) == TYPE_METHOD_BASETYPE(method_type))
    return true;
  if (main_type(arg_type) && main_type(arg_type) != TREE_TYPE(arg_type) &&
      (main_type(arg_type) == TYPE_METHOD_BASETYPE(method_type)))
    return true;
  return false;
}

/// createMethodType - Create MethodType.
MigDIType DebugInfo::createMethodType(tree type) {

  // Create a  place holder type first. The may be used as a context
  // for the argument types.
  // https://reviews.llvm.org/rL228852
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
  DICompositeType *FwdType = Builder.createReplaceableCompositeType(
#else
  DIType FwdType =
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 3)
     Builder.createReplaceableForwardDecl(
#else
     Builder.createForwardDecl(
#endif
#endif
    llvm::dwarf::DW_TAG_subroutine_type, StringRef(),
    findRegion(TYPE_CONTEXT(type)), getOrCreateFile(main_input_filename),
    0, 0, 0, 0);
  llvm::MDNode *FTN = FwdType;
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
  llvm::TempMDNode FwdTypeNode(FTN);
#else
  llvm::TrackingVH<llvm::MDNode> FwdTypeNode = FTN;
#endif
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
  TypeCache[type].reset(FwdType);
  RegionStack.push_back(FwdType);
  RegionMap[type].reset(FwdType);
#else
  TypeCache[type] = WeakVH(FwdType);
  // Push the struct on region stack.
  RegionStack.push_back(WeakVH(FwdType));
  RegionMap[type] = WeakVH(FwdType);
#endif

#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
  llvm::SmallVector<Metadata*, 16>
#else
  llvm::SmallVector<Value*, 16>
#endif
      EltTys;

  // Add the result type at least.
  EltTys.push_back(getOrCreateType(TREE_TYPE(type)));

  // Set up remainder of arguments.
  bool ProcessedFirstArg = false;
  for (tree arg = TYPE_ARG_TYPES(type); arg; arg = TREE_CHAIN(arg)) {
    tree formal_type = TREE_VALUE(arg);
    if (formal_type == void_type_node)
      break;
    MigDIType FormalType = getOrCreateType(formal_type);
    if (!ProcessedFirstArg && isArtificialArgumentType(formal_type, type)) {
      MigDIType AFormalType = Builder.createArtificialType(FormalType);
      EltTys.push_back(AFormalType);
    } else
      EltTys.push_back(FormalType);
    if (!ProcessedFirstArg)
      ProcessedFirstArg = true;
  }

#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
  llvm::DITypeRefArray EltTypeArray = Builder.getOrCreateTypeArray(makeArrayRef(EltTys));
#elif LLVM_VERSION_CODE > LLVM_VERSION(3, 3)
  llvm::DITypeArray EltTypeArray = Builder.getOrCreateTypeArray(EltTys);
#else
  llvm::DIArray EltTypeArray = Builder.getOrCreateArray(EltTys);
#endif

  RegionStack.pop_back();
  std::map<tree_node *, MigTrackingMDRefType>::iterator RI = RegionMap.find(type);
  if (RI != RegionMap.end())
    RegionMap.erase(RI);

  MigDIType RealType =
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
      Builder.createSubroutineType(EltTypeArray);
#else
      Builder.createSubroutineType(
           getOrCreateFile(main_input_filename),
           EltTypeArray);
#endif

  // Now that we have a real decl for the struct, replace anything using the
  // old decl with the new one.  This will recursively update the debug info.
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
  FwdTypeNode->replaceAllUsesWith(RealType);
#else
  llvm::DIType(FwdTypeNode).replaceAllUsesWith(RealType);
#endif

  return RealType;
}

/// createPointerType - Create PointerType.
MigDIType DebugInfo::createPointerType(tree type) {

  MigDIType FromTy = getOrCreateType(TREE_TYPE(type));
  // type* and type&
  // FIXME: Should BLOCK_POINTER_TYP have its own DW_TAG?
  unsigned Tag =
      isa<REFERENCE_TYPE>(type) ? DW_TAG_reference_type : DW_TAG_pointer_type;
  unsigned Flags = 0;

  // Check if this pointer type has a name.
  if (tree TyName = TYPE_NAME(type))
    if (isa<TYPE_DECL>(TyName) && !DECL_ORIGINAL_TYPE(TyName)) {
      expanded_location TypeNameLoc = GetNodeLocation(TyName);
      MigDIType Ty = CreateDerivedType(
          Tag, findRegion(DECL_CONTEXT(TyName)), GetNodeName(TyName),
          getOrCreateFile(TypeNameLoc.file), TypeNameLoc.line, 0 /*size*/,
          0 /*align*/, 0 /*offset */, 0 /*flags*/, FromTy);
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
      TypeCache[TyName].reset(Ty);
#else
      TypeCache[TyName] = WeakVH(Ty);
#endif
      return Ty;
    }

  StringRef PName =
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
      FromTy->getName();
#else
      FromTy.getName();
#endif
  MigDIType PTy = CreateDerivedType(
      Tag, findRegion(TYPE_CONTEXT(type)),
      Tag == DW_TAG_pointer_type ? StringRef() : PName,
      getOrCreateFile(main_input_filename), 0 /*line no*/, NodeSizeInBits(type),
      NodeAlignInBits(type), 0 /*offset */, Flags, FromTy);
  return PTy;
}

/// createArrayType - Create ArrayType.
MigDIType DebugInfo::createArrayType(tree type) {
  // Add the dimensions of the array.  FIXME: This loses CV qualifiers from
  // interior arrays, do we care?  Why aren't nested arrays represented the
  // obvious/recursive way?
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
  llvm::SmallVector<Metadata*, 8>
#else
  llvm::SmallVector<Value*, 8>
#endif
      Subscripts;

  // There will be ARRAY_TYPE nodes for each rank.  Followed by the derived
  // type.
  tree EltTy = TREE_TYPE(type);
  if (isa<ARRAY_TYPE>(type)) {
    tree atype = type;
    for (; isa<ARRAY_TYPE>(atype); atype = TREE_TYPE(atype)) {
      tree Domain = TYPE_DOMAIN(atype);
      if (Domain) {
        // FIXME - handle dynamic ranges
        tree MinValue = TYPE_MIN_VALUE(Domain);
        tree MaxValue = TYPE_MAX_VALUE(Domain);
        int64_t Low = 0;
        int64_t Hi = 0;
        if (isInt64(MinValue, false))
          Low = getInt64(MinValue, false);
        if (isInt64(MaxValue, false))
          Hi = getInt64(MaxValue, false);
        Subscripts.push_back(Builder.getOrCreateSubrange(Low, Hi));
      }
      EltTy = TREE_TYPE(atype);
    }
  } else {
    assert(isa<VECTOR_TYPE>(type) && "Not an array or vector type!");
    unsigned Length = TYPE_VECTOR_SUBPARTS(type);
    Subscripts.push_back(Builder.getOrCreateSubrange(0, Length));
  }

  MigDINodeArray SubscriptArray = Builder.getOrCreateArray(
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
          makeArrayRef
#endif
          (Subscripts));
  expanded_location Loc = GetNodeLocation(type);
  return CreateCompositeType(
      llvm::dwarf::DW_TAG_array_type, findRegion(TYPE_CONTEXT(type)),
      StringRef(), getOrCreateFile(Loc.file), 0, NodeSizeInBits(type),
      NodeAlignInBits(type), 0, 0, getOrCreateType(EltTy), SubscriptArray);
}

/// createEnumType - Create EnumType.
MigDIType DebugInfo::createEnumType(tree type) {
  // enum { a, b, ..., z };
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
  llvm::SmallVector<Metadata*, 32>
#else
  llvm::SmallVector<Value*, 32>
#endif
      Elements;

  if (TYPE_SIZE(type)) {
    for (tree Link = TYPE_VALUES(type); Link; Link = TREE_CHAIN(Link)) {
      tree EnumValue = TREE_VALUE(Link);
      if (isa<CONST_DECL>(EnumValue))
        EnumValue = DECL_INITIAL(EnumValue);
      uint64_t Value = getAPIntValue(EnumValue, 64).getZExtValue();
      const char *EnumName = IDENTIFIER_POINTER(TREE_PURPOSE(Link));
      Elements.push_back(Builder.createEnumerator(EnumName, Value));
    }
  }

  MigDINodeArray EltArray = Builder.getOrCreateArray(
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
          makeArrayRef
#endif
          (Elements));

  expanded_location Loc = {};

  if (TYPE_SIZE(type))
    // Incomplete enums do not  have any location info.
    Loc = GetNodeLocation(TREE_CHAIN(type), false);

  return CreateCompositeType(
      llvm::dwarf::DW_TAG_enumeration_type, findRegion(TYPE_CONTEXT(type)),
      GetNodeName(type), getOrCreateFile(Loc.file), Loc.line,
      NodeSizeInBits(type), NodeAlignInBits(type), 0, 0,
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
      0,
#else
      llvm::DIType(), /* RunTimeLang? */
#endif
      EltArray);
}

/// createStructType - Create StructType for struct or union or class.
MigDIType DebugInfo::createStructType(tree type) {

  // struct { a; b; ... z; }; | union { a; b; ... z; };
  unsigned Tag =
      isa<RECORD_TYPE>(type) ? DW_TAG_structure_type : DW_TAG_union_type;

  unsigned RunTimeLang = 0;
  //TODO  if (TYPE_LANG_SPECIFIC (type)
  //TODO      && lang_hooks.types.is_runtime_specific_type (type))
  //TODO    {
  //TODO      unsigned CULang = TheCU.getLanguage();
  //TODO      switch (CULang) {
  //TODO      case DW_LANG_ObjC_plus_plus :
  //TODO        RunTimeLang = DW_LANG_ObjC_plus_plus;
  //TODO        break;
  //TODO      case DW_LANG_ObjC :
  //TODO        RunTimeLang = DW_LANG_ObjC;
  //TODO        break;
  //TODO      case DW_LANG_C_plus_plus :
  //TODO        RunTimeLang = DW_LANG_C_plus_plus;
  //TODO        break;
  //TODO      default:
  //TODO        break;
  //TODO      }
  //TODO    }

  // Records and classes and unions can all be recursive.  To handle them,
  // we first generate a debug descriptor for the struct as a forward
  // declaration. Then (if it is a definition) we go through and get debug
  // info for all of its members.  Finally, we create a descriptor for the
  // complete type (which may refer to the forward decl if the struct is
  // recursive) and replace all  uses of the forward declaration with the
  // final definition.
  expanded_location Loc = GetNodeLocation(TREE_CHAIN(type), false);
  unsigned SFlags = 0;
  MigDIScope TyContext = findRegion(TYPE_CONTEXT(type));

  // Check if this type is created while creating context information
  // descriptor.
  {
    std::map<tree_node *, MigTrackingMDRefType>::iterator I = TypeCache.find(type);
    if (I != TypeCache.end())
      if (MDNode *TN = cast<MDNode>(&*I->second))
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
        return dyn_cast_or_null<DIType>(TN);
#else
        return DIType(TN);
#endif
  }

  MigDIType FwdDecl =
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
      Builder.createReplaceableCompositeType(
#elif LLVM_VERSION_CODE > LLVM_VERSION(3, 3)
      Builder.createReplaceableForwardDecl(
#else
      Builder.createForwardDecl(
#endif
      Tag, GetNodeName(type), TyContext, getOrCreateFile(Loc.file), Loc.line,
      0, 0, 0);

  if (TYPE_SIZE(type) == 0)
    // forward declaration,
    return FwdDecl;

  // Insert into the TypeCache so that recursive uses will find it.
  llvm::MDNode *FDN = FwdDecl;
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
  llvm::TempMDNode FwdDeclNode(FDN);
  TypeCache[type].reset(FwdDecl);
  RegionStack.push_back(FwdDecl);
  RegionMap[type].reset(FwdDecl);
#else
  llvm::TrackingVH<llvm::MDNode> FwdDeclNode = FDN;
  TypeCache[type] = WeakVH(FwdDecl);

  // Push the struct on region stack.
  RegionStack.push_back(WeakVH(FwdDecl));
  RegionMap[type] = WeakVH(FwdDecl);
#endif

  // Convert all the elements.
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
  llvm::SmallVector<Metadata*, 16>
#else
  llvm::SmallVector<Value*, 16>
#endif
      EltTys;

  if (tree binfo = TYPE_BINFO(type)) {
    for (unsigned i = 0, e = BINFO_N_BASE_BINFOS(binfo); i != e; ++i) {
      tree BInfo = BINFO_BASE_BINFO(binfo, i);
      tree BInfoType = BINFO_TYPE(BInfo);
      MigDIType BaseClass = getOrCreateType(BInfoType);
      unsigned BFlags = 0;
      if (BINFO_VIRTUAL_P(BInfo))
        BFlags = llvm::DIType::FlagVirtual;
      if (BINFO_BASE_ACCESSES(binfo)) {
        tree access = BINFO_BASE_ACCESS(binfo, i);
        if (access == access_protected_node)
          BFlags |= llvm::DIType::FlagProtected;
        else if (access == access_private_node)
          BFlags |= llvm::DIType::FlagPrivate;
      }

      // Check for zero BINFO_OFFSET.
      // FIXME : Is this correct ?
      unsigned Offset =
          BINFO_OFFSET(BInfo) ? getInt64(BINFO_OFFSET(BInfo), true) * 8 : 0;

      if (BINFO_VIRTUAL_P(BInfo))
        Offset = 0 - getInt64(BINFO_VPTR_FIELD(BInfo), false);
      // FIXME : name, size, align etc...
      MigDIType DTy = CreateDerivedType(
          DW_TAG_inheritance, findRegion(type), StringRef(),
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
          nullptr,
#else
          llvm::DIFile(),
#endif
          0,
          0, 0, Offset, BFlags, BaseClass);
      EltTys.push_back(DTy);
    }
  }

  // Now add members of this class.
  for (tree Member = TYPE_FIELDS(type); Member; Member = TREE_CHAIN(Member)) {
    // Should we skip.
    if (DECL_P(Member) && DECL_IGNORED_P(Member))
      continue;

    // Get the location of the member.
    expanded_location MemLoc = GetNodeLocation(Member, false);

    if (!isa<FIELD_DECL>(Member))
      // otherwise is a static variable, whose debug info is emitted
      // when through EmitGlobalVariable().
      continue;

    if (!OffsetIsLLVMCompatible(Member))
      // FIXME: field with variable or humongous offset.
      // Skip it for now.
      continue;

    /* Ignore nameless fields.  */
    if (DECL_NAME(Member) == NULL_TREE &&
        !isa<RECORD_OR_UNION_TYPE>(TREE_TYPE(Member)))
      continue;

    // Field type is the declared type of the field.
    tree FieldNodeType = FieldType(Member);
    MigDIType MemberType = getOrCreateType(FieldNodeType);
    StringRef MemberName = GetNodeName(Member);
    unsigned MFlags = 0;
    if (TREE_PROTECTED(Member))
      MFlags = llvm::DIType::FlagProtected;
    else if (TREE_PRIVATE(Member))
      MFlags = llvm::DIType::FlagPrivate;

    MigDIType DTy = CreateDerivedType(
        DW_TAG_member, findRegion(DECL_CONTEXT(Member)), MemberName,
        getOrCreateFile(MemLoc.file), MemLoc.line, NodeSizeInBits(Member),
        NodeAlignInBits(FieldNodeType), int_bit_position(Member), MFlags,
        MemberType);
    EltTys.push_back(DTy);
  }

  for (tree Member =
#if (GCC_MAJOR > 7)
          TYPE_FIELDS(type);
#else
          TYPE_METHODS(type);
#endif
          Member; Member = TREE_CHAIN(Member)) {

    if (DECL_ABSTRACT_ORIGIN(Member))
      continue;
    // Ignore unused aritificial members.
    if (DECL_ARTIFICIAL(Member) && !TREE_USED(Member))
      continue;
    // In C++, TEMPLATE_DECLs are marked Ignored, and should be.
    if (DECL_P(Member) && DECL_IGNORED_P(Member))
      continue;

    std::map<tree_node *, MigTrackingMDRefType>::iterator I = SPCache.find(Member);
    if (I != SPCache.end())
      EltTys.push_back(
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
              dyn_cast_or_null<DISubprogram>(cast<MDNode>(I->second))
#else
              DISubprogram(cast<MDNode>(I->second))
#endif
              );
    else {
      // Get the location of the member.
      expanded_location MemLoc = GetNodeLocation(Member, false);
      StringRef MemberName = getFunctionName(Member);
      StringRef LinkageName = getLinkageName(Member);
      MigDIType SPTy = getOrCreateType(TREE_TYPE(Member));
      unsigned Virtuality = 0;
      unsigned VIndex = 0;
      MigDIType ContainingType;
      if (DECL_VINDEX(Member)) {
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
        if (tree_fits_uhwi_p(DECL_VINDEX(Member)))
          VIndex = tree_to_shwi(DECL_VINDEX(Member));
#else
        if (host_integerp(DECL_VINDEX(Member), 0))
          VIndex = tree_low_cst(DECL_VINDEX(Member), 0);
#endif
        Virtuality = dwarf::DW_VIRTUALITY_virtual;
        ContainingType = getOrCreateType(DECL_CONTEXT(Member));
      }
      MigDISubprogram SP = CreateSubprogram(
          findRegion(DECL_CONTEXT(Member)), MemberName, MemberName, LinkageName,
          getOrCreateFile(MemLoc.file), MemLoc.line, SPTy, false, false,
          ContainingType, Virtuality, VIndex, DECL_ARTIFICIAL(Member),
          optimize);
      EltTys.push_back(SP);
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
      SPCache[Member].reset(SP);
#else
      SPCache[Member] = WeakVH(SP);
#endif
    }
  }

  MigDINodeArray Elements = Builder.getOrCreateArray(EltTys);

  RegionStack.pop_back();
  std::map<tree_node *, MigTrackingMDRefType>::iterator RI = RegionMap.find(type);
  if (RI != RegionMap.end())
    RegionMap.erase(RI);

  MigDIType ContainingType;
  if (TYPE_VFIELD(type)) {
    tree vtype = DECL_FCONTEXT(TYPE_VFIELD(type));
    ContainingType = getOrCreateType(vtype);
  }
  MigDICompositeType RealDecl = CreateCompositeType(
      Tag, findRegion(TYPE_CONTEXT(type)), GetNodeName(type),
      getOrCreateFile(Loc.file), Loc.line, NodeSizeInBits(type),
      NodeAlignInBits(type), 0, SFlags,
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
      nullptr,
#else
      llvm::DIType(),
#endif
      Elements, RunTimeLang,
      ContainingType);
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
  RegionMap[type].reset(RealDecl);
#else
  RegionMap[type] = WeakVH(RealDecl);
#endif

  // Now that we have a real decl for the struct, replace anything using the
  // old decl with the new one.  This will recursively update the debug info
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
  FwdDeclNode->replaceAllUsesWith(RealDecl);
#else
  llvm::DIType(FwdDeclNode).replaceAllUsesWith(RealDecl);
#endif

  return RealDecl;
}

/// createVariantType - Create variant type or return MainTy.
MigDIType DebugInfo::createVariantType(tree type, MigDIType MainTy) {
  MigDIType Ty;
  if (tree TyDef = TYPE_NAME(type)) {
    std::map<tree_node *, MigTrackingMDRefType>::iterator I = TypeCache.find(TyDef);
    if (I != TypeCache.end())
      if (I->second)
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
        return dyn_cast_or_null<DIType>(cast<MDNode>(I->second));
#else
        return DIType(cast<MDNode>(I->second));
#endif
    if (isa<TYPE_DECL>(TyDef) && DECL_ORIGINAL_TYPE(TyDef)) {
      expanded_location TypeDefLoc = GetNodeLocation(TyDef);
      Ty = CreateDerivedType(
          DW_TAG_typedef, findRegion(DECL_CONTEXT(TyDef)), GetNodeName(TyDef),
          getOrCreateFile(TypeDefLoc.file), TypeDefLoc.line, 0 /*size*/,
          0 /*align*/, 0 /*offset */, 0 /*flags*/, MainTy);
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
      TypeCache[TyDef].reset(Ty);
#else
      TypeCache[TyDef] = WeakVH(Ty);
#endif
      return Ty;
    }
  }

  if (TYPE_VOLATILE(type)) {
    Ty = CreateDerivedType(
        DW_TAG_volatile_type, findRegion(TYPE_CONTEXT(type)), StringRef(),
        getOrCreateFile(main_input_filename), 0 /*line no*/,
        NodeSizeInBits(type), NodeAlignInBits(type), 0 /*offset */,
        0 /* flags */, MainTy);
    MainTy = Ty;
  }

  if (TYPE_READONLY(type))
    Ty = CreateDerivedType(
        DW_TAG_const_type, findRegion(TYPE_CONTEXT(type)), StringRef(),
        getOrCreateFile(main_input_filename), 0 /*line no*/,
        NodeSizeInBits(type), NodeAlignInBits(type), 0 /*offset */,
        0 /* flags */, MainTy);

  if (TYPE_VOLATILE(type) || TYPE_READONLY(type)) {
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
    TypeCache[type].reset(Ty);
#else
    TypeCache[type] = WeakVH(Ty);
#endif
    return Ty;
  }

  // If, for some reason, main type variant type is seen then use it.
  return MainTy;
}

/// getOrCreateType - Get the type from the cache or create a new type if
/// necessary.
MigDIType DebugInfo::getOrCreateType(tree type) {
  if (type == NULL_TREE || type == error_mark_node)
    llvm_unreachable("Not a type.");

  // Should only be void if a pointer/reference/return type.  Returning NULL
  // allows the caller to produce a non-derived type.
  if (isa<VOID_TYPE>(type))
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
    return nullptr;
#else
    return DIType();
#endif

  // Check to see if the compile unit already has created this type.
  std::map<tree_node *, MigTrackingMDRefType>::iterator I = TypeCache.find(type);
  if (I != TypeCache.end())
    if (I->second)
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
      return dyn_cast_or_null<DIType>(cast<MDNode>(I->second));
#else
      return DIType(cast<MDNode>(I->second));
#endif

  if (type != TYPE_MAIN_VARIANT(type) && TYPE_MAIN_VARIANT(type)) {
    MigDIType MainTy = getOrCreateType(TYPE_MAIN_VARIANT(type));
    MigDIType Ty = createVariantType(type, MainTy);
#if LLVM_VERSION_CODE < LLVM_VERSION(3, 9)
    if (Ty.isValid())
#endif
      return Ty;
  }

  // Work out details of type.
  MigDIType Ty;
  switch (TREE_CODE(type)) {
  case ERROR_MARK:
#if LLVM_VERSION_CODE < LLVM_VERSION(3, 4)
  case LANG_TYPE:
#endif
  case TRANSLATION_UNIT_DECL:
  default:
    llvm_unreachable("Unsupported type");

#if GCC_VERSION_CODE > GCC_VERSION(4, 5)
  case NULLPTR_TYPE:
#endif
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 3)
  case LANG_TYPE: {
    tree name = TYPE_NAME(type);
    if (TREE_CODE(name) == TYPE_DECL)
      name = DECL_NAME(name);
    return Builder.createUnspecifiedType(IDENTIFIER_POINTER(name));
  }
#endif

  case OFFSET_TYPE:
  case POINTER_TYPE:
  case REFERENCE_TYPE:
    // Do not cache pointer type. The pointer may point to forward declared
    // struct.
    return createPointerType(type);

  case FUNCTION_TYPE:
  case METHOD_TYPE:
    Ty = createMethodType(type);
    break;

  case VECTOR_TYPE:
  case ARRAY_TYPE:
    Ty = createArrayType(type);
    break;

  case ENUMERAL_TYPE:
    Ty = createEnumType(type);
    break;

  case RECORD_TYPE:
  case QUAL_UNION_TYPE:
  case UNION_TYPE:
    return createStructType(type);

  case INTEGER_TYPE:
  case REAL_TYPE:
  case COMPLEX_TYPE:
  case BOOLEAN_TYPE:
    Ty = createBasicType(type);
    break;
  }
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
  TypeCache[type].reset(Ty);
#else
  TypeCache[type] = WeakVH(Ty);
#endif
  return Ty;
}

/// Initialize - Initialize debug info by creating compile unit for
/// main_input_filename. This must be invoked after language dependent
/// initialization is done.
void DebugInfo::Initialize() {

  // Debug info metadata without a version or with an outdated version will be
  // dropped. Add a version here to avoid that.
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 3)
  M.addModuleFlag(llvm::Module::Error, "Debug Info Version",
                  llvm::DEBUG_METADATA_VERSION);
#endif
  // Each input file is encoded as a separate compile unit in LLVM
  // debugging information output. However, many target specific tool chains
  // prefer to encode only one compile unit in an object file. In this
  // situation, the LLVM code generator will include  debugging information
  // entities in the compile unit that is marked as main compile unit. The
  // code generator accepts maximum one main compile unit per module. If a
  // module does not contain any main compile unit then the code generator
  // will emit multiple compile units in the output object file.
  getOrCreateCompileUnit(main_input_filename, true);
}

/// getOrCreateCompileUnit - Get the compile unit from the cache or
/// create a new one if necessary.
void DebugInfo::getOrCreateCompileUnit(const char *FullPath, bool isMain) {
  if (!FullPath)
    FullPath = main_input_filename;
  if (!strcmp(FullPath, ""))
    FullPath = "<stdin>";

  // Get source file information.
  std::string Directory;
  std::string FileName;
  DirectoryAndFile(FullPath, Directory, FileName);

  // Set up Language number.
  unsigned LangTag;
  const std::string LanguageName(lang_hooks.name);
  if (LanguageName == "GNU C")
    LangTag = DW_LANG_C89;
  else if (LanguageName == "GNU C++")
    LangTag = DW_LANG_C_plus_plus;
  else if (LanguageName == "GNU Ada")
    LangTag = DW_LANG_Ada95;
  else if (LanguageName == "GNU F77")
    LangTag = DW_LANG_Fortran77;
  else if (LanguageName == "GNU Pascal")
    LangTag = DW_LANG_Pascal83;
  else if (LanguageName == "GNU Java")
    LangTag = DW_LANG_Java;
  else if (LanguageName == "GNU Objective-C")
    LangTag = DW_LANG_ObjC;
  else if (LanguageName == "GNU Objective-C++")
    LangTag = DW_LANG_ObjC_plus_plus;
  else
    LangTag = DW_LANG_C89;

  StringRef Flags;

  // flag_objc_abi represents Objective-C runtime version number. It is zero
  // for all other language.
  unsigned ObjcRunTimeVer = 0;
  //  if (flag_objc_abi != 0 && flag_objc_abi != -1)
  //    ObjcRunTimeVer = flag_objc_abi;
  Builder.createCompileUnit(LangTag,
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 9)
                            Builder.createFile(FileName, Directory),
#else
                            FileName, Directory,
#endif
                            version_string, optimize, Flags, ObjcRunTimeVer);
}

/// getOrCreateFile - Get DIFile descriptor.
MigDIFile DebugInfo::getOrCreateFile(const char *FullPath) {
  if (!FullPath)
    FullPath = main_input_filename;
  if (!strcmp(FullPath, ""))
    FullPath = "<stdin>";

  // Get source file information.
  std::string Directory;
  std::string FileName;
  DirectoryAndFile(FullPath, Directory, FileName);
  return Builder.createFile(FileName, Directory);
}

//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Primary Constructors
//===----------------------------------------------------------------------===//

/// CreateDerivedType - Create a derived type like const qualified type,
/// pointer, typedef, etc.
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
DIDerivedType *
#else
DIDerivedType
#endif
DebugInfo::CreateDerivedType(unsigned Tag, MigDIScope Context, StringRef Name,
    MigDIFile F, unsigned LineNumber, uint64_t SizeInBits, uint64_t AlignInBits,
    uint64_t OffsetInBits, unsigned Flags, MigDIType DerivedFrom) {
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 9)
  DINode::DIFlags MigFlags = DINode::FlagZero;
#else
  unsigned MigFlags = Flags;
#endif
  switch (Tag) {
  case dwarf::DW_TAG_typedef:
    return Builder.createTypedef(DerivedFrom, Name, F, LineNumber, Context);
  case dwarf::DW_TAG_pointer_type:
    return Builder.createPointerType(DerivedFrom, SizeInBits, AlignInBits,
#if LLVM_VERSION_CODE > LLVM_VERSION(4, 0)
                                     None,
#endif
                                     Name);
  case dwarf::DW_TAG_reference_type:
  case dwarf::DW_TAG_rvalue_reference_type:
    return Builder.createReferenceType(Tag, DerivedFrom);
  case dwarf::DW_TAG_const_type:
  case dwarf::DW_TAG_volatile_type:
  case dwarf::DW_TAG_restrict_type:
    return Builder.createQualifiedType(Tag, DerivedFrom);
  case dwarf::DW_TAG_member:
    return Builder.createMemberType(Context, Name, F, LineNumber, SizeInBits,
                                    AlignInBits, OffsetInBits,
                                    MigFlags,
                                    DerivedFrom);
  case dwarf::DW_TAG_inheritance:
    return Builder.createInheritance(
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
                                     dyn_cast_or_null<DIType>(Context),
#else
                                     DIType(Context),
#endif
                                     DerivedFrom, OffsetInBits,
                                     MigFlags
                                     );
  case dwarf::DW_TAG_friend:
  case dwarf::DW_TAG_ptr_to_member_type:
    break;
  }
  llvm_unreachable("Unimplemented derived type tag");
}

/// CreateCompositeType - Create a composite type like array, struct, etc.
MigDICompositeType DebugInfo::CreateCompositeType(
    unsigned Tag, MigDIScope Context, StringRef Name, MigDIFile F,
    unsigned LineNumber, uint64_t SizeInBits, uint64_t AlignInBits,
    uint64_t OffsetInBits, unsigned Flags, MigDIType DerivedFrom,
    MigDINodeArray Elements, unsigned RuntimeLang, MDNode *ContainingType) {
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 9)
  DINode::DIFlags MigFlags = DINode::FlagZero;
#else
  unsigned MigFlags = Flags;
#endif
  switch (Tag) {
  case dwarf::DW_TAG_array_type:
    return Builder.createArrayType(SizeInBits, AlignInBits, DerivedFrom,
                                   Elements);
  case dwarf::DW_TAG_structure_type:
    return Builder.createStructType(Context, Name, F, LineNumber, SizeInBits,
                                    AlignInBits,
                                    MigFlags,
                                    DerivedFrom, Elements,
                                    0,
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
                                    dyn_cast_or_null<DIType>(ContainingType)
#else
                                    DIType(ContainingType)
#endif
                                    );
  case dwarf::DW_TAG_union_type:
    return Builder.createUnionType(Context, Name, F, LineNumber, SizeInBits,
                                   AlignInBits,
                                   MigFlags,
                                   Elements, RuntimeLang);
  case dwarf::DW_TAG_enumeration_type:
    return Builder.createEnumerationType(Context, Name, F, LineNumber,
                                         SizeInBits, AlignInBits, Elements,
                                         DerivedFrom);
  case dwarf::DW_TAG_subroutine_type:
    llvm_unreachable("DW_TAG_subroutine is not handled here.");
  case dwarf::DW_TAG_class_type:
    break;
  }
  llvm_unreachable("Unimplemented composite type tag");
}

/// CreateSubprogram - Create a new descriptor for the specified subprogram.
/// See comments in DISubprogram for descriptions of these fields.  This
/// method does not unique the generated descriptors.
MigDISubprogram DebugInfo::CreateSubprogram(
    MigDIScope Context, StringRef Name, StringRef DisplayName,
    StringRef LinkageName, MigDIFile F, unsigned LineNo, MigDIType Ty,
    bool isLocalToUnit, bool isDefinition, MigDIType ContainingType,
    unsigned VK, unsigned VIndex, unsigned Flags, bool isOptimized,
    Function *Fn) {
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 9)
  DINode::DIFlags MigFlags = DINode::FlagZero;
#else
  unsigned MigFlags = Flags;
#endif
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
  DISubroutineType *CTy = dyn_cast_or_null<DISubroutineType>(Ty);
#else
  DICompositeType CTy = getDICompositeType(Ty);
  assert(CTy.Verify() && "Expected a composite type!");
#endif
  if (
#if LLVM_VERSION_CODE < LLVM_VERSION(3, 9)
      ContainingType.isValid() ||
#endif
      VK || VIndex)
    return Builder.createMethod(Context, Name, LinkageName, F, LineNo, CTy,
                                isLocalToUnit, isDefinition, VK, VIndex,
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
                                0, /* ThisAdjustment */
                                nullptr,
#else
                                DIType(),
#endif
                                MigFlags, isOptimized,
#if LLVM_VERSION_CODE < LLVM_VERSION(3, 9)
                                Fn,
#endif
                                NULL);
  return Builder.createFunction(Context, Name, LinkageName, F, LineNo, CTy,
                                isLocalToUnit, isDefinition, LineNo, MigFlags,
                                isOptimized,
#if LLVM_VERSION_CODE < LLVM_VERSION(3, 9)
                                Fn,
#endif
                                NULL, NULL);
}

/// CreateSubprogramDefinition - Create new subprogram descriptor for the
/// given declaration.
MigDISubprogram DebugInfo::CreateSubprogramDefinition(
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
    DISubprogram *SP, unsigned LineNo, Function *Fn) {
  if (SP->isDefinition())
    return SP;

  MigDIFile File = Builder.createFile(SP->getFilename(), SP->getDirectory());
  return Builder.createFunction(dyn_cast_or_null<DIScope>(SP->getScope()),
      SP->getName(), SP->getLinkageName(), File, LineNo, SP->getType(),
      SP->isLocalToUnit(), true, LineNo, SP->getFlags(), SP->isOptimized(),
      SP->getTemplateParams(), SP);
#else
    DISubprogram &SP, unsigned LineNo, Function *Fn) {
  if (SP.isDefinition())
    return DISubprogram(SP);

  MigDIFile File = Builder.createFile(SP.getFilename(), SP.getDirectory());
  return Builder.createFunction(
      SP.getContext(), SP.getName(), SP.getLinkageName(), File,
      SP.getLineNumber(), SP.getType(), SP.isLocalToUnit(), true, LineNo,
      SP.getFlags(), SP.isOptimized(), Fn, SP.getTemplateParams(), SP);
#endif
}

//===----------------------------------------------------------------------===//
// Routines for inserting code into a function
//===----------------------------------------------------------------------===//

/// InsertDeclare - Insert a new llvm.dbg.declare intrinsic call.
Instruction *DebugInfo::InsertDeclare(Value *Storage, DIVariable D,
                                      Instruction *InsertBefore) {
  assert(Storage && "no storage passed to dbg.declare");
  assert(D.Verify() && "empty DIVariable passed to dbg.declare");
  if (!DeclareFn)
    DeclareFn = Intrinsic::getDeclaration(&M, Intrinsic::dbg_declare);

  Value *Args[] = {
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
    MetadataAsValue::get(Storage->getContext(), ValueAsMetadata::get(Storage)),
    MetadataAsValue::get(Storage->getContext(), &D)
#else
    MDNode::get(Storage->getContext(), Storage), D
#endif
  };
  return CallInst::Create(DeclareFn, Args, "", InsertBefore);
}

/// InsertDeclare - Insert a new llvm.dbg.declare intrinsic call.
Instruction *DebugInfo::InsertDeclare(Value *Storage, DIVariable D,
                                      BasicBlock *InsertAtEnd) {
  assert(Storage && "no storage passed to dbg.declare");
  assert(D.Verify() && "invalid DIVariable passed to dbg.declare");
  if (!DeclareFn)
    DeclareFn = Intrinsic::getDeclaration(&M, Intrinsic::dbg_declare);

  Value *Args[] = {
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
     MetadataAsValue::get(Storage->getContext(), ValueAsMetadata::get(Storage)),
     MetadataAsValue::get(Storage->getContext(), &D)
#else
     MDNode::get(Storage->getContext(), Storage), D
#endif
  };

  // If this block already has a terminator then insert this intrinsic
  // before the terminator.
  if (TerminatorInst *T = InsertAtEnd->getTerminator())
    return CallInst::Create(DeclareFn, Args, "", T);
  else
    return CallInst::Create(DeclareFn, Args, "", InsertAtEnd);
}

/// InsertDbgValueIntrinsic - Insert a new llvm.dbg.value intrinsic call.
Instruction *DebugInfo::InsertDbgValueIntrinsic(
    Value *V, uint64_t Offset, DIVariable D, Instruction *InsertBefore) {
  assert(V && "no value passed to dbg.value");
  assert(D.Verify() && "invalid DIVariable passed to dbg.value");
  if (!ValueFn)
    ValueFn = Intrinsic::getDeclaration(&M, Intrinsic::dbg_value);

  Value *Args[] = {
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
      MetadataAsValue::get(V->getContext(), ValueAsMetadata::get(V)),
#else
      MDNode::get(V->getContext(), V),
#endif
      ConstantInt::get(Type::getInt64Ty(V->getContext()), Offset),
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
      MetadataAsValue::get(V->getContext(), &D)
#else
      D
#endif
  };
  return CallInst::Create(ValueFn, Args, "", InsertBefore);
}

/// InsertDbgValueIntrinsic - Insert a new llvm.dbg.value intrinsic call.
Instruction *DebugInfo::InsertDbgValueIntrinsic(
    Value *V, uint64_t Offset, DIVariable D, BasicBlock *InsertAtEnd) {
  assert(V && "no value passed to dbg.value");
  assert(D.Verify() && "invalid DIVariable passed to dbg.value");
  if (!ValueFn)
    ValueFn = Intrinsic::getDeclaration(&M, Intrinsic::dbg_value);

  Value *Args[] = {
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
      MetadataAsValue::get(V->getContext(), ValueAsMetadata::get(V)),
#else
      MDNode::get(V->getContext(), V),
#endif
      ConstantInt::get(Type::getInt64Ty(V->getContext()), Offset),
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
      MetadataAsValue::get(V->getContext(), &D)
#else
      D
#endif
  };
  return CallInst::Create(ValueFn, Args, "", InsertAtEnd);
}
