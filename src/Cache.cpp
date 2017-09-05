//==----------- Cache.h - Caching values "in" GCC trees ----------*- C++ -*-==//
//
// Copyright (C) 2009 to 2013  Duncan Sands.
//
// This file is part of DragonEgg.
//
// DragonEgg is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2, or (at your option) any later
// version.
//
// DragonEgg is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
// more details.
// You should have received a copy of the GNU General Public License along
// with DragonEgg; see the file COPYING.  If not, write to the Free Software
// Foundation, 51 Franklin Street, Suite 500, Boston, MA 02110-1335, USA.
//
//===----------------------------------------------------------------------===//
// This code lets you associate values with a tree, as if it were cached inside
// the tree: if the tree is garbage collected and reallocated, then the cached
// value will have been cleared.
//===----------------------------------------------------------------------===//

// Plugin headers.
#include "dragonegg/Internals.h"
#include "dragonegg/Cache.h"

// LLVM headers
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 3)
#include "llvm/IR/ValueHandle.h"
#else
#include "llvm/Support/ValueHandle.h"
#endif

// System headers
#include <cassert>
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
#if (GCC_MAJOR > 4)
#include "tree-core.h"
#endif

#include "ggc.h"
#ifndef ENABLE_BUILD_WITH_CXX
} // extern "C"
#endif

using namespace llvm;

// Hash table mapping trees to integers.

#if (GCC_MAJOR > 4)
struct GTY((for_user))
#else
struct GTY(())
#endif
    tree2int {
  struct tree_map_base base;
  int GTY((skip)) val;
};

#define tree2int_eq tree_map_base_eq
#define tree2int_hash tree_map_base_hash
#define tree2int_marked_p tree_map_base_marked_p

#if (GCC_MAJOR < 5)
// FIXME: gengtype does not support macro https://gcc.gnu.org/ml/gcc/2017-07/msg00061.html
static GTY((if_marked("tree2int_marked_p"), param_is(struct tree2int)))
    htab_t intCache;
#else
#if (GCC_MAJOR == 5)
struct intCacheHasher : ggc_cache_hasher<tree2int *> {
#else
struct intCacheHasher : ggc_cache_ptr_hash<tree2int> {
#endif
  static inline hashval_t hash(tree2int *t2i) {
    return tree_map_base_hash(&t2i->base);
  }

  static inline bool equal(tree2int *a, tree2int *b) {
    return a->base.from == b->base.from;
  }
};
static GTY((cache)) hash_table<intCacheHasher> *intCache;
#endif

// Hash table mapping trees to Type*.

// Forward declare Type for the benefit of gengtype.
#ifndef IN_GCC
struct Type;
#endif
#if (GCC_MAJOR > 4)
struct GTY((for_user))
#else
struct GTY(())
#endif
    tree2Type {
  struct tree_map_base base;
#ifndef IN_GCC
  struct
#endif
      Type *
          GTY((skip)) Ty;
};

#define tree2Type_eq tree_map_base_eq
#define tree2Type_hash tree_map_base_hash
#define tree2Type_marked_p tree_map_base_marked_p

#if (GCC_MAJOR < 5)
// FIXME: gengtype does not support macro https://gcc.gnu.org/ml/gcc/2017-07/msg00061.html
static GTY((if_marked("tree2Type_marked_p"), param_is(struct tree2Type)))
    htab_t TypeCache;
#else
#if (GCC_MAJOR == 5)
struct TypeCacheHaser : ggc_cache_hasher<tree2Type *> {
#else
struct TypeCacheHaser : ggc_cache_ptr_hash<tree2Type> {
#endif
  static inline hashval_t hash(tree2Type *t2T) {
    return tree_map_base_hash(&t2T->base);
  }

