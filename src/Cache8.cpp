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
#include "dragonegg/Cache.h"

// LLVM headers
#include "llvm/IR/ValueHandle.h"

// System headers
#include <cassert>
#include <gmp.h>

// GCC headers
#include "auto-host.h"
#include "config.h"
// Stop GCC declaring 'getopt' as it can clash with the system's declaration.
#undef HAVE_DECL_GETOPT
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "tree-core.h"

#include "ggc.h"

using namespace llvm;

// Hash table mapping trees to integers.

struct GTY((for_user)) tree2int {
  struct tree_map_base base;
  int GTY((skip)) val;
};

#define tree2int_eq tree_map_base_eq
#define tree2int_hash tree_map_base_hash
#define tree2int_marked_p tree_map_base_marked_p

struct intCacheHasher : ggc_cache_ptr_hash<tree2int> {
  static inline hashval_t hash(tree2int *t2i) {
    return tree_map_base_hash(&t2i->base);
  }

  static inline bool equal(tree2int *a, tree2int *b) {
    return a->base.from == b->base.from;
  }
};
static GTY((cache)) hash_table<intCacheHasher> *intCache;

// Hash table mapping trees to Type*.

// Forward declare Type for the benefit of gengtype.
struct GTY((for_user)) tree2Type {
  struct tree_map_base base;
  Type *GTY((skip)) Ty;
};

#define tree2Type_eq tree_map_base_eq
#define tree2Type_hash tree_map_base_hash
#define tree2Type_marked_p tree_map_base_marked_p

struct TypeCacheHaser : ggc_cache_ptr_hash<tree2Type> {
  static inline hashval_t hash(tree2Type *t2T) {
    return tree_map_base_hash(&t2T->base);
  }

  static inline bool equal(tree2Type *a, tree2Type *b) {
    return a->base.from == b->base.from;
  }
};
static GTY((cache)) hash_table<TypeCacheHaser> *TypeCache;

// Hash table mapping trees to WeakVH.

// Forward declare WeakVH for the benefit of gengtype.
struct GTY((for_user)) tree2WeakVH {
  struct tree_map_base base;
  WeakVH GTY((skip)) V;
};

#define tree2WeakVH_eq tree_map_base_eq
#define tree2WeakVH_hash tree_map_base_hash
#define tree2WeakVH_marked_p tree_map_base_marked_p

struct WeakVHCacheHasher : ggc_cache_ptr_hash<tree2WeakVH> {
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

// Include the garbage collector header.
#include "dragonegg/gt-cache-8.0.inc"

bool getCachedInteger(tree t, int &Val) {
  if (!intCache)
    return false;
  tree2int in;
  in.base.from = t;
  tree2int *h = intCache->find(&in);
  if (!h)
    return false;
  Val = h->val;
  return true;
}

void setCachedInteger(tree t, int Val) {
  if (!intCache)
    intCache = hash_table<intCacheHasher>::create_ggc(1024);

  tree2int in;
  in.base.from = t;
  tree2int **slot = intCache->find_slot(&in, INSERT);
  assert(slot && "Failed to create hash table slot!");

  if (!*slot) {
    *slot = ggc_alloc<tree2int>();
    (*slot)->base.from = t;
  }

  (*slot)->val = Val;
}

Type *getCachedType(tree t) {
  if (!TypeCache)
    return 0;
  tree2Type in;
  in.base.from = t;
  tree2Type *h = TypeCache->find(&in);
  return h ? h->Ty : 0;
}

void setCachedType(tree t, Type *Ty) {
  tree2Type in;
  in.base.from = t;

  /* If deleting, remove the slot.  */
  if (!Ty) {
    if (TypeCache)
      TypeCache->remove_elt(&in);
    return;
  }

  if (!TypeCache)
    TypeCache = hash_table<TypeCacheHaser>::create_ggc(1024);

  tree2Type **slot = TypeCache->find_slot(&in, INSERT);
  assert(slot && "Failed to create hash table slot!");

  if (!*slot) {
    *slot = ggc_alloc<tree2Type>();
    (*slot)->base.from = t;
  }

  (*slot)->Ty = Ty;
}

/// getCachedValue - Returns the value associated with the given GCC tree, or
/// null if none.
Value *getCachedValue(tree t) {
  if (!WeakVHCache)
    return 0;
  tree2WeakVH in;
  in.base.from = t;
  tree2WeakVH *h = WeakVHCache->find(&in);
  return h ? h->V : 0;
}

static void DestructWeakVH(void *p) {
  ((WeakVH *)&((tree2WeakVH *)p)->V)->~WeakVH();
}

/// setCachedValue - Associates the given value (which may be null) with the
/// given GCC tree.  The association is removed if tree is garbage collected
/// or the value deleted.
void setCachedValue(tree t, Value *V) {
  tree2WeakVH in;
  in.base.from = t;

  // If deleting, remove the slot.
  if (!V) {
    if (WeakVHCache)
      WeakVHCache->remove_elt(&in);
    return;
  }

  if (!WeakVHCache)
    WeakVHCache =
        hash_table<WeakVHCacheHasher>::create_ggc(1024);

  tree2WeakVH **slot = WeakVHCache->find_slot(&in, INSERT);
  assert(slot && "Failed to create hash table slot!");

  if (*slot) {
    (*slot)->V = V;
    return;
  }

  *slot = ggc_alloc<tree2WeakVH>();
  (*slot)->base.from = t;
  WeakVH *W = new (&(*slot)->V) WeakVH(V);
  assert(W == &(*slot)->V && "Pointer was displaced!");
  (void)W;
}
