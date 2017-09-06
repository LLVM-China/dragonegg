//==----- Target.h - Target hooks for GCC to LLVM conversion -----*- C++ -*-==//
//
// Copyright (C) 2017 Leslie Zhai <lesliezhai@llvm.org.cn>
// Copyright (C) 2007 to 2013  Anton Korobeynikov, Duncan Sands et al.
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
// This file declares some target-specific hooks for GCC to LLVM conversion.
//===----------------------------------------------------------------------===//

#ifndef DRAGONEGG_TARGET_H
#define DRAGONEGG_TARGET_H

#define LLVM_TARGET_INTRINSIC_PREFIX "mips"

#define LLVM_TARGET_NAME Mips

#endif /* DRAGONEGG_TARGET_H */