  static inline bool equal(tree2Type *a, tree2Type *b) {
    return a->base.from == b->base.from;
  }
};
static GTY((cache)) hash_table<TypeCacheHaser> *TypeCache;
#endif

// Hash table mapping trees to WeakVH.

// Forward declare WeakVH for the benefit of gengtype.
#ifndef IN_GCC
struct WeakVH;
#endif
#if (GCC_MAJOR > 4)
struct GTY((for_user))
#else
struct GTY(())
#endif
    tree2WeakVH {
  struct tree_map_base base;
#ifndef IN_GCC
  struct
#endif
      WeakVH
          GTY((skip)) V;
};

#define tree2WeakVH_eq tree_map_base_eq
#define tree2WeakVH_hash tree_map_base_hash
#define tree2WeakVH_marked_p tree_map_base_marked_p

#if (GCC_MAJOR < 5)
// FIXME: gengtype does not support macro https://gcc.gnu.org/ml/gcc/2017-07/msg00061.html
static GTY((if_marked("tree2WeakVH_marked_p"), param_is(struct tree2WeakVH)))
    htab_t WeakVHCache;
#else
#if (GCC_MAJOR == 5)
struct WeakVHCacheHasher : ggc_cache_hasher<tree2WeakVH *> {
#else
struct WeakVHCacheHasher : ggc_cache_ptr_hash<tree2WeakVH> {
#endif
  static inline hashval_t hash(tree2WeakVH *t2W) {
    return tree_map_base_hash(&t2W->base);
  }

  static inline bool equal(tree2WeakVH *a, tree2WeakVH *b) {
    return a->base.from == b->base.from;
  }

  static int keep_cache_entry(tree2WeakVH *&t2W) {
    return ggc_marked_p(t2W->base.from);
  }
};
static GTY((cache)) hash_table<WeakVHCacheHasher> *WeakVHCache;
#endif

// Include the garbage collector header.
#ifndef ENABLE_BUILD_WITH_CXX
extern "C" {
#endif
#if (GCC_MAJOR < 5)
#if GCC_VERSION_CODE > GCC_VERSION(4, 5)
#include "dragonegg/gt-cache-4.6.inc"
#else
#include "dragonegg/gt-cache-4.5.inc"
#endif
#else
#if (GCC_MAJOR == 6)
#include "dragonegg/gt-cache-6.4.inc"
#elif (GCC_MAJOR == 8)
#include "dragonegg/gt-cache-8.0.inc"
#endif
#endif
#ifndef ENABLE_BUILD_WITH_CXX
} // extern "C"
#endif

bool getCachedInteger(tree t, int &Val) {
  if (!intCache)
    return false;
#if (GCC_MAJOR < 5)
  tree_map_base in = { t };
  tree2int *h = (tree2int *)htab_find(intCache, &in);
#else
  tree2int in;
  in.base.from = t;
  tree2int *h = intCache->find(&in);
#endif
  if (!h)
    return false;
  Val = h->val;
  return true;
}

void setCachedInteger(tree t, int Val) {
  if (!intCache)
#if (GCC_MAJOR < 5)
    intCache = htab_create_ggc(1024, tree2int_hash, tree2int_eq, 0);
#else
    intCache = hash_table<intCacheHasher>::create_ggc(1024);
#endif

#if (GCC_MAJOR < 5)
  tree_map_base in = { t };
  tree2int **slot = (tree2int **)htab_find_slot(intCache, &in, INSERT);
#else
  tree2int in;
  in.base.from = t;
  tree2int **slot = intCache->find_slot(&in, INSERT);
#endif
  assert(slot && "Failed to create hash table slot!");

  if (!*slot) {
    *slot =
#if (GCC_MAJOR > 4)
        ggc_alloc<tree2int>();
#elif GCC_VERSION_CODE > GCC_VERSION(4, 5)
        ggc_alloc_tree2int();
#else
    GGC_NEW(struct tree2int);
#endif
    (*slot)->base.from = t;
  }

  (*slot)->val = Val;
}

Type *getCachedType(tree t) {
  if (!TypeCache)
    return 0;
#if (GCC_MAJOR < 5)
  tree_map_base in = { t };
  tree2Type *h = (tree2Type *)htab_find(TypeCache, &in);
#else
  tree2Type in;
  in.base.from = t;
  tree2Type *h = TypeCache->find(&in);
#endif
  return h ? h->Ty : 0;
}

void setCachedType(tree t, Type *Ty) {
#if (GCC_MAJOR < 5)
  tree_map_base in = { t };
#else
  tree2Type in;
  in.base.from = t;
#endif

  /* If deleting, remove the slot.  */
  if (!Ty) {
    if (TypeCache)
#if (GCC_MAJOR < 5)
      htab_remove_elt(TypeCache, &in);
#else
      TypeCache->remove_elt(&in);
#endif
    return;
  }

  if (!TypeCache)
#if (GCC_MAJOR < 5)
    TypeCache = htab_create_ggc(1024, tree2Type_hash, tree2Type_eq, 0);
#else
    TypeCache = hash_table<TypeCacheHaser>::create_ggc(1024);
#endif

#if (GCC_MAJOR < 5)
  tree2Type **slot = (tree2Type **)htab_find_slot(TypeCache, &in, INSERT);
#else
  tree2Type **slot = TypeCache->find_slot(&in, INSERT);
#endif
  assert(slot && "Failed to create hash table slot!");

  if (!*slot) {
    *slot =
#if (GCC_MAJOR > 4)
        ggc_alloc<tree2Type>();
#elif GCC_VERSION_CODE > GCC_VERSION(4, 5)
        ggc_alloc_tree2Type();
#else
    GGC_NEW(struct tree2Type);
#endif
    (*slot)->base.from = t;
  }

  (*slot)->Ty = Ty;
}

/// getCachedValue - Returns the value associated with the given GCC tree, or
/// null if none.
Value *getCachedValue(tree t) {
  if (!WeakVHCache)
    return 0;
#if (GCC_MAJOR < 5)
  tree_map_base in = { t };
  tree2WeakVH *h = (tree2WeakVH *)htab_find(WeakVHCache, &in);
#else
  tree2WeakVH in;
  in.base.from = t;
  tree2WeakVH *h = WeakVHCache->find(&in);
#endif
  return h ? h->V : 0;
}

static void DestructWeakVH(void *p) {
  ((WeakVH *)&((tree2WeakVH *)p)->V)->~WeakVH();
}

/// setCachedValue - Associates the given value (which may be null) with the
/// given GCC tree.  The association is removed if tree is garbage collected
/// or the value deleted.
void setCachedValue(tree t, Value *V) {
#if (GCC_MAJOR < 5)
  tree_map_base in = { t };
#else
  tree2WeakVH in;
  in.base.from = t;
#endif

  // If deleting, remove the slot.
  if (!V) {
    if (WeakVHCache)
#if (GCC_MAJOR < 5)
      htab_remove_elt(WeakVHCache, &in);
#else
      WeakVHCache->remove_elt(&in);
#endif
    return;
  }

  if (!WeakVHCache)
    WeakVHCache =
#if (GCC_MAJOR < 5)
        htab_create_ggc(1024, tree2WeakVH_hash, tree2WeakVH_eq, DestructWeakVH);
#else
        hash_table<WeakVHCacheHasher>::create_ggc(1024);
#endif

#if (GCC_MAJOR < 5)
  tree2WeakVH **slot = (tree2WeakVH **)htab_find_slot(WeakVHCache, &in, INSERT);
#else
  tree2WeakVH **slot = WeakVHCache->find_slot(&in, INSERT);
#endif
  assert(slot && "Failed to create hash table slot!");

  if (*slot) {
    (*slot)->V = V;
    return;
  }

  *slot =
#if (GCC_MAJOR > 4)
      ggc_alloc<tree2WeakVH>();
#elif GCC_VERSION_CODE > GCC_VERSION(4, 5)
      ggc_alloc_tree2WeakVH();
#else
  GGC_NEW(struct tree2WeakVH);
#endif
  (*slot)->base.from = t;
  WeakVH *W = new (&(*slot)->V) WeakVH(V);
  assert(W == &(*slot)->V && "Pointer was displaced!");
  (void)W;
}
