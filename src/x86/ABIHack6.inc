/* Subroutines used for code generation on IA-32.
   Copyright (C) 1988-2016 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "tree.h"
#include "gimple.h"
#include "cfghooks.h"
#include "cfgloop.h"
#include "df.h"
#include "tm_p.h"
#include "stringpool.h"
#include "expmed.h"
#include "regs.h"
#include "emit-rtl.h"
#include "cgraph.h"
#include "diagnostic.h"
#include "cfgbuild.h"
#include "alias.h"
#include "fold-const.h"
#include "attribs.h"
#include "calls.h"
#include "stor-layout.h"
#include "varasm.h"
#include "output.h"
#include "flags.h"
#include "except.h"
#include "explow.h"
#include "expr.h"
#include "cfgrtl.h"
#include "langhooks.h"
#include "gimplify.h"
#include "params.h"
#include "cselib.h"
#include "sched-int.h"
#include "opts.h"
#include "tree-pass.h"
#include "context.h"
#include "pass_manager.h"
#include "target-globals.h"
#include "gimple-iterator.h"
#include "tree-vectorizer.h"
#include "shrink-wrap.h"
#include "builtins.h"
#include "rtl-iter.h"
#include "tree-iterator.h"
#include "tree-chkp.h"
#include "rtl-chkp.h"
#include "dbgcnt.h"
#include "regrename.h"
#include "dojump.h"
#include "fold-const-call.h"
#include "tree-ssanames.h"

static rtx legitimize_dllimport_symbol (rtx, bool);
static rtx legitimize_pe_coff_extern_decl (rtx, bool);
static rtx legitimize_pe_coff_symbol (rtx, bool);
static void ix86_print_operand_address_as (FILE *, rtx, addr_space_t, bool);

#ifndef CHECK_STACK_LIMIT
#define CHECK_STACK_LIMIT (-1)
#endif

/* Return index of given mode in mult and division cost tables.  */
#define MODE_INDEX(mode)					\
  ((mode) == QImode ? 0						\
   : (mode) == HImode ? 1					\
   : (mode) == SImode ? 2					\
   : (mode) == DImode ? 3					\
   : 4)

/* Processor costs (relative to an add) */
/* We assume COSTS_N_INSNS is defined as (N)*4 and an addition is 2 bytes.  */
#define COSTS_N_BYTES(N) ((N) * 2)

#define DUMMY_STRINGOP_ALGS {libcall, {{-1, libcall, false}}}

static stringop_algs ix86_size_memcpy[2] = {
  {rep_prefix_1_byte, {{-1, rep_prefix_1_byte, false}}},
  {rep_prefix_1_byte, {{-1, rep_prefix_1_byte, false}}}};
static stringop_algs ix86_size_memset[2] = {
  {rep_prefix_1_byte, {{-1, rep_prefix_1_byte, false}}},
  {rep_prefix_1_byte, {{-1, rep_prefix_1_byte, false}}}};

const
struct processor_costs ix86_size_cost = {/* costs for tuning for size */
  COSTS_N_BYTES (2),			/* cost of an add instruction */
  COSTS_N_BYTES (3),			/* cost of a lea instruction */
  COSTS_N_BYTES (2),			/* variable shift costs */
  COSTS_N_BYTES (3),			/* constant shift costs */
  {COSTS_N_BYTES (3),			/* cost of starting multiply for QI */
   COSTS_N_BYTES (3),			/*				 HI */
   COSTS_N_BYTES (3),			/*				 SI */
   COSTS_N_BYTES (3),			/*				 DI */
   COSTS_N_BYTES (5)},			/*			      other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_BYTES (3),			/* cost of a divide/mod for QI */
   COSTS_N_BYTES (3),			/*			    HI */
   COSTS_N_BYTES (3),			/*			    SI */
   COSTS_N_BYTES (3),			/*			    DI */
   COSTS_N_BYTES (5)},			/*			    other */
  COSTS_N_BYTES (3),			/* cost of movsx */
  COSTS_N_BYTES (3),			/* cost of movzx */
  0,					/* "large" insn */
  2,					/* MOVE_RATIO */
  2,				     /* cost for loading QImode using movzbl */
  {2, 2, 2},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {2, 2, 2},				/* cost of storing integer registers */
  2,					/* cost of reg,reg fld/fst */
  {2, 2, 2},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {2, 2, 2},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  3,					/* cost of moving MMX register */
  {3, 3},				/* cost of loading MMX registers
					   in SImode and DImode */
  {3, 3},				/* cost of storing MMX registers
					   in SImode and DImode */
  3,					/* cost of moving SSE register */
  {3, 3, 3},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {3, 3, 3},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  3,					/* MMX or SSE register to integer */
  0,					/* size of l1 cache  */
  0,					/* size of l2 cache  */
  0,					/* size of prefetch block */
  0,					/* number of parallel prefetches */
  2,					/* Branch cost */
  COSTS_N_BYTES (2),			/* cost of FADD and FSUB insns.  */
  COSTS_N_BYTES (2),			/* cost of FMUL instruction.  */
  COSTS_N_BYTES (2),			/* cost of FDIV instruction.  */
  COSTS_N_BYTES (2),			/* cost of FABS instruction.  */
  COSTS_N_BYTES (2),			/* cost of FCHS instruction.  */
  COSTS_N_BYTES (2),			/* cost of FSQRT instruction.  */
  ix86_size_memcpy,
  ix86_size_memset,
  1,					/* scalar_stmt_cost.  */
  1,					/* scalar load_cost.  */
  1,					/* scalar_store_cost.  */
  1,					/* vec_stmt_cost.  */
  1,					/* vec_to_scalar_cost.  */
  1,					/* scalar_to_vec_cost.  */
  1,					/* vec_align_load_cost.  */
  1,					/* vec_unalign_load_cost.  */
  1,					/* vec_store_cost.  */
  1,					/* cond_taken_branch_cost.  */
  1,					/* cond_not_taken_branch_cost.  */
};

/* Processor costs (relative to an add) */
static stringop_algs i386_memcpy[2] = {
  {rep_prefix_1_byte, {{-1, rep_prefix_1_byte, false}}},
  DUMMY_STRINGOP_ALGS};
static stringop_algs i386_memset[2] = {
  {rep_prefix_1_byte, {{-1, rep_prefix_1_byte, false}}},
  DUMMY_STRINGOP_ALGS};

static const
struct processor_costs i386_cost = {	/* 386 specific costs */
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (1),			/* cost of a lea instruction */
  COSTS_N_INSNS (3),			/* variable shift costs */
  COSTS_N_INSNS (2),			/* constant shift costs */
  {COSTS_N_INSNS (6),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (6),			/*				 HI */
   COSTS_N_INSNS (6),			/*				 SI */
   COSTS_N_INSNS (6),			/*				 DI */
   COSTS_N_INSNS (6)},			/*			      other */
  COSTS_N_INSNS (1),			/* cost of multiply per each bit set */
  {COSTS_N_INSNS (23),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (23),			/*			    HI */
   COSTS_N_INSNS (23),			/*			    SI */
   COSTS_N_INSNS (23),			/*			    DI */
   COSTS_N_INSNS (23)},			/*			    other */
  COSTS_N_INSNS (3),			/* cost of movsx */
  COSTS_N_INSNS (2),			/* cost of movzx */
  15,					/* "large" insn */
  3,					/* MOVE_RATIO */
  4,				     /* cost for loading QImode using movzbl */
  {2, 4, 2},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {2, 4, 2},				/* cost of storing integer registers */
  2,					/* cost of reg,reg fld/fst */
  {8, 8, 8},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {8, 8, 8},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  2,					/* cost of moving MMX register */
  {4, 8},				/* cost of loading MMX registers
					   in SImode and DImode */
  {4, 8},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {4, 8, 16},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {4, 8, 16},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  3,					/* MMX or SSE register to integer */
  0,					/* size of l1 cache  */
  0,					/* size of l2 cache  */
  0,					/* size of prefetch block */
  0,					/* number of parallel prefetches */
  1,					/* Branch cost */
  COSTS_N_INSNS (23),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (27),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (88),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (22),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (24),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (122),			/* cost of FSQRT instruction.  */
  i386_memcpy,
  i386_memset,
  1,					/* scalar_stmt_cost.  */
  1,					/* scalar load_cost.  */
  1,					/* scalar_store_cost.  */
  1,					/* vec_stmt_cost.  */
  1,					/* vec_to_scalar_cost.  */
  1,					/* scalar_to_vec_cost.  */
  1,					/* vec_align_load_cost.  */
  2,					/* vec_unalign_load_cost.  */
  1,					/* vec_store_cost.  */
  3,					/* cond_taken_branch_cost.  */
  1,					/* cond_not_taken_branch_cost.  */
};

static stringop_algs i486_memcpy[2] = {
  {rep_prefix_4_byte, {{-1, rep_prefix_4_byte, false}}},
  DUMMY_STRINGOP_ALGS};
static stringop_algs i486_memset[2] = {
  {rep_prefix_4_byte, {{-1, rep_prefix_4_byte, false}}},
  DUMMY_STRINGOP_ALGS};

static const
struct processor_costs i486_cost = {	/* 486 specific costs */
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (1),			/* cost of a lea instruction */
  COSTS_N_INSNS (3),			/* variable shift costs */
  COSTS_N_INSNS (2),			/* constant shift costs */
  {COSTS_N_INSNS (12),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (12),			/*				 HI */
   COSTS_N_INSNS (12),			/*				 SI */
   COSTS_N_INSNS (12),			/*				 DI */
   COSTS_N_INSNS (12)},			/*			      other */
  1,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (40),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (40),			/*			    HI */
   COSTS_N_INSNS (40),			/*			    SI */
   COSTS_N_INSNS (40),			/*			    DI */
   COSTS_N_INSNS (40)},			/*			    other */
  COSTS_N_INSNS (3),			/* cost of movsx */
  COSTS_N_INSNS (2),			/* cost of movzx */
  15,					/* "large" insn */
  3,					/* MOVE_RATIO */
  4,				     /* cost for loading QImode using movzbl */
  {2, 4, 2},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {2, 4, 2},				/* cost of storing integer registers */
  2,					/* cost of reg,reg fld/fst */
  {8, 8, 8},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {8, 8, 8},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  2,					/* cost of moving MMX register */
  {4, 8},				/* cost of loading MMX registers
					   in SImode and DImode */
  {4, 8},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {4, 8, 16},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {4, 8, 16},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  3,					/* MMX or SSE register to integer */
  4,					/* size of l1 cache.  486 has 8kB cache
					   shared for code and data, so 4kB is
					   not really precise.  */
  4,					/* size of l2 cache  */
  0,					/* size of prefetch block */
  0,					/* number of parallel prefetches */
  1,					/* Branch cost */
  COSTS_N_INSNS (8),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (16),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (73),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (3),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (3),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (83),			/* cost of FSQRT instruction.  */
  i486_memcpy,
  i486_memset,
  1,					/* scalar_stmt_cost.  */
  1,					/* scalar load_cost.  */
  1,					/* scalar_store_cost.  */
  1,					/* vec_stmt_cost.  */
  1,					/* vec_to_scalar_cost.  */
  1,					/* scalar_to_vec_cost.  */
  1,					/* vec_align_load_cost.  */
  2,					/* vec_unalign_load_cost.  */
  1,					/* vec_store_cost.  */
  3,					/* cond_taken_branch_cost.  */
  1,					/* cond_not_taken_branch_cost.  */
};

static stringop_algs pentium_memcpy[2] = {
  {libcall, {{256, rep_prefix_4_byte, false}, {-1, libcall, false}}},
  DUMMY_STRINGOP_ALGS};
static stringop_algs pentium_memset[2] = {
  {libcall, {{-1, rep_prefix_4_byte, false}}},
  DUMMY_STRINGOP_ALGS};

static const
struct processor_costs pentium_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (1),			/* cost of a lea instruction */
  COSTS_N_INSNS (4),			/* variable shift costs */
  COSTS_N_INSNS (1),			/* constant shift costs */
  {COSTS_N_INSNS (11),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (11),			/*				 HI */
   COSTS_N_INSNS (11),			/*				 SI */
   COSTS_N_INSNS (11),			/*				 DI */
   COSTS_N_INSNS (11)},			/*			      other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (25),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (25),			/*			    HI */
   COSTS_N_INSNS (25),			/*			    SI */
   COSTS_N_INSNS (25),			/*			    DI */
   COSTS_N_INSNS (25)},			/*			    other */
  COSTS_N_INSNS (3),			/* cost of movsx */
  COSTS_N_INSNS (2),			/* cost of movzx */
  8,					/* "large" insn */
  6,					/* MOVE_RATIO */
  6,				     /* cost for loading QImode using movzbl */
  {2, 4, 2},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {2, 4, 2},				/* cost of storing integer registers */
  2,					/* cost of reg,reg fld/fst */
  {2, 2, 6},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {4, 4, 6},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  8,					/* cost of moving MMX register */
  {8, 8},				/* cost of loading MMX registers
					   in SImode and DImode */
  {8, 8},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {4, 8, 16},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {4, 8, 16},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  3,					/* MMX or SSE register to integer */
  8,					/* size of l1 cache.  */
  8,					/* size of l2 cache  */
  0,					/* size of prefetch block */
  0,					/* number of parallel prefetches */
  2,					/* Branch cost */
  COSTS_N_INSNS (3),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (3),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (39),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (1),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (1),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (70),			/* cost of FSQRT instruction.  */
  pentium_memcpy,
  pentium_memset,
  1,					/* scalar_stmt_cost.  */
  1,					/* scalar load_cost.  */
  1,					/* scalar_store_cost.  */
  1,					/* vec_stmt_cost.  */
  1,					/* vec_to_scalar_cost.  */
  1,					/* scalar_to_vec_cost.  */
  1,					/* vec_align_load_cost.  */
  2,					/* vec_unalign_load_cost.  */
  1,					/* vec_store_cost.  */
  3,					/* cond_taken_branch_cost.  */
  1,					/* cond_not_taken_branch_cost.  */
};

static const
struct processor_costs lakemont_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (1) + 1,		/* cost of a lea instruction */
  COSTS_N_INSNS (1),			/* variable shift costs */
  COSTS_N_INSNS (1),			/* constant shift costs */
  {COSTS_N_INSNS (11),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (11),			/*				 HI */
   COSTS_N_INSNS (11),			/*				 SI */
   COSTS_N_INSNS (11),			/*				 DI */
   COSTS_N_INSNS (11)},			/*			      other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (25),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (25),			/*			    HI */
   COSTS_N_INSNS (25),			/*			    SI */
   COSTS_N_INSNS (25),			/*			    DI */
   COSTS_N_INSNS (25)},			/*			    other */
  COSTS_N_INSNS (3),			/* cost of movsx */
  COSTS_N_INSNS (2),			/* cost of movzx */
  8,					/* "large" insn */
  9,					/* MOVE_RATIO */
  6,				     /* cost for loading QImode using movzbl */
  {2, 4, 2},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {2, 4, 2},				/* cost of storing integer registers */
  2,					/* cost of reg,reg fld/fst */
  {2, 2, 6},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {4, 4, 6},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  8,					/* cost of moving MMX register */
  {8, 8},				/* cost of loading MMX registers
					   in SImode and DImode */
  {8, 8},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {4, 8, 16},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {4, 8, 16},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  3,					/* MMX or SSE register to integer */
  8,					/* size of l1 cache.  */
  8,					/* size of l2 cache  */
  0,					/* size of prefetch block */
  0,					/* number of parallel prefetches */
  2,					/* Branch cost */
  COSTS_N_INSNS (3),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (3),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (39),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (1),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (1),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (70),			/* cost of FSQRT instruction.  */
  pentium_memcpy,
  pentium_memset,
  1,					/* scalar_stmt_cost.  */
  1,					/* scalar load_cost.  */
  1,					/* scalar_store_cost.  */
  1,					/* vec_stmt_cost.  */
  1,					/* vec_to_scalar_cost.  */
  1,					/* scalar_to_vec_cost.  */
  1,					/* vec_align_load_cost.  */
  2,					/* vec_unalign_load_cost.  */
  1,					/* vec_store_cost.  */
  3,					/* cond_taken_branch_cost.  */
  1,					/* cond_not_taken_branch_cost.  */
};

/* PentiumPro has optimized rep instructions for blocks aligned by 8 bytes
   (we ensure the alignment).  For small blocks inline loop is still a
   noticeable win, for bigger blocks either rep movsl or rep movsb is
   way to go.  Rep movsb has apparently more expensive startup time in CPU,
   but after 4K the difference is down in the noise.  */
static stringop_algs pentiumpro_memcpy[2] = {
  {rep_prefix_4_byte, {{128, loop, false}, {1024, unrolled_loop, false},
                       {8192, rep_prefix_4_byte, false},
                       {-1, rep_prefix_1_byte, false}}},
  DUMMY_STRINGOP_ALGS};
static stringop_algs pentiumpro_memset[2] = {
  {rep_prefix_4_byte, {{1024, unrolled_loop, false},
                       {8192, rep_prefix_4_byte, false},
                       {-1, libcall, false}}},
  DUMMY_STRINGOP_ALGS};
static const
struct processor_costs pentiumpro_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (1),			/* cost of a lea instruction */
  COSTS_N_INSNS (1),			/* variable shift costs */
  COSTS_N_INSNS (1),			/* constant shift costs */
  {COSTS_N_INSNS (4),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (4),			/*				 HI */
   COSTS_N_INSNS (4),			/*				 SI */
   COSTS_N_INSNS (4),			/*				 DI */
   COSTS_N_INSNS (4)},			/*			      other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (17),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (17),			/*			    HI */
   COSTS_N_INSNS (17),			/*			    SI */
   COSTS_N_INSNS (17),			/*			    DI */
   COSTS_N_INSNS (17)},			/*			    other */
  COSTS_N_INSNS (1),			/* cost of movsx */
  COSTS_N_INSNS (1),			/* cost of movzx */
  8,					/* "large" insn */
  6,					/* MOVE_RATIO */
  2,				     /* cost for loading QImode using movzbl */
  {4, 4, 4},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {2, 2, 2},				/* cost of storing integer registers */
  2,					/* cost of reg,reg fld/fst */
  {2, 2, 6},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {4, 4, 6},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  2,					/* cost of moving MMX register */
  {2, 2},				/* cost of loading MMX registers
					   in SImode and DImode */
  {2, 2},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {2, 2, 8},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {2, 2, 8},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  3,					/* MMX or SSE register to integer */
  8,					/* size of l1 cache.  */
  256,					/* size of l2 cache  */
  32,					/* size of prefetch block */
  6,					/* number of parallel prefetches */
  2,					/* Branch cost */
  COSTS_N_INSNS (3),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (5),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (56),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (2),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (2),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (56),			/* cost of FSQRT instruction.  */
  pentiumpro_memcpy,
  pentiumpro_memset,
  1,					/* scalar_stmt_cost.  */
  1,					/* scalar load_cost.  */
  1,					/* scalar_store_cost.  */
  1,					/* vec_stmt_cost.  */
  1,					/* vec_to_scalar_cost.  */
  1,					/* scalar_to_vec_cost.  */
  1,					/* vec_align_load_cost.  */
  2,					/* vec_unalign_load_cost.  */
  1,					/* vec_store_cost.  */
  3,					/* cond_taken_branch_cost.  */
  1,					/* cond_not_taken_branch_cost.  */
};

static stringop_algs geode_memcpy[2] = {
  {libcall, {{256, rep_prefix_4_byte, false}, {-1, libcall, false}}},
  DUMMY_STRINGOP_ALGS};
static stringop_algs geode_memset[2] = {
  {libcall, {{256, rep_prefix_4_byte, false}, {-1, libcall, false}}},
  DUMMY_STRINGOP_ALGS};
static const
struct processor_costs geode_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (1),			/* cost of a lea instruction */
  COSTS_N_INSNS (2),			/* variable shift costs */
  COSTS_N_INSNS (1),			/* constant shift costs */
  {COSTS_N_INSNS (3),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (4),			/*				 HI */
   COSTS_N_INSNS (7),			/*				 SI */
   COSTS_N_INSNS (7),			/*				 DI */
   COSTS_N_INSNS (7)},			/*			      other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (15),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (23),			/*			    HI */
   COSTS_N_INSNS (39),			/*			    SI */
   COSTS_N_INSNS (39),			/*			    DI */
   COSTS_N_INSNS (39)},			/*			    other */
  COSTS_N_INSNS (1),			/* cost of movsx */
  COSTS_N_INSNS (1),			/* cost of movzx */
  8,					/* "large" insn */
  4,					/* MOVE_RATIO */
  1,				     /* cost for loading QImode using movzbl */
  {1, 1, 1},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {1, 1, 1},				/* cost of storing integer registers */
  1,					/* cost of reg,reg fld/fst */
  {1, 1, 1},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {4, 6, 6},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */

  2,					/* cost of moving MMX register */
  {2, 2},				/* cost of loading MMX registers
					   in SImode and DImode */
  {2, 2},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {2, 2, 8},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {2, 2, 8},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  3,					/* MMX or SSE register to integer */
  64,					/* size of l1 cache.  */
  128,					/* size of l2 cache.  */
  32,					/* size of prefetch block */
  1,					/* number of parallel prefetches */
  1,					/* Branch cost */
  COSTS_N_INSNS (6),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (11),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (47),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (1),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (1),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (54),			/* cost of FSQRT instruction.  */
  geode_memcpy,
  geode_memset,
  1,					/* scalar_stmt_cost.  */
  1,					/* scalar load_cost.  */
  1,					/* scalar_store_cost.  */
  1,					/* vec_stmt_cost.  */
  1,					/* vec_to_scalar_cost.  */
  1,					/* scalar_to_vec_cost.  */
  1,					/* vec_align_load_cost.  */
  2,					/* vec_unalign_load_cost.  */
  1,					/* vec_store_cost.  */
  3,					/* cond_taken_branch_cost.  */
  1,					/* cond_not_taken_branch_cost.  */
};

static stringop_algs k6_memcpy[2] = {
  {libcall, {{256, rep_prefix_4_byte, false}, {-1, libcall, false}}},
  DUMMY_STRINGOP_ALGS};
static stringop_algs k6_memset[2] = {
  {libcall, {{256, rep_prefix_4_byte, false}, {-1, libcall, false}}},
  DUMMY_STRINGOP_ALGS};
static const
struct processor_costs k6_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (2),			/* cost of a lea instruction */
  COSTS_N_INSNS (1),			/* variable shift costs */
  COSTS_N_INSNS (1),			/* constant shift costs */
  {COSTS_N_INSNS (3),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (3),			/*				 HI */
   COSTS_N_INSNS (3),			/*				 SI */
   COSTS_N_INSNS (3),			/*				 DI */
   COSTS_N_INSNS (3)},			/*			      other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (18),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (18),			/*			    HI */
   COSTS_N_INSNS (18),			/*			    SI */
   COSTS_N_INSNS (18),			/*			    DI */
   COSTS_N_INSNS (18)},			/*			    other */
  COSTS_N_INSNS (2),			/* cost of movsx */
  COSTS_N_INSNS (2),			/* cost of movzx */
  8,					/* "large" insn */
  4,					/* MOVE_RATIO */
  3,				     /* cost for loading QImode using movzbl */
  {4, 5, 4},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {2, 3, 2},				/* cost of storing integer registers */
  4,					/* cost of reg,reg fld/fst */
  {6, 6, 6},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {4, 4, 4},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  2,					/* cost of moving MMX register */
  {2, 2},				/* cost of loading MMX registers
					   in SImode and DImode */
  {2, 2},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {2, 2, 8},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {2, 2, 8},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  6,					/* MMX or SSE register to integer */
  32,					/* size of l1 cache.  */
  32,					/* size of l2 cache.  Some models
					   have integrated l2 cache, but
					   optimizing for k6 is not important
					   enough to worry about that.  */
  32,					/* size of prefetch block */
  1,					/* number of parallel prefetches */
  1,					/* Branch cost */
  COSTS_N_INSNS (2),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (2),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (56),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (2),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (2),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (56),			/* cost of FSQRT instruction.  */
  k6_memcpy,
  k6_memset,
  1,					/* scalar_stmt_cost.  */
  1,					/* scalar load_cost.  */
  1,					/* scalar_store_cost.  */
  1,					/* vec_stmt_cost.  */
  1,					/* vec_to_scalar_cost.  */
  1,					/* scalar_to_vec_cost.  */
  1,					/* vec_align_load_cost.  */
  2,					/* vec_unalign_load_cost.  */
  1,					/* vec_store_cost.  */
  3,					/* cond_taken_branch_cost.  */
  1,					/* cond_not_taken_branch_cost.  */
};

/* For some reason, Athlon deals better with REP prefix (relative to loops)
   compared to K8. Alignment becomes important after 8 bytes for memcpy and
   128 bytes for memset.  */
static stringop_algs athlon_memcpy[2] = {
  {libcall, {{2048, rep_prefix_4_byte, false}, {-1, libcall, false}}},
  DUMMY_STRINGOP_ALGS};
static stringop_algs athlon_memset[2] = {
  {libcall, {{2048, rep_prefix_4_byte, false}, {-1, libcall, false}}},
  DUMMY_STRINGOP_ALGS};
static const
struct processor_costs athlon_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (2),			/* cost of a lea instruction */
  COSTS_N_INSNS (1),			/* variable shift costs */
  COSTS_N_INSNS (1),			/* constant shift costs */
  {COSTS_N_INSNS (5),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (5),			/*				 HI */
   COSTS_N_INSNS (5),			/*				 SI */
   COSTS_N_INSNS (5),			/*				 DI */
   COSTS_N_INSNS (5)},			/*			      other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (18),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (26),			/*			    HI */
   COSTS_N_INSNS (42),			/*			    SI */
   COSTS_N_INSNS (74),			/*			    DI */
   COSTS_N_INSNS (74)},			/*			    other */
  COSTS_N_INSNS (1),			/* cost of movsx */
  COSTS_N_INSNS (1),			/* cost of movzx */
  8,					/* "large" insn */
  9,					/* MOVE_RATIO */
  4,				     /* cost for loading QImode using movzbl */
  {3, 4, 3},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {3, 4, 3},				/* cost of storing integer registers */
  4,					/* cost of reg,reg fld/fst */
  {4, 4, 12},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {6, 6, 8},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  2,					/* cost of moving MMX register */
  {4, 4},				/* cost of loading MMX registers
					   in SImode and DImode */
  {4, 4},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {4, 4, 6},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {4, 4, 5},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  5,					/* MMX or SSE register to integer */
  64,					/* size of l1 cache.  */
  256,					/* size of l2 cache.  */
  64,					/* size of prefetch block */
  6,					/* number of parallel prefetches */
  5,					/* Branch cost */
  COSTS_N_INSNS (4),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (4),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (24),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (2),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (2),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (35),			/* cost of FSQRT instruction.  */
  athlon_memcpy,
  athlon_memset,
  1,					/* scalar_stmt_cost.  */
  1,					/* scalar load_cost.  */
  1,					/* scalar_store_cost.  */
  1,					/* vec_stmt_cost.  */
  1,					/* vec_to_scalar_cost.  */
  1,					/* scalar_to_vec_cost.  */
  1,					/* vec_align_load_cost.  */
  2,					/* vec_unalign_load_cost.  */
  1,					/* vec_store_cost.  */
  3,					/* cond_taken_branch_cost.  */
  1,					/* cond_not_taken_branch_cost.  */
};

/* K8 has optimized REP instruction for medium sized blocks, but for very
   small blocks it is better to use loop. For large blocks, libcall can
   do nontemporary accesses and beat inline considerably.  */
static stringop_algs k8_memcpy[2] = {
  {libcall, {{6, loop, false}, {14, unrolled_loop, false},
             {-1, rep_prefix_4_byte, false}}},
  {libcall, {{16, loop, false}, {8192, rep_prefix_8_byte, false},
             {-1, libcall, false}}}};
static stringop_algs k8_memset[2] = {
  {libcall, {{8, loop, false}, {24, unrolled_loop, false},
             {2048, rep_prefix_4_byte, false}, {-1, libcall, false}}},
  {libcall, {{48, unrolled_loop, false},
             {8192, rep_prefix_8_byte, false}, {-1, libcall, false}}}};
static const
struct processor_costs k8_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (2),			/* cost of a lea instruction */
  COSTS_N_INSNS (1),			/* variable shift costs */
  COSTS_N_INSNS (1),			/* constant shift costs */
  {COSTS_N_INSNS (3),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (4),			/*				 HI */
   COSTS_N_INSNS (3),			/*				 SI */
   COSTS_N_INSNS (4),			/*				 DI */
   COSTS_N_INSNS (5)},			/*			      other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (18),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (26),			/*			    HI */
   COSTS_N_INSNS (42),			/*			    SI */
   COSTS_N_INSNS (74),			/*			    DI */
   COSTS_N_INSNS (74)},			/*			    other */
  COSTS_N_INSNS (1),			/* cost of movsx */
  COSTS_N_INSNS (1),			/* cost of movzx */
  8,					/* "large" insn */
  9,					/* MOVE_RATIO */
  4,				     /* cost for loading QImode using movzbl */
  {3, 4, 3},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {3, 4, 3},				/* cost of storing integer registers */
  4,					/* cost of reg,reg fld/fst */
  {4, 4, 12},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {6, 6, 8},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  2,					/* cost of moving MMX register */
  {3, 3},				/* cost of loading MMX registers
					   in SImode and DImode */
  {4, 4},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {4, 3, 6},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {4, 4, 5},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  5,					/* MMX or SSE register to integer */
  64,					/* size of l1 cache.  */
  512,					/* size of l2 cache.  */
  64,					/* size of prefetch block */
  /* New AMD processors never drop prefetches; if they cannot be performed
     immediately, they are queued.  We set number of simultaneous prefetches
     to a large constant to reflect this (it probably is not a good idea not
     to limit number of prefetches at all, as their execution also takes some
     time).  */
  100,					/* number of parallel prefetches */
  3,					/* Branch cost */
  COSTS_N_INSNS (4),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (4),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (19),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (2),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (2),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (35),			/* cost of FSQRT instruction.  */

  k8_memcpy,
  k8_memset,
  4,					/* scalar_stmt_cost.  */
  2,					/* scalar load_cost.  */
  2,					/* scalar_store_cost.  */
  5,					/* vec_stmt_cost.  */
  0,					/* vec_to_scalar_cost.  */
  2,					/* scalar_to_vec_cost.  */
  2,					/* vec_align_load_cost.  */
  3,					/* vec_unalign_load_cost.  */
  3,					/* vec_store_cost.  */
  3,					/* cond_taken_branch_cost.  */
  2,					/* cond_not_taken_branch_cost.  */
};

/* AMDFAM10 has optimized REP instruction for medium sized blocks, but for
   very small blocks it is better to use loop. For large blocks, libcall can
   do nontemporary accesses and beat inline considerably.  */
static stringop_algs amdfam10_memcpy[2] = {
  {libcall, {{6, loop, false}, {14, unrolled_loop, false},
             {-1, rep_prefix_4_byte, false}}},
  {libcall, {{16, loop, false}, {8192, rep_prefix_8_byte, false},
             {-1, libcall, false}}}};
static stringop_algs amdfam10_memset[2] = {
  {libcall, {{8, loop, false}, {24, unrolled_loop, false},
             {2048, rep_prefix_4_byte, false}, {-1, libcall, false}}},
  {libcall, {{48, unrolled_loop, false}, {8192, rep_prefix_8_byte, false},
             {-1, libcall, false}}}};
struct processor_costs amdfam10_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (2),			/* cost of a lea instruction */
  COSTS_N_INSNS (1),			/* variable shift costs */
  COSTS_N_INSNS (1),			/* constant shift costs */
  {COSTS_N_INSNS (3),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (4),			/*				 HI */
   COSTS_N_INSNS (3),			/*				 SI */
   COSTS_N_INSNS (4),			/*				 DI */
   COSTS_N_INSNS (5)},			/*			      other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (19),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (35),			/*			    HI */
   COSTS_N_INSNS (51),			/*			    SI */
   COSTS_N_INSNS (83),			/*			    DI */
   COSTS_N_INSNS (83)},			/*			    other */
  COSTS_N_INSNS (1),			/* cost of movsx */
  COSTS_N_INSNS (1),			/* cost of movzx */
  8,					/* "large" insn */
  9,					/* MOVE_RATIO */
  4,				     /* cost for loading QImode using movzbl */
  {3, 4, 3},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {3, 4, 3},				/* cost of storing integer registers */
  4,					/* cost of reg,reg fld/fst */
  {4, 4, 12},				/* cost of loading fp registers
		   			   in SFmode, DFmode and XFmode */
  {6, 6, 8},				/* cost of storing fp registers
 		   			   in SFmode, DFmode and XFmode */
  2,					/* cost of moving MMX register */
  {3, 3},				/* cost of loading MMX registers
					   in SImode and DImode */
  {4, 4},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {4, 4, 3},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {4, 4, 5},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  3,					/* MMX or SSE register to integer */
  					/* On K8:
  					    MOVD reg64, xmmreg Double FSTORE 4
					    MOVD reg32, xmmreg Double FSTORE 4
					   On AMDFAM10:
					    MOVD reg64, xmmreg Double FADD 3
							       1/1  1/1
					    MOVD reg32, xmmreg Double FADD 3
							       1/1  1/1 */
  64,					/* size of l1 cache.  */
  512,					/* size of l2 cache.  */
  64,					/* size of prefetch block */
  /* New AMD processors never drop prefetches; if they cannot be performed
     immediately, they are queued.  We set number of simultaneous prefetches
     to a large constant to reflect this (it probably is not a good idea not
     to limit number of prefetches at all, as their execution also takes some
     time).  */
  100,					/* number of parallel prefetches */
  2,					/* Branch cost */
  COSTS_N_INSNS (4),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (4),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (19),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (2),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (2),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (35),			/* cost of FSQRT instruction.  */

  amdfam10_memcpy,
  amdfam10_memset,
  4,					/* scalar_stmt_cost.  */
  2,					/* scalar load_cost.  */
  2,					/* scalar_store_cost.  */
  6,					/* vec_stmt_cost.  */
  0,					/* vec_to_scalar_cost.  */
  2,					/* scalar_to_vec_cost.  */
  2,					/* vec_align_load_cost.  */
  2,					/* vec_unalign_load_cost.  */
  2,					/* vec_store_cost.  */
  2,					/* cond_taken_branch_cost.  */
  1,					/* cond_not_taken_branch_cost.  */
};

/*  BDVER1 has optimized REP instruction for medium sized blocks, but for
    very small blocks it is better to use loop. For large blocks, libcall
    can do nontemporary accesses and beat inline considerably.  */
static stringop_algs bdver1_memcpy[2] = {
  {libcall, {{6, loop, false}, {14, unrolled_loop, false},
             {-1, rep_prefix_4_byte, false}}},
  {libcall, {{16, loop, false}, {8192, rep_prefix_8_byte, false},
             {-1, libcall, false}}}};
static stringop_algs bdver1_memset[2] = {
  {libcall, {{8, loop, false}, {24, unrolled_loop, false},
             {2048, rep_prefix_4_byte, false}, {-1, libcall, false}}},
  {libcall, {{48, unrolled_loop, false}, {8192, rep_prefix_8_byte, false},
             {-1, libcall, false}}}};

const struct processor_costs bdver1_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (1),			/* cost of a lea instruction */
  COSTS_N_INSNS (1),			/* variable shift costs */
  COSTS_N_INSNS (1),			/* constant shift costs */
  {COSTS_N_INSNS (4),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (4),			/*				 HI */
   COSTS_N_INSNS (4),			/*				 SI */
   COSTS_N_INSNS (6),			/*				 DI */
   COSTS_N_INSNS (6)},			/*			      other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (19),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (35),			/*			    HI */
   COSTS_N_INSNS (51),			/*			    SI */
   COSTS_N_INSNS (83),			/*			    DI */
   COSTS_N_INSNS (83)},			/*			    other */
  COSTS_N_INSNS (1),			/* cost of movsx */
  COSTS_N_INSNS (1),			/* cost of movzx */
  8,					/* "large" insn */
  9,					/* MOVE_RATIO */
  4,				     /* cost for loading QImode using movzbl */
  {5, 5, 4},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {4, 4, 4},				/* cost of storing integer registers */
  2,					/* cost of reg,reg fld/fst */
  {5, 5, 12},				/* cost of loading fp registers
		   			   in SFmode, DFmode and XFmode */
  {4, 4, 8},				/* cost of storing fp registers
 		   			   in SFmode, DFmode and XFmode */
  2,					/* cost of moving MMX register */
  {4, 4},				/* cost of loading MMX registers
					   in SImode and DImode */
  {4, 4},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {4, 4, 4},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {4, 4, 4},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  2,					/* MMX or SSE register to integer */
  					/* On K8:
					    MOVD reg64, xmmreg Double FSTORE 4
					    MOVD reg32, xmmreg Double FSTORE 4
					   On AMDFAM10:
					    MOVD reg64, xmmreg Double FADD 3
							       1/1  1/1
					    MOVD reg32, xmmreg Double FADD 3
							       1/1  1/1 */
  16,					/* size of l1 cache.  */
  2048,					/* size of l2 cache.  */
  64,					/* size of prefetch block */
  /* New AMD processors never drop prefetches; if they cannot be performed
     immediately, they are queued.  We set number of simultaneous prefetches
     to a large constant to reflect this (it probably is not a good idea not
     to limit number of prefetches at all, as their execution also takes some
     time).  */
  100,					/* number of parallel prefetches */
  2,					/* Branch cost */
  COSTS_N_INSNS (6),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (6),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (42),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (2),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (2),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (52),			/* cost of FSQRT instruction.  */

  bdver1_memcpy,
  bdver1_memset,
  6,					/* scalar_stmt_cost.  */
  4,					/* scalar load_cost.  */
  4,					/* scalar_store_cost.  */
  6,					/* vec_stmt_cost.  */
  0,					/* vec_to_scalar_cost.  */
  2,					/* scalar_to_vec_cost.  */
  4,					/* vec_align_load_cost.  */
  4,					/* vec_unalign_load_cost.  */
  4,					/* vec_store_cost.  */
  4,					/* cond_taken_branch_cost.  */
  2,					/* cond_not_taken_branch_cost.  */
};

/*  BDVER2 has optimized REP instruction for medium sized blocks, but for
    very small blocks it is better to use loop. For large blocks, libcall
    can do nontemporary accesses and beat inline considerably.  */

static stringop_algs bdver2_memcpy[2] = {
  {libcall, {{6, loop, false}, {14, unrolled_loop, false},
             {-1, rep_prefix_4_byte, false}}},
  {libcall, {{16, loop, false}, {8192, rep_prefix_8_byte, false},
             {-1, libcall, false}}}};
static stringop_algs bdver2_memset[2] = {
  {libcall, {{8, loop, false}, {24, unrolled_loop, false},
             {2048, rep_prefix_4_byte, false}, {-1, libcall, false}}},
  {libcall, {{48, unrolled_loop, false}, {8192, rep_prefix_8_byte, false},
             {-1, libcall, false}}}};

const struct processor_costs bdver2_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (1),			/* cost of a lea instruction */
  COSTS_N_INSNS (1),			/* variable shift costs */
  COSTS_N_INSNS (1),			/* constant shift costs */
  {COSTS_N_INSNS (4),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (4),			/*				 HI */
   COSTS_N_INSNS (4),			/*				 SI */
   COSTS_N_INSNS (6),			/*				 DI */
   COSTS_N_INSNS (6)},			/*			      other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (19),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (35),			/*			    HI */
   COSTS_N_INSNS (51),			/*			    SI */
   COSTS_N_INSNS (83),			/*			    DI */
   COSTS_N_INSNS (83)},			/*			    other */
  COSTS_N_INSNS (1),			/* cost of movsx */
  COSTS_N_INSNS (1),			/* cost of movzx */
  8,					/* "large" insn */
  9,					/* MOVE_RATIO */
  4,				     /* cost for loading QImode using movzbl */
  {5, 5, 4},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {4, 4, 4},				/* cost of storing integer registers */
  2,					/* cost of reg,reg fld/fst */
  {5, 5, 12},				/* cost of loading fp registers
		   			   in SFmode, DFmode and XFmode */
  {4, 4, 8},				/* cost of storing fp registers
 		   			   in SFmode, DFmode and XFmode */
  2,					/* cost of moving MMX register */
  {4, 4},				/* cost of loading MMX registers
					   in SImode and DImode */
  {4, 4},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {4, 4, 4},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {4, 4, 4},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  2,					/* MMX or SSE register to integer */
  					/* On K8:
					    MOVD reg64, xmmreg Double FSTORE 4
					    MOVD reg32, xmmreg Double FSTORE 4
					   On AMDFAM10:
					    MOVD reg64, xmmreg Double FADD 3
							       1/1  1/1
					    MOVD reg32, xmmreg Double FADD 3
							       1/1  1/1 */
  16,					/* size of l1 cache.  */
  2048,					/* size of l2 cache.  */
  64,					/* size of prefetch block */
  /* New AMD processors never drop prefetches; if they cannot be performed
     immediately, they are queued.  We set number of simultaneous prefetches
     to a large constant to reflect this (it probably is not a good idea not
     to limit number of prefetches at all, as their execution also takes some
     time).  */
  100,					/* number of parallel prefetches */
  2,					/* Branch cost */
  COSTS_N_INSNS (6),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (6),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (42),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (2),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (2),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (52),			/* cost of FSQRT instruction.  */

  bdver2_memcpy,
  bdver2_memset,
  6,					/* scalar_stmt_cost.  */
  4,					/* scalar load_cost.  */
  4,					/* scalar_store_cost.  */
  6,					/* vec_stmt_cost.  */
  0,					/* vec_to_scalar_cost.  */
  2,					/* scalar_to_vec_cost.  */
  4,					/* vec_align_load_cost.  */
  4,					/* vec_unalign_load_cost.  */
  4,					/* vec_store_cost.  */
  4,					/* cond_taken_branch_cost.  */
  2,					/* cond_not_taken_branch_cost.  */
};


  /*  BDVER3 has optimized REP instruction for medium sized blocks, but for
      very small blocks it is better to use loop. For large blocks, libcall
      can do nontemporary accesses and beat inline considerably.  */
static stringop_algs bdver3_memcpy[2] = {
  {libcall, {{6, loop, false}, {14, unrolled_loop, false},
             {-1, rep_prefix_4_byte, false}}},
  {libcall, {{16, loop, false}, {8192, rep_prefix_8_byte, false},
             {-1, libcall, false}}}};
static stringop_algs bdver3_memset[2] = {
  {libcall, {{8, loop, false}, {24, unrolled_loop, false},
             {2048, rep_prefix_4_byte, false}, {-1, libcall, false}}},
  {libcall, {{48, unrolled_loop, false}, {8192, rep_prefix_8_byte, false},
             {-1, libcall, false}}}};
struct processor_costs bdver3_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (1),			/* cost of a lea instruction */
  COSTS_N_INSNS (1),			/* variable shift costs */
  COSTS_N_INSNS (1),			/* constant shift costs */
  {COSTS_N_INSNS (4),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (4),			/*				 HI */
   COSTS_N_INSNS (4),			/*				 SI */
   COSTS_N_INSNS (6),			/*				 DI */
   COSTS_N_INSNS (6)},			/*			      other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (19),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (35),			/*			    HI */
   COSTS_N_INSNS (51),			/*			    SI */
   COSTS_N_INSNS (83),			/*			    DI */
   COSTS_N_INSNS (83)},			/*			    other */
  COSTS_N_INSNS (1),			/* cost of movsx */
  COSTS_N_INSNS (1),			/* cost of movzx */
  8,					/* "large" insn */
  9,					/* MOVE_RATIO */
  4,				     /* cost for loading QImode using movzbl */
  {5, 5, 4},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {4, 4, 4},				/* cost of storing integer registers */
  2,					/* cost of reg,reg fld/fst */
  {5, 5, 12},				/* cost of loading fp registers
		   			   in SFmode, DFmode and XFmode */
  {4, 4, 8},				/* cost of storing fp registers
 		   			   in SFmode, DFmode and XFmode */
  2,					/* cost of moving MMX register */
  {4, 4},				/* cost of loading MMX registers
					   in SImode and DImode */
  {4, 4},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {4, 4, 4},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {4, 4, 4},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  2,					/* MMX or SSE register to integer */
  16,					/* size of l1 cache.  */
  2048,					/* size of l2 cache.  */
  64,					/* size of prefetch block */
  /* New AMD processors never drop prefetches; if they cannot be performed
     immediately, they are queued.  We set number of simultaneous prefetches
     to a large constant to reflect this (it probably is not a good idea not
     to limit number of prefetches at all, as their execution also takes some
     time).  */
  100,					/* number of parallel prefetches */
  2,					/* Branch cost */
  COSTS_N_INSNS (6),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (6),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (42),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (2),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (2),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (52),			/* cost of FSQRT instruction.  */

  bdver3_memcpy,
  bdver3_memset,
  6,					/* scalar_stmt_cost.  */
  4,					/* scalar load_cost.  */
  4,					/* scalar_store_cost.  */
  6,					/* vec_stmt_cost.  */
  0,					/* vec_to_scalar_cost.  */
  2,					/* scalar_to_vec_cost.  */
  4,					/* vec_align_load_cost.  */
  4,					/* vec_unalign_load_cost.  */
  4,					/* vec_store_cost.  */
  4,					/* cond_taken_branch_cost.  */
  2,					/* cond_not_taken_branch_cost.  */
};

/*  BDVER4 has optimized REP instruction for medium sized blocks, but for
    very small blocks it is better to use loop. For large blocks, libcall
    can do nontemporary accesses and beat inline considerably.  */
static stringop_algs bdver4_memcpy[2] = {
  {libcall, {{6, loop, false}, {14, unrolled_loop, false},
             {-1, rep_prefix_4_byte, false}}},
  {libcall, {{16, loop, false}, {8192, rep_prefix_8_byte, false},
             {-1, libcall, false}}}};
static stringop_algs bdver4_memset[2] = {
  {libcall, {{8, loop, false}, {24, unrolled_loop, false},
             {2048, rep_prefix_4_byte, false}, {-1, libcall, false}}},
  {libcall, {{48, unrolled_loop, false}, {8192, rep_prefix_8_byte, false},
             {-1, libcall, false}}}};
struct processor_costs bdver4_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (1),			/* cost of a lea instruction */
  COSTS_N_INSNS (1),			/* variable shift costs */
  COSTS_N_INSNS (1),			/* constant shift costs */
  {COSTS_N_INSNS (4),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (4),			/*				 HI */
   COSTS_N_INSNS (4),			/*				 SI */
   COSTS_N_INSNS (6),			/*				 DI */
   COSTS_N_INSNS (6)},			/*			      other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (19),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (35),			/*			    HI */
   COSTS_N_INSNS (51),			/*			    SI */
   COSTS_N_INSNS (83),			/*			    DI */
   COSTS_N_INSNS (83)},			/*			    other */
  COSTS_N_INSNS (1),			/* cost of movsx */
  COSTS_N_INSNS (1),			/* cost of movzx */
  8,					/* "large" insn */
  9,					/* MOVE_RATIO */
  4,				     /* cost for loading QImode using movzbl */
  {5, 5, 4},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {4, 4, 4},				/* cost of storing integer registers */
  2,					/* cost of reg,reg fld/fst */
  {5, 5, 12},				/* cost of loading fp registers
		   			   in SFmode, DFmode and XFmode */
  {4, 4, 8},				/* cost of storing fp registers
 		   			   in SFmode, DFmode and XFmode */
  2,					/* cost of moving MMX register */
  {4, 4},				/* cost of loading MMX registers
					   in SImode and DImode */
  {4, 4},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {4, 4, 4},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {4, 4, 4},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  2,					/* MMX or SSE register to integer */
  16,					/* size of l1 cache.  */
  2048,					/* size of l2 cache.  */
  64,					/* size of prefetch block */
  /* New AMD processors never drop prefetches; if they cannot be performed
     immediately, they are queued.  We set number of simultaneous prefetches
     to a large constant to reflect this (it probably is not a good idea not
     to limit number of prefetches at all, as their execution also takes some
     time).  */
  100,					/* number of parallel prefetches */
  2,					/* Branch cost */
  COSTS_N_INSNS (6),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (6),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (42),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (2),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (2),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (52),			/* cost of FSQRT instruction.  */

  bdver4_memcpy,
  bdver4_memset,
  6,					/* scalar_stmt_cost.  */
  4,					/* scalar load_cost.  */
  4,					/* scalar_store_cost.  */
  6,					/* vec_stmt_cost.  */
  0,					/* vec_to_scalar_cost.  */
  2,					/* scalar_to_vec_cost.  */
  4,					/* vec_align_load_cost.  */
  4,					/* vec_unalign_load_cost.  */
  4,					/* vec_store_cost.  */
  4,					/* cond_taken_branch_cost.  */
  2,					/* cond_not_taken_branch_cost.  */
};


/*  ZNVER1 has optimized REP instruction for medium sized blocks, but for
    very small blocks it is better to use loop.  For large blocks, libcall
    can do nontemporary accesses and beat inline considerably.  */
static stringop_algs znver1_memcpy[2] = {
  {libcall, {{6, loop, false}, {14, unrolled_loop, false},
	     {-1, rep_prefix_4_byte, false}}},
  {libcall, {{16, loop, false}, {8192, rep_prefix_8_byte, false},
	     {-1, libcall, false}}}};
static stringop_algs znver1_memset[2] = {
  {libcall, {{8, loop, false}, {24, unrolled_loop, false},
	     {2048, rep_prefix_4_byte, false}, {-1, libcall, false}}},
  {libcall, {{48, unrolled_loop, false}, {8192, rep_prefix_8_byte, false},
	     {-1, libcall, false}}}};
struct processor_costs znver1_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction.  */
  COSTS_N_INSNS (1),			/* cost of a lea instruction.  */
  COSTS_N_INSNS (1),			/* variable shift costs.  */
  COSTS_N_INSNS (1),			/* constant shift costs.  */
  {COSTS_N_INSNS (3),			/* cost of starting multiply for QI.  */
   COSTS_N_INSNS (3),			/*				 HI.  */
   COSTS_N_INSNS (3),			/*				 SI.  */
   COSTS_N_INSNS (4),			/*				 DI.  */
   COSTS_N_INSNS (4)},			/*			      other.  */
  0,					/* cost of multiply per each bit
					    set.  */
  {COSTS_N_INSNS (19),			/* cost of a divide/mod for QI.  */
   COSTS_N_INSNS (35),			/*			    HI.  */
   COSTS_N_INSNS (51),			/*			    SI.  */
   COSTS_N_INSNS (83),			/*			    DI.  */
   COSTS_N_INSNS (83)},			/*			    other.  */
  COSTS_N_INSNS (1),			/* cost of movsx.  */
  COSTS_N_INSNS (1),			/* cost of movzx.  */
  8,					/* "large" insn.  */
  9,					/* MOVE_RATIO.  */
  4,					/* cost for loading QImode using
					   movzbl.  */
  {5, 5, 4},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {4, 4, 4},				/* cost of storing integer
					   registers.  */
  2,					/* cost of reg,reg fld/fst.  */
  {5, 5, 12},				/* cost of loading fp registers
		   			   in SFmode, DFmode and XFmode.  */
  {4, 4, 8},				/* cost of storing fp registers
 		   			   in SFmode, DFmode and XFmode.  */
  2,					/* cost of moving MMX register.  */
  {4, 4},				/* cost of loading MMX registers
					   in SImode and DImode.  */
  {4, 4},				/* cost of storing MMX registers
					   in SImode and DImode.  */
  2,					/* cost of moving SSE register.  */
  {4, 4, 4},				/* cost of loading SSE registers
					   in SImode, DImode and TImode.  */
  {4, 4, 4},				/* cost of storing SSE registers
					   in SImode, DImode and TImode.  */
  2,					/* MMX or SSE register to integer.  */
  32,					/* size of l1 cache.  */
  512,					/* size of l2 cache.  */
  64,					/* size of prefetch block.  */
  /* New AMD processors never drop prefetches; if they cannot be performed
     immediately, they are queued.  We set number of simultaneous prefetches
     to a large constant to reflect this (it probably is not a good idea not
     to limit number of prefetches at all, as their execution also takes some
     time).  */
  100,					/* number of parallel prefetches.  */
  2,					/* Branch cost.  */
  COSTS_N_INSNS (6),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (6),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (42),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (2),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (2),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (52),			/* cost of FSQRT instruction.  */

  znver1_memcpy,
  znver1_memset,
  6,					/* scalar_stmt_cost.  */
  4,					/* scalar load_cost.  */
  4,					/* scalar_store_cost.  */
  6,					/* vec_stmt_cost.  */
  0,					/* vec_to_scalar_cost.  */
  2,					/* scalar_to_vec_cost.  */
  4,					/* vec_align_load_cost.  */
  4,					/* vec_unalign_load_cost.  */
  4,					/* vec_store_cost.  */
  4,					/* cond_taken_branch_cost.  */
  2,					/* cond_not_taken_branch_cost.  */
};

  /* BTVER1 has optimized REP instruction for medium sized blocks, but for
     very small blocks it is better to use loop. For large blocks, libcall can
     do nontemporary accesses and beat inline considerably.  */
static stringop_algs btver1_memcpy[2] = {
  {libcall, {{6, loop, false}, {14, unrolled_loop, false},
             {-1, rep_prefix_4_byte, false}}},
  {libcall, {{16, loop, false}, {8192, rep_prefix_8_byte, false},
             {-1, libcall, false}}}};
static stringop_algs btver1_memset[2] = {
  {libcall, {{8, loop, false}, {24, unrolled_loop, false},
             {2048, rep_prefix_4_byte, false}, {-1, libcall, false}}},
  {libcall, {{48, unrolled_loop, false}, {8192, rep_prefix_8_byte, false},
             {-1, libcall, false}}}};
const struct processor_costs btver1_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (2),			/* cost of a lea instruction */
  COSTS_N_INSNS (1),			/* variable shift costs */
  COSTS_N_INSNS (1),			/* constant shift costs */
  {COSTS_N_INSNS (3),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (4),			/*				 HI */
   COSTS_N_INSNS (3),			/*				 SI */
   COSTS_N_INSNS (4),			/*				 DI */
   COSTS_N_INSNS (5)},			/*			      other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (19),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (35),			/*			    HI */
   COSTS_N_INSNS (51),			/*			    SI */
   COSTS_N_INSNS (83),			/*			    DI */
   COSTS_N_INSNS (83)},			/*			    other */
  COSTS_N_INSNS (1),			/* cost of movsx */
  COSTS_N_INSNS (1),			/* cost of movzx */
  8,					/* "large" insn */
  9,					/* MOVE_RATIO */
  4,				     /* cost for loading QImode using movzbl */
  {3, 4, 3},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {3, 4, 3},				/* cost of storing integer registers */
  4,					/* cost of reg,reg fld/fst */
  {4, 4, 12},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {6, 6, 8},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  2,					/* cost of moving MMX register */
  {3, 3},				/* cost of loading MMX registers
					   in SImode and DImode */
  {4, 4},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {4, 4, 3},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {4, 4, 5},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  3,					/* MMX or SSE register to integer */
					/* On K8:
					   MOVD reg64, xmmreg Double FSTORE 4
					   MOVD reg32, xmmreg Double FSTORE 4
					   On AMDFAM10:
					   MOVD reg64, xmmreg Double FADD 3
							       1/1  1/1
					    MOVD reg32, xmmreg Double FADD 3
							       1/1  1/1 */
  32,					/* size of l1 cache.  */
  512,					/* size of l2 cache.  */
  64,					/* size of prefetch block */
  100,					/* number of parallel prefetches */
  2,					/* Branch cost */
  COSTS_N_INSNS (4),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (4),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (19),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (2),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (2),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (35),			/* cost of FSQRT instruction.  */

  btver1_memcpy,
  btver1_memset,
  4,					/* scalar_stmt_cost.  */
  2,					/* scalar load_cost.  */
  2,					/* scalar_store_cost.  */
  6,					/* vec_stmt_cost.  */
  0,					/* vec_to_scalar_cost.  */
  2,					/* scalar_to_vec_cost.  */
  2,					/* vec_align_load_cost.  */
  2,					/* vec_unalign_load_cost.  */
  2,					/* vec_store_cost.  */
  2,					/* cond_taken_branch_cost.  */
  1,					/* cond_not_taken_branch_cost.  */
};

static stringop_algs btver2_memcpy[2] = {
  {libcall, {{6, loop, false}, {14, unrolled_loop, false},
             {-1, rep_prefix_4_byte, false}}},
  {libcall, {{16, loop, false}, {8192, rep_prefix_8_byte, false},
             {-1, libcall, false}}}};
static stringop_algs btver2_memset[2] = {
  {libcall, {{8, loop, false}, {24, unrolled_loop, false},
             {2048, rep_prefix_4_byte, false}, {-1, libcall, false}}},
  {libcall, {{48, unrolled_loop, false}, {8192, rep_prefix_8_byte, false},
             {-1, libcall, false}}}};
const struct processor_costs btver2_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (2),			/* cost of a lea instruction */
  COSTS_N_INSNS (1),			/* variable shift costs */
  COSTS_N_INSNS (1),			/* constant shift costs */
  {COSTS_N_INSNS (3),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (4),			/*				 HI */
   COSTS_N_INSNS (3),			/*				 SI */
   COSTS_N_INSNS (4),			/*				 DI */
   COSTS_N_INSNS (5)},			/*			      other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (19),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (35),			/*			    HI */
   COSTS_N_INSNS (51),			/*			    SI */
   COSTS_N_INSNS (83),			/*			    DI */
   COSTS_N_INSNS (83)},			/*			    other */
  COSTS_N_INSNS (1),			/* cost of movsx */
  COSTS_N_INSNS (1),			/* cost of movzx */
  8,					/* "large" insn */
  9,					/* MOVE_RATIO */
  4,				     /* cost for loading QImode using movzbl */
  {3, 4, 3},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {3, 4, 3},				/* cost of storing integer registers */
  4,					/* cost of reg,reg fld/fst */
  {4, 4, 12},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {6, 6, 8},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  2,					/* cost of moving MMX register */
  {3, 3},				/* cost of loading MMX registers
					   in SImode and DImode */
  {4, 4},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {4, 4, 3},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {4, 4, 5},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  3,					/* MMX or SSE register to integer */
					/* On K8:
					   MOVD reg64, xmmreg Double FSTORE 4
					   MOVD reg32, xmmreg Double FSTORE 4
					   On AMDFAM10:
					   MOVD reg64, xmmreg Double FADD 3
							       1/1  1/1
					    MOVD reg32, xmmreg Double FADD 3
							       1/1  1/1 */
  32,					/* size of l1 cache.  */
  2048,					/* size of l2 cache.  */
  64,					/* size of prefetch block */
  100,					/* number of parallel prefetches */
  2,					/* Branch cost */
  COSTS_N_INSNS (4),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (4),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (19),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (2),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (2),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (35),			/* cost of FSQRT instruction.  */
  btver2_memcpy,
  btver2_memset,
  4,					/* scalar_stmt_cost.  */
  2,					/* scalar load_cost.  */
  2,					/* scalar_store_cost.  */
  6,					/* vec_stmt_cost.  */
  0,					/* vec_to_scalar_cost.  */
  2,					/* scalar_to_vec_cost.  */
  2,					/* vec_align_load_cost.  */
  2,					/* vec_unalign_load_cost.  */
  2,					/* vec_store_cost.  */
  2,					/* cond_taken_branch_cost.  */
  1,					/* cond_not_taken_branch_cost.  */
};

static stringop_algs pentium4_memcpy[2] = {
  {libcall, {{12, loop_1_byte, false}, {-1, rep_prefix_4_byte, false}}},
  DUMMY_STRINGOP_ALGS};
static stringop_algs pentium4_memset[2] = {
  {libcall, {{6, loop_1_byte, false}, {48, loop, false},
             {20480, rep_prefix_4_byte, false}, {-1, libcall, false}}},
  DUMMY_STRINGOP_ALGS};

static const
struct processor_costs pentium4_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (3),			/* cost of a lea instruction */
  COSTS_N_INSNS (4),			/* variable shift costs */
  COSTS_N_INSNS (4),			/* constant shift costs */
  {COSTS_N_INSNS (15),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (15),			/*				 HI */
   COSTS_N_INSNS (15),			/*				 SI */
   COSTS_N_INSNS (15),			/*				 DI */
   COSTS_N_INSNS (15)},			/*			      other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (56),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (56),			/*			    HI */
   COSTS_N_INSNS (56),			/*			    SI */
   COSTS_N_INSNS (56),			/*			    DI */
   COSTS_N_INSNS (56)},			/*			    other */
  COSTS_N_INSNS (1),			/* cost of movsx */
  COSTS_N_INSNS (1),			/* cost of movzx */
  16,					/* "large" insn */
  6,					/* MOVE_RATIO */
  2,				     /* cost for loading QImode using movzbl */
  {4, 5, 4},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {2, 3, 2},				/* cost of storing integer registers */
  2,					/* cost of reg,reg fld/fst */
  {2, 2, 6},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {4, 4, 6},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  2,					/* cost of moving MMX register */
  {2, 2},				/* cost of loading MMX registers
					   in SImode and DImode */
  {2, 2},				/* cost of storing MMX registers
					   in SImode and DImode */
  12,					/* cost of moving SSE register */
  {12, 12, 12},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {2, 2, 8},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  10,					/* MMX or SSE register to integer */
  8,					/* size of l1 cache.  */
  256,					/* size of l2 cache.  */
  64,					/* size of prefetch block */
  6,					/* number of parallel prefetches */
  2,					/* Branch cost */
  COSTS_N_INSNS (5),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (7),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (43),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (2),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (2),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (43),			/* cost of FSQRT instruction.  */
  pentium4_memcpy,
  pentium4_memset,
  1,					/* scalar_stmt_cost.  */
  1,					/* scalar load_cost.  */
  1,					/* scalar_store_cost.  */
  1,					/* vec_stmt_cost.  */
  1,					/* vec_to_scalar_cost.  */
  1,					/* scalar_to_vec_cost.  */
  1,					/* vec_align_load_cost.  */
  2,					/* vec_unalign_load_cost.  */
  1,					/* vec_store_cost.  */
  3,					/* cond_taken_branch_cost.  */
  1,					/* cond_not_taken_branch_cost.  */
};

static stringop_algs nocona_memcpy[2] = {
  {libcall, {{12, loop_1_byte, false}, {-1, rep_prefix_4_byte, false}}},
  {libcall, {{32, loop, false}, {20000, rep_prefix_8_byte, false},
             {100000, unrolled_loop, false}, {-1, libcall, false}}}};

static stringop_algs nocona_memset[2] = {
  {libcall, {{6, loop_1_byte, false}, {48, loop, false},
             {20480, rep_prefix_4_byte, false}, {-1, libcall, false}}},
  {libcall, {{24, loop, false}, {64, unrolled_loop, false},
             {8192, rep_prefix_8_byte, false}, {-1, libcall, false}}}};

static const
struct processor_costs nocona_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (1),			/* cost of a lea instruction */
  COSTS_N_INSNS (1),			/* variable shift costs */
  COSTS_N_INSNS (1),			/* constant shift costs */
  {COSTS_N_INSNS (10),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (10),			/*				 HI */
   COSTS_N_INSNS (10),			/*				 SI */
   COSTS_N_INSNS (10),			/*				 DI */
   COSTS_N_INSNS (10)},			/*			      other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (66),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (66),			/*			    HI */
   COSTS_N_INSNS (66),			/*			    SI */
   COSTS_N_INSNS (66),			/*			    DI */
   COSTS_N_INSNS (66)},			/*			    other */
  COSTS_N_INSNS (1),			/* cost of movsx */
  COSTS_N_INSNS (1),			/* cost of movzx */
  16,					/* "large" insn */
  17,					/* MOVE_RATIO */
  4,				     /* cost for loading QImode using movzbl */
  {4, 4, 4},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {4, 4, 4},				/* cost of storing integer registers */
  3,					/* cost of reg,reg fld/fst */
  {12, 12, 12},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {4, 4, 4},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  6,					/* cost of moving MMX register */
  {12, 12},				/* cost of loading MMX registers
					   in SImode and DImode */
  {12, 12},				/* cost of storing MMX registers
					   in SImode and DImode */
  6,					/* cost of moving SSE register */
  {12, 12, 12},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {12, 12, 12},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  8,					/* MMX or SSE register to integer */
  8,					/* size of l1 cache.  */
  1024,					/* size of l2 cache.  */
  64,					/* size of prefetch block */
  8,					/* number of parallel prefetches */
  1,					/* Branch cost */
  COSTS_N_INSNS (6),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (8),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (40),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (3),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (3),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (44),			/* cost of FSQRT instruction.  */
  nocona_memcpy,
  nocona_memset,
  1,					/* scalar_stmt_cost.  */
  1,					/* scalar load_cost.  */
  1,					/* scalar_store_cost.  */
  1,					/* vec_stmt_cost.  */
  1,					/* vec_to_scalar_cost.  */
  1,					/* scalar_to_vec_cost.  */
  1,					/* vec_align_load_cost.  */
  2,					/* vec_unalign_load_cost.  */
  1,					/* vec_store_cost.  */
  3,					/* cond_taken_branch_cost.  */
  1,					/* cond_not_taken_branch_cost.  */
};

static stringop_algs atom_memcpy[2] = {
  {libcall, {{11, loop, false}, {-1, rep_prefix_4_byte, false}}},
  {libcall, {{32, loop, false}, {64, rep_prefix_4_byte, false},
             {8192, rep_prefix_8_byte, false}, {-1, libcall, false}}}};
static stringop_algs atom_memset[2] = {
  {libcall, {{8, loop, false}, {15, unrolled_loop, false},
             {2048, rep_prefix_4_byte, false}, {-1, libcall, false}}},
  {libcall, {{24, loop, false}, {32, unrolled_loop, false},
             {8192, rep_prefix_8_byte, false}, {-1, libcall, false}}}};
static const
struct processor_costs atom_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (1) + 1,		/* cost of a lea instruction */
  COSTS_N_INSNS (1),			/* variable shift costs */
  COSTS_N_INSNS (1),			/* constant shift costs */
  {COSTS_N_INSNS (3),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (4),			/*				 HI */
   COSTS_N_INSNS (3),			/*				 SI */
   COSTS_N_INSNS (4),			/*				 DI */
   COSTS_N_INSNS (2)},			/*			      other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (18),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (26),			/*			    HI */
   COSTS_N_INSNS (42),			/*			    SI */
   COSTS_N_INSNS (74),			/*			    DI */
   COSTS_N_INSNS (74)},			/*			    other */
  COSTS_N_INSNS (1),			/* cost of movsx */
  COSTS_N_INSNS (1),			/* cost of movzx */
  8,					/* "large" insn */
  17,					/* MOVE_RATIO */
  4,					/* cost for loading QImode using movzbl */
  {4, 4, 4},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {4, 4, 4},				/* cost of storing integer registers */
  4,					/* cost of reg,reg fld/fst */
  {12, 12, 12},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {6, 6, 8},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  2,					/* cost of moving MMX register */
  {8, 8},				/* cost of loading MMX registers
					   in SImode and DImode */
  {8, 8},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {8, 8, 8},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {8, 8, 8},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  5,					/* MMX or SSE register to integer */
  32,					/* size of l1 cache.  */
  256,					/* size of l2 cache.  */
  64,					/* size of prefetch block */
  6,					/* number of parallel prefetches */
  3,					/* Branch cost */
  COSTS_N_INSNS (8),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (8),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (20),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (8),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (8),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (40),			/* cost of FSQRT instruction.  */
  atom_memcpy,
  atom_memset,
  1,					/* scalar_stmt_cost.  */
  1,					/* scalar load_cost.  */
  1,					/* scalar_store_cost.  */
  1,					/* vec_stmt_cost.  */
  1,					/* vec_to_scalar_cost.  */
  1,					/* scalar_to_vec_cost.  */
  1,					/* vec_align_load_cost.  */
  2,					/* vec_unalign_load_cost.  */
  1,					/* vec_store_cost.  */
  3,					/* cond_taken_branch_cost.  */
  1,					/* cond_not_taken_branch_cost.  */
};

static stringop_algs slm_memcpy[2] = {
  {libcall, {{11, loop, false}, {-1, rep_prefix_4_byte, false}}},
  {libcall, {{32, loop, false}, {64, rep_prefix_4_byte, false},
             {8192, rep_prefix_8_byte, false}, {-1, libcall, false}}}};
static stringop_algs slm_memset[2] = {
  {libcall, {{8, loop, false}, {15, unrolled_loop, false},
             {2048, rep_prefix_4_byte, false}, {-1, libcall, false}}},
  {libcall, {{24, loop, false}, {32, unrolled_loop, false},
             {8192, rep_prefix_8_byte, false}, {-1, libcall, false}}}};
static const
struct processor_costs slm_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (1) + 1,		/* cost of a lea instruction */
  COSTS_N_INSNS (1),			/* variable shift costs */
  COSTS_N_INSNS (1),			/* constant shift costs */
  {COSTS_N_INSNS (3),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (3),			/*				 HI */
   COSTS_N_INSNS (3),			/*				 SI */
   COSTS_N_INSNS (4),			/*				 DI */
   COSTS_N_INSNS (2)},			/*			      other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (18),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (26),			/*			    HI */
   COSTS_N_INSNS (42),			/*			    SI */
   COSTS_N_INSNS (74),			/*			    DI */
   COSTS_N_INSNS (74)},			/*			    other */
  COSTS_N_INSNS (1),			/* cost of movsx */
  COSTS_N_INSNS (1),			/* cost of movzx */
  8,					/* "large" insn */
  17,					/* MOVE_RATIO */
  4,					/* cost for loading QImode using movzbl */
  {4, 4, 4},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {4, 4, 4},				/* cost of storing integer registers */
  4,					/* cost of reg,reg fld/fst */
  {12, 12, 12},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {6, 6, 8},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  2,					/* cost of moving MMX register */
  {8, 8},				/* cost of loading MMX registers
					   in SImode and DImode */
  {8, 8},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {8, 8, 8},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {8, 8, 8},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  5,					/* MMX or SSE register to integer */
  32,					/* size of l1 cache.  */
  256,					/* size of l2 cache.  */
  64,					/* size of prefetch block */
  6,					/* number of parallel prefetches */
  3,					/* Branch cost */
  COSTS_N_INSNS (8),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (8),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (20),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (8),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (8),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (40),			/* cost of FSQRT instruction.  */
  slm_memcpy,
  slm_memset,
  1,					/* scalar_stmt_cost.  */
  1,					/* scalar load_cost.  */
  1,					/* scalar_store_cost.  */
  1,					/* vec_stmt_cost.  */
  4,					/* vec_to_scalar_cost.  */
  1,					/* scalar_to_vec_cost.  */
  1,					/* vec_align_load_cost.  */
  2,					/* vec_unalign_load_cost.  */
  1,					/* vec_store_cost.  */
  3,					/* cond_taken_branch_cost.  */
  1,					/* cond_not_taken_branch_cost.  */
};

static stringop_algs intel_memcpy[2] = {
  {libcall, {{11, loop, false}, {-1, rep_prefix_4_byte, false}}},
  {libcall, {{32, loop, false}, {64, rep_prefix_4_byte, false},
             {8192, rep_prefix_8_byte, false}, {-1, libcall, false}}}};
static stringop_algs intel_memset[2] = {
  {libcall, {{8, loop, false}, {15, unrolled_loop, false},
             {2048, rep_prefix_4_byte, false}, {-1, libcall, false}}},
  {libcall, {{24, loop, false}, {32, unrolled_loop, false},
             {8192, rep_prefix_8_byte, false}, {-1, libcall, false}}}};
static const
struct processor_costs intel_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (1) + 1,		/* cost of a lea instruction */
  COSTS_N_INSNS (1),			/* variable shift costs */
  COSTS_N_INSNS (1),			/* constant shift costs */
  {COSTS_N_INSNS (3),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (3),			/*				 HI */
   COSTS_N_INSNS (3),			/*				 SI */
   COSTS_N_INSNS (4),			/*				 DI */
   COSTS_N_INSNS (2)},			/*			      other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (18),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (26),			/*			    HI */
   COSTS_N_INSNS (42),			/*			    SI */
   COSTS_N_INSNS (74),			/*			    DI */
   COSTS_N_INSNS (74)},			/*			    other */
  COSTS_N_INSNS (1),			/* cost of movsx */
  COSTS_N_INSNS (1),			/* cost of movzx */
  8,					/* "large" insn */
  17,					/* MOVE_RATIO */
  4,					/* cost for loading QImode using movzbl */
  {4, 4, 4},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {4, 4, 4},				/* cost of storing integer registers */
  4,					/* cost of reg,reg fld/fst */
  {12, 12, 12},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {6, 6, 8},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  2,					/* cost of moving MMX register */
  {8, 8},				/* cost of loading MMX registers
					   in SImode and DImode */
  {8, 8},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {8, 8, 8},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {8, 8, 8},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  5,					/* MMX or SSE register to integer */
  32,					/* size of l1 cache.  */
  256,					/* size of l2 cache.  */
  64,					/* size of prefetch block */
  6,					/* number of parallel prefetches */
  3,					/* Branch cost */
  COSTS_N_INSNS (8),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (8),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (20),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (8),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (8),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (40),			/* cost of FSQRT instruction.  */
  intel_memcpy,
  intel_memset,
  1,					/* scalar_stmt_cost.  */
  1,					/* scalar load_cost.  */
  1,					/* scalar_store_cost.  */
  1,					/* vec_stmt_cost.  */
  4,					/* vec_to_scalar_cost.  */
  1,					/* scalar_to_vec_cost.  */
  1,					/* vec_align_load_cost.  */
  2,					/* vec_unalign_load_cost.  */
  1,					/* vec_store_cost.  */
  3,					/* cond_taken_branch_cost.  */
  1,					/* cond_not_taken_branch_cost.  */
};

/* Generic should produce code tuned for Core-i7 (and newer chips)
   and btver1 (and newer chips).  */

static stringop_algs generic_memcpy[2] = {
  {libcall, {{32, loop, false}, {8192, rep_prefix_4_byte, false},
             {-1, libcall, false}}},
  {libcall, {{32, loop, false}, {8192, rep_prefix_8_byte, false},
             {-1, libcall, false}}}};
static stringop_algs generic_memset[2] = {
  {libcall, {{32, loop, false}, {8192, rep_prefix_4_byte, false},
             {-1, libcall, false}}},
  {libcall, {{32, loop, false}, {8192, rep_prefix_8_byte, false},
             {-1, libcall, false}}}};
static const
struct processor_costs generic_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  /* On all chips taken into consideration lea is 2 cycles and more.  With
     this cost however our current implementation of synth_mult results in
     use of unnecessary temporary registers causing regression on several
     SPECfp benchmarks.  */
  COSTS_N_INSNS (1) + 1,		/* cost of a lea instruction */
  COSTS_N_INSNS (1),			/* variable shift costs */
  COSTS_N_INSNS (1),			/* constant shift costs */
  {COSTS_N_INSNS (3),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (4),			/*				 HI */
   COSTS_N_INSNS (3),			/*				 SI */
   COSTS_N_INSNS (4),			/*				 DI */
   COSTS_N_INSNS (2)},			/*			      other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (18),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (26),			/*			    HI */
   COSTS_N_INSNS (42),			/*			    SI */
   COSTS_N_INSNS (74),			/*			    DI */
   COSTS_N_INSNS (74)},			/*			    other */
  COSTS_N_INSNS (1),			/* cost of movsx */
  COSTS_N_INSNS (1),			/* cost of movzx */
  8,					/* "large" insn */
  17,					/* MOVE_RATIO */
  4,				     /* cost for loading QImode using movzbl */
  {4, 4, 4},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {4, 4, 4},				/* cost of storing integer registers */
  4,					/* cost of reg,reg fld/fst */
  {12, 12, 12},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {6, 6, 8},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  2,					/* cost of moving MMX register */
  {8, 8},				/* cost of loading MMX registers
					   in SImode and DImode */
  {8, 8},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {8, 8, 8},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {8, 8, 8},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  5,					/* MMX or SSE register to integer */
  32,					/* size of l1 cache.  */
  512,					/* size of l2 cache.  */
  64,					/* size of prefetch block */
  6,					/* number of parallel prefetches */
  /* Benchmarks shows large regressions on K8 sixtrack benchmark when this
     value is increased to perhaps more appropriate value of 5.  */
  3,					/* Branch cost */
  COSTS_N_INSNS (8),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (8),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (20),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (8),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (8),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (40),			/* cost of FSQRT instruction.  */
  generic_memcpy,
  generic_memset,
  1,					/* scalar_stmt_cost.  */
  1,					/* scalar load_cost.  */
  1,					/* scalar_store_cost.  */
  1,					/* vec_stmt_cost.  */
  1,					/* vec_to_scalar_cost.  */
  1,					/* scalar_to_vec_cost.  */
  1,					/* vec_align_load_cost.  */
  2,					/* vec_unalign_load_cost.  */
  1,					/* vec_store_cost.  */
  3,					/* cond_taken_branch_cost.  */
  1,					/* cond_not_taken_branch_cost.  */
};

/* core_cost should produce code tuned for Core familly of CPUs.  */
static stringop_algs core_memcpy[2] = {
  {libcall, {{1024, rep_prefix_4_byte, true}, {-1, libcall, false}}},
  {libcall, {{24, loop, true}, {128, rep_prefix_8_byte, true},
             {-1, libcall, false}}}};
static stringop_algs core_memset[2] = {
  {libcall, {{6, loop_1_byte, true},
             {24, loop, true},
             {8192, rep_prefix_4_byte, true},
             {-1, libcall, false}}},
  {libcall, {{24, loop, true}, {512, rep_prefix_8_byte, true},
             {-1, libcall, false}}}};

static const
struct processor_costs core_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  /* On all chips taken into consideration lea is 2 cycles and more.  With
     this cost however our current implementation of synth_mult results in
     use of unnecessary temporary registers causing regression on several
     SPECfp benchmarks.  */
  COSTS_N_INSNS (1) + 1,		/* cost of a lea instruction */
  COSTS_N_INSNS (1),			/* variable shift costs */
  COSTS_N_INSNS (1),			/* constant shift costs */
  {COSTS_N_INSNS (3),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (4),			/*				 HI */
   COSTS_N_INSNS (3),			/*				 SI */
   COSTS_N_INSNS (4),			/*				 DI */
   COSTS_N_INSNS (2)},			/*			      other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (18),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (26),			/*			    HI */
   COSTS_N_INSNS (42),			/*			    SI */
   COSTS_N_INSNS (74),			/*			    DI */
   COSTS_N_INSNS (74)},			/*			    other */
  COSTS_N_INSNS (1),			/* cost of movsx */
  COSTS_N_INSNS (1),			/* cost of movzx */
  8,					/* "large" insn */
  17,					/* MOVE_RATIO */
  4,				     /* cost for loading QImode using movzbl */
  {4, 4, 4},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {4, 4, 4},				/* cost of storing integer registers */
  4,					/* cost of reg,reg fld/fst */
  {12, 12, 12},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {6, 6, 8},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  2,					/* cost of moving MMX register */
  {8, 8},				/* cost of loading MMX registers
					   in SImode and DImode */
  {8, 8},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {8, 8, 8},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {8, 8, 8},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  5,					/* MMX or SSE register to integer */
  64,					/* size of l1 cache.  */
  512,					/* size of l2 cache.  */
  64,					/* size of prefetch block */
  6,					/* number of parallel prefetches */
  /* FIXME perhaps more appropriate value is 5.  */
  3,					/* Branch cost */
  COSTS_N_INSNS (8),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (8),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (20),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (8),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (8),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (40),			/* cost of FSQRT instruction.  */
  core_memcpy,
  core_memset,
  1,					/* scalar_stmt_cost.  */
  1,					/* scalar load_cost.  */
  1,					/* scalar_store_cost.  */
  1,					/* vec_stmt_cost.  */
  1,					/* vec_to_scalar_cost.  */
  1,					/* scalar_to_vec_cost.  */
  1,					/* vec_align_load_cost.  */
  2,					/* vec_unalign_load_cost.  */
  1,					/* vec_store_cost.  */
  3,					/* cond_taken_branch_cost.  */
  1,					/* cond_not_taken_branch_cost.  */
};


/* Set by -mtune.  */
const struct processor_costs *ix86_tune_cost = &pentium_cost;

/* Set by -mtune or -Os.  */
const struct processor_costs *ix86_cost = &pentium_cost;

/* Processor feature/optimization bitmasks.  */
#define m_386 (1<<PROCESSOR_I386)
#define m_486 (1<<PROCESSOR_I486)
#define m_PENT (1<<PROCESSOR_PENTIUM)
#define m_LAKEMONT (1<<PROCESSOR_LAKEMONT)
#define m_PPRO (1<<PROCESSOR_PENTIUMPRO)
#define m_PENT4 (1<<PROCESSOR_PENTIUM4)
#define m_NOCONA (1<<PROCESSOR_NOCONA)
#define m_P4_NOCONA (m_PENT4 | m_NOCONA)
#define m_CORE2 (1<<PROCESSOR_CORE2)
#define m_NEHALEM (1<<PROCESSOR_NEHALEM)
#define m_SANDYBRIDGE (1<<PROCESSOR_SANDYBRIDGE)
#define m_HASWELL (1<<PROCESSOR_HASWELL)
#define m_CORE_ALL (m_CORE2 | m_NEHALEM  | m_SANDYBRIDGE | m_HASWELL)
#define m_BONNELL (1<<PROCESSOR_BONNELL)
#define m_SILVERMONT (1<<PROCESSOR_SILVERMONT)
#define m_KNL (1<<PROCESSOR_KNL)
#define m_SKYLAKE_AVX512 (1<<PROCESSOR_SKYLAKE_AVX512)
#define m_INTEL (1<<PROCESSOR_INTEL)

#define m_GEODE (1<<PROCESSOR_GEODE)
#define m_K6 (1<<PROCESSOR_K6)
#define m_K6_GEODE (m_K6 | m_GEODE)
#define m_K8 (1<<PROCESSOR_K8)
#define m_ATHLON (1<<PROCESSOR_ATHLON)
#define m_ATHLON_K8 (m_K8 | m_ATHLON)
#define m_AMDFAM10 (1<<PROCESSOR_AMDFAM10)
#define m_BDVER1 (1<<PROCESSOR_BDVER1)
#define m_BDVER2 (1<<PROCESSOR_BDVER2)
#define m_BDVER3 (1<<PROCESSOR_BDVER3)
#define m_BDVER4 (1<<PROCESSOR_BDVER4)
#define m_ZNVER1 (1<<PROCESSOR_ZNVER1)
#define m_BTVER1 (1<<PROCESSOR_BTVER1)
#define m_BTVER2 (1<<PROCESSOR_BTVER2)
#define m_BDVER	(m_BDVER1 | m_BDVER2 | m_BDVER3 | m_BDVER4)
#define m_BTVER (m_BTVER1 | m_BTVER2)
#define m_AMD_MULTIPLE (m_ATHLON_K8 | m_AMDFAM10 | m_BDVER | m_BTVER \
			| m_ZNVER1)

#define m_GENERIC (1<<PROCESSOR_GENERIC)

const char* ix86_tune_feature_names[X86_TUNE_LAST] = {
#undef DEF_TUNE
#define DEF_TUNE(tune, name, selector) name,
#undef DEF_TUNE
};

/* Feature tests against the various tunings.  */
unsigned char ix86_tune_features[X86_TUNE_LAST];

/* Feature tests against the various architecture variations.  */
unsigned char ix86_arch_features[X86_ARCH_LAST];

/* In case the average insn count for single function invocation is
   lower than this constant, emit fast (but longer) prologue and
   epilogue code.  */
#define FAST_PROLOGUE_INSN_COUNT 20

/* Names for 8 (low), 8 (high), and 16-bit registers, respectively.  */
static const char *const qi_reg_name[] = QI_REGISTER_NAMES;
static const char *const qi_high_reg_name[] = QI_HIGH_REGISTER_NAMES;
static const char *const hi_reg_name[] = HI_REGISTER_NAMES;

/* Array of the smallest class containing reg number REGNO, indexed by
   REGNO.  Used by REGNO_REG_CLASS in i386.h.  */

enum reg_class const regclass_map[FIRST_PSEUDO_REGISTER] =
{
  /* ax, dx, cx, bx */
  AREG, DREG, CREG, BREG,
  /* si, di, bp, sp */
  SIREG, DIREG, NON_Q_REGS, NON_Q_REGS,
  /* FP registers */
  FP_TOP_REG, FP_SECOND_REG, FLOAT_REGS, FLOAT_REGS,
  FLOAT_REGS, FLOAT_REGS, FLOAT_REGS, FLOAT_REGS,
  /* arg pointer */
  NON_Q_REGS,
  /* flags, fpsr, fpcr, frame */
  NO_REGS, NO_REGS, NO_REGS, NON_Q_REGS,
  /* SSE registers */
  SSE_FIRST_REG, SSE_REGS, SSE_REGS, SSE_REGS, SSE_REGS, SSE_REGS,
  SSE_REGS, SSE_REGS,
  /* MMX registers */
  MMX_REGS, MMX_REGS, MMX_REGS, MMX_REGS, MMX_REGS, MMX_REGS,
  MMX_REGS, MMX_REGS,
  /* REX registers */
  NON_Q_REGS, NON_Q_REGS, NON_Q_REGS, NON_Q_REGS,
  NON_Q_REGS, NON_Q_REGS, NON_Q_REGS, NON_Q_REGS,
  /* SSE REX registers */
  SSE_REGS, SSE_REGS, SSE_REGS, SSE_REGS, SSE_REGS, SSE_REGS,
  SSE_REGS, SSE_REGS,
  /* AVX-512 SSE registers */
  EVEX_SSE_REGS, EVEX_SSE_REGS, EVEX_SSE_REGS, EVEX_SSE_REGS,
  EVEX_SSE_REGS, EVEX_SSE_REGS, EVEX_SSE_REGS, EVEX_SSE_REGS,
  EVEX_SSE_REGS, EVEX_SSE_REGS, EVEX_SSE_REGS, EVEX_SSE_REGS,
  EVEX_SSE_REGS, EVEX_SSE_REGS, EVEX_SSE_REGS, EVEX_SSE_REGS,
  /* Mask registers.  */
  MASK_REGS, MASK_EVEX_REGS, MASK_EVEX_REGS, MASK_EVEX_REGS,
  MASK_EVEX_REGS, MASK_EVEX_REGS, MASK_EVEX_REGS, MASK_EVEX_REGS,
  /* MPX bound registers */
  BND_REGS, BND_REGS, BND_REGS, BND_REGS,
};

/* The "default" register map used in 32bit mode.  */

int const dbx_register_map[FIRST_PSEUDO_REGISTER] =
{
  0, 2, 1, 3, 6, 7, 4, 5,		/* general regs */
  12, 13, 14, 15, 16, 17, 18, 19,	/* fp regs */
  -1, -1, -1, -1, -1,			/* arg, flags, fpsr, fpcr, frame */
  21, 22, 23, 24, 25, 26, 27, 28,	/* SSE */
  29, 30, 31, 32, 33, 34, 35, 36,       /* MMX */
  -1, -1, -1, -1, -1, -1, -1, -1,	/* extended integer registers */
  -1, -1, -1, -1, -1, -1, -1, -1,	/* extended SSE registers */
  -1, -1, -1, -1, -1, -1, -1, -1,       /* AVX-512 registers 16-23*/
  -1, -1, -1, -1, -1, -1, -1, -1,       /* AVX-512 registers 24-31*/
  93, 94, 95, 96, 97, 98, 99, 100,      /* Mask registers */
  101, 102, 103, 104,			/* bound registers */
};

/* The "default" register map used in 64bit mode.  */

int const dbx64_register_map[FIRST_PSEUDO_REGISTER] =
{
  0, 1, 2, 3, 4, 5, 6, 7,		/* general regs */
  33, 34, 35, 36, 37, 38, 39, 40,	/* fp regs */
  -1, -1, -1, -1, -1,			/* arg, flags, fpsr, fpcr, frame */
  17, 18, 19, 20, 21, 22, 23, 24,	/* SSE */
  41, 42, 43, 44, 45, 46, 47, 48,       /* MMX */
  8,9,10,11,12,13,14,15,		/* extended integer registers */
  25, 26, 27, 28, 29, 30, 31, 32,	/* extended SSE registers */
  67, 68, 69, 70, 71, 72, 73, 74,       /* AVX-512 registers 16-23 */
  75, 76, 77, 78, 79, 80, 81, 82,       /* AVX-512 registers 24-31 */
  118, 119, 120, 121, 122, 123, 124, 125, /* Mask registers */
  126, 127, 128, 129,			/* bound registers */
};

/* Define the register numbers to be used in Dwarf debugging information.
   The SVR4 reference port C compiler uses the following register numbers
   in its Dwarf output code:
	0 for %eax (gcc regno = 0)
	1 for %ecx (gcc regno = 2)
	2 for %edx (gcc regno = 1)
	3 for %ebx (gcc regno = 3)
	4 for %esp (gcc regno = 7)
	5 for %ebp (gcc regno = 6)
	6 for %esi (gcc regno = 4)
	7 for %edi (gcc regno = 5)
   The following three DWARF register numbers are never generated by
   the SVR4 C compiler or by the GNU compilers, but SDB on x86/svr4
   believes these numbers have these meanings.
	8  for %eip    (no gcc equivalent)
	9  for %eflags (gcc regno = 17)
	10 for %trapno (no gcc equivalent)
   It is not at all clear how we should number the FP stack registers
   for the x86 architecture.  If the version of SDB on x86/svr4 were
   a bit less brain dead with respect to floating-point then we would
   have a precedent to follow with respect to DWARF register numbers
   for x86 FP registers, but the SDB on x86/svr4 is so completely
   broken with respect to FP registers that it is hardly worth thinking
   of it as something to strive for compatibility with.
   The version of x86/svr4 SDB I have at the moment does (partially)
   seem to believe that DWARF register number 11 is associated with
   the x86 register %st(0), but that's about all.  Higher DWARF
   register numbers don't seem to be associated with anything in
   particular, and even for DWARF regno 11, SDB only seems to under-
   stand that it should say that a variable lives in %st(0) (when
   asked via an `=' command) if we said it was in DWARF regno 11,
   but SDB still prints garbage when asked for the value of the
   variable in question (via a `/' command).
   (Also note that the labels SDB prints for various FP stack regs
   when doing an `x' command are all wrong.)
   Note that these problems generally don't affect the native SVR4
   C compiler because it doesn't allow the use of -O with -g and
   because when it is *not* optimizing, it allocates a memory
   location for each floating-point variable, and the memory
   location is what gets described in the DWARF AT_location
   attribute for the variable in question.
   Regardless of the severe mental illness of the x86/svr4 SDB, we
   do something sensible here and we use the following DWARF
   register numbers.  Note that these are all stack-top-relative
   numbers.
	11 for %st(0) (gcc regno = 8)
	12 for %st(1) (gcc regno = 9)
	13 for %st(2) (gcc regno = 10)
	14 for %st(3) (gcc regno = 11)
	15 for %st(4) (gcc regno = 12)
	16 for %st(5) (gcc regno = 13)
	17 for %st(6) (gcc regno = 14)
	18 for %st(7) (gcc regno = 15)
*/
int const svr4_dbx_register_map[FIRST_PSEUDO_REGISTER] =
{
  0, 2, 1, 3, 6, 7, 5, 4,		/* general regs */
  11, 12, 13, 14, 15, 16, 17, 18,	/* fp regs */
  -1, 9, -1, -1, -1,			/* arg, flags, fpsr, fpcr, frame */
  21, 22, 23, 24, 25, 26, 27, 28,	/* SSE registers */
  29, 30, 31, 32, 33, 34, 35, 36,	/* MMX registers */
  -1, -1, -1, -1, -1, -1, -1, -1,	/* extended integer registers */
  -1, -1, -1, -1, -1, -1, -1, -1,	/* extended SSE registers */
  -1, -1, -1, -1, -1, -1, -1, -1,       /* AVX-512 registers 16-23*/
  -1, -1, -1, -1, -1, -1, -1, -1,       /* AVX-512 registers 24-31*/
  93, 94, 95, 96, 97, 98, 99, 100,      /* Mask registers */
  101, 102, 103, 104,			/* bound registers */
};

/* Define parameter passing and return registers.  */

static int const x86_64_int_parameter_registers[6] =
{
  DI_REG, SI_REG, DX_REG, CX_REG, R8_REG, R9_REG
};

static int const x86_64_ms_abi_int_parameter_registers[4] =
{
  CX_REG, DX_REG, R8_REG, R9_REG
};

static int const x86_64_int_return_registers[4] =
{
  AX_REG, DX_REG, DI_REG, SI_REG
};

/* Additional registers that are clobbered by SYSV calls.  */

int const x86_64_ms_sysv_extra_clobbered_registers[12] =
{
  SI_REG, DI_REG,
  XMM6_REG, XMM7_REG,
  XMM8_REG, XMM9_REG, XMM10_REG, XMM11_REG,
  XMM12_REG, XMM13_REG, XMM14_REG, XMM15_REG
};

/* Define the structure for the machine field in struct function.  */

struct GTY(()) stack_local_entry {
  unsigned short mode;
  unsigned short n;
  rtx rtl;
  struct stack_local_entry *next;
};

/* Structure describing stack frame layout.
   Stack grows downward:

   [arguments]
					<- ARG_POINTER
   saved pc

   saved static chain			if ix86_static_chain_on_stack

   saved frame pointer			if frame_pointer_needed
					<- HARD_FRAME_POINTER
   [saved regs]
					<- regs_save_offset
   [padding0]

   [saved SSE regs]
					<- sse_regs_save_offset
   [padding1]          |
		       |		<- FRAME_POINTER
   [va_arg registers]  |
		       |
   [frame]	       |
		       |
   [padding2]	       | = to_allocate
					<- STACK_POINTER
  */
#if (GCC_MAJOR < 8)
struct ix86_frame
{
  int nsseregs;
  int nregs;
  int va_arg_size;
  int red_zone_size;
  int outgoing_arguments_size;

  /* The offsets relative to ARG_POINTER.  */
  HOST_WIDE_INT frame_pointer_offset;
  HOST_WIDE_INT hard_frame_pointer_offset;
  HOST_WIDE_INT stack_pointer_offset;
  HOST_WIDE_INT hfp_save_offset;
  HOST_WIDE_INT reg_save_offset;
  HOST_WIDE_INT sse_reg_save_offset;

  /* When save_regs_using_mov is set, emit prologue using
     move instead of push instructions.  */
  bool save_regs_using_mov;
};
#endif

#define MAX_CLASSES 8

/* Return the "natural" mode for TYPE.  In most cases, this is just TYPE_MODE.
   But in the case of vector types, it is some vector mode.

   When we have only some of our vector isa extensions enabled, then there
   are some modes for which vector_mode_supported_p is false.  For these
   modes, the generic vector support in gcc will choose some non-vector mode
   in order to implement the type.  By computing the natural mode, we'll
   select the proper ABI location for the operand and not depend on whatever
   the middle-end decides to do with these vector types.

   The midde-end can't deal with the vector types > 16 bytes.  In this
   case, we return the original mode and warn ABI change if CUM isn't
   NULL. 

   If INT_RETURN is true, warn ABI change if the vector mode isn't
   available for function return value.  */

machine_mode
type_natural_mode (const_tree type, const CUMULATIVE_ARGS *cum,
		   bool in_return = false)
{
  machine_mode mode = TYPE_MODE (type);

  if (TREE_CODE (type) == VECTOR_TYPE && !VECTOR_MODE_P (mode))
    {
      HOST_WIDE_INT size = int_size_in_bytes (type);
      if ((size == 8 || size == 16 || size == 32 || size == 64)
	  /* ??? Generic code allows us to create width 1 vectors.  Ignore.  */
	  && TYPE_VECTOR_SUBPARTS (type) > 1)
	{
	  machine_mode innermode = TYPE_MODE (TREE_TYPE (type));

	  /* There are no XFmode vector modes.  */
	  if (innermode == XFmode)
	    return mode;

	  if (TREE_CODE (TREE_TYPE (type)) == REAL_TYPE)
	    mode = MIN_MODE_VECTOR_FLOAT;
	  else
	    mode = MIN_MODE_VECTOR_INT;

	  /* Get the mode which has this inner mode and number of units.  */
	  for (; mode != VOIDmode; mode = GET_MODE_WIDER_MODE (mode))
	    if (GET_MODE_NUNITS (mode) == TYPE_VECTOR_SUBPARTS (type)
		&& GET_MODE_INNER (mode) == innermode)
	      {
		if (size == 64 && !TARGET_AVX512F && !TARGET_IAMCU)
		  {
		    static bool warnedavx512f;
		    static bool warnedavx512f_ret;

		    if (cum && cum->warn_avx512f && !warnedavx512f)
		      {
			if (warning (OPT_Wpsabi, "AVX512F vector argument "
				     "without AVX512F enabled changes the ABI"))
			  warnedavx512f = true;
		      }
		    else if (in_return && !warnedavx512f_ret)
		      {
			if (warning (OPT_Wpsabi, "AVX512F vector return "
				     "without AVX512F enabled changes the ABI"))
			  warnedavx512f_ret = true;
		      }

		    return TYPE_MODE (type);
		  }
		else if (size == 32 && !TARGET_AVX && !TARGET_IAMCU)
		  {
		    static bool warnedavx;
		    static bool warnedavx_ret;

		    if (cum && cum->warn_avx && !warnedavx)
		      {
			if (warning (OPT_Wpsabi, "AVX vector argument "
				     "without AVX enabled changes the ABI"))
			  warnedavx = true;
		      }
		    else if (in_return && !warnedavx_ret)
		      {
			if (warning (OPT_Wpsabi, "AVX vector return "
				     "without AVX enabled changes the ABI"))
			  warnedavx_ret = true;
		      }

		    return TYPE_MODE (type);
		  }
		else if (((size == 8 && TARGET_64BIT) || size == 16)
			 && !TARGET_SSE
			 && !TARGET_IAMCU)
		  {
		    static bool warnedsse;
		    static bool warnedsse_ret;

		    if (cum && cum->warn_sse && !warnedsse)
		      {
			if (warning (OPT_Wpsabi, "SSE vector argument "
				     "without SSE enabled changes the ABI"))
			  warnedsse = true;
		      }
		    else if (!TARGET_64BIT && in_return && !warnedsse_ret)
		      {
			if (warning (OPT_Wpsabi, "SSE vector return "
				     "without SSE enabled changes the ABI"))
			  warnedsse_ret = true;
		      }
		  }
		else if ((size == 8 && !TARGET_64BIT)
			 && !TARGET_MMX
			 && !TARGET_IAMCU)
		  {
		    static bool warnedmmx;
		    static bool warnedmmx_ret;

		    if (cum && cum->warn_mmx && !warnedmmx)
		      {
			if (warning (OPT_Wpsabi, "MMX vector argument "
				     "without MMX enabled changes the ABI"))
			  warnedmmx = true;
		      }
		    else if (in_return && !warnedmmx_ret)
		      {
			if (warning (OPT_Wpsabi, "MMX vector return "
				     "without MMX enabled changes the ABI"))
			  warnedmmx_ret = true;
		      }
		  }
		return mode;
	      }

	  gcc_unreachable ();
	}
    }

  return mode;
}

/* x86-64 register passing implementation.  See x86-64 ABI for details.  Goal
   of this code is to classify each 8bytes of incoming argument by the register
   class and assign registers accordingly.  */

/* Return the union class of CLASS1 and CLASS2.
   See the x86-64 PS ABI for details.  */

static enum x86_64_reg_class
merge_classes (enum x86_64_reg_class class1, enum x86_64_reg_class class2)
{
  /* Rule #1: If both classes are equal, this is the resulting class.  */
  if (class1 == class2)
    return class1;

  /* Rule #2: If one of the classes is NO_CLASS, the resulting class is
     the other class.  */
  if (class1 == X86_64_NO_CLASS)
    return class2;
  if (class2 == X86_64_NO_CLASS)
    return class1;

  /* Rule #3: If one of the classes is MEMORY, the result is MEMORY.  */
  if (class1 == X86_64_MEMORY_CLASS || class2 == X86_64_MEMORY_CLASS)
    return X86_64_MEMORY_CLASS;

  /* Rule #4: If one of the classes is INTEGER, the result is INTEGER.  */
  if ((class1 == X86_64_INTEGERSI_CLASS && class2 == X86_64_SSESF_CLASS)
      || (class2 == X86_64_INTEGERSI_CLASS && class1 == X86_64_SSESF_CLASS))
    return X86_64_INTEGERSI_CLASS;
  if (class1 == X86_64_INTEGER_CLASS || class1 == X86_64_INTEGERSI_CLASS
      || class2 == X86_64_INTEGER_CLASS || class2 == X86_64_INTEGERSI_CLASS)
    return X86_64_INTEGER_CLASS;

  /* Rule #5: If one of the classes is X87, X87UP, or COMPLEX_X87 class,
     MEMORY is used.  */
  if (class1 == X86_64_X87_CLASS
      || class1 == X86_64_X87UP_CLASS
      || class1 == X86_64_COMPLEX_X87_CLASS
      || class2 == X86_64_X87_CLASS
      || class2 == X86_64_X87UP_CLASS
      || class2 == X86_64_COMPLEX_X87_CLASS)
    return X86_64_MEMORY_CLASS;

  /* Rule #6: Otherwise class SSE is used.  */
  return X86_64_SSE_CLASS;
}

/* Classify the argument of type TYPE and mode MODE.
   CLASSES will be filled by the register class used to pass each word
   of the operand.  The number of words is returned.  In case the parameter
   should be passed in memory, 0 is returned. As a special case for zero
   sized containers, classes[0] will be NO_CLASS and 1 is returned.

   BIT_OFFSET is used internally for handling records and specifies offset
   of the offset in bits modulo 512 to avoid overflow cases.

   See the x86-64 PS ABI for details.
*/

static int
classify_argument (machine_mode mode, const_tree type,
		   enum x86_64_reg_class classes[MAX_CLASSES], int bit_offset)
{
  HOST_WIDE_INT bytes =
    (mode == BLKmode) ? int_size_in_bytes (type) : (int) GET_MODE_SIZE (mode);
  int words = CEIL (bytes + (bit_offset % 64) / 8, UNITS_PER_WORD);

  /* Variable sized entities are always passed/returned in memory.  */
  if (bytes < 0)
    return 0;

  if (mode != VOIDmode
      && targetm.calls.must_pass_in_stack (mode, type))
    return 0;

  if (type && AGGREGATE_TYPE_P (type))
    {
      int i;
      tree field;
      enum x86_64_reg_class subclasses[MAX_CLASSES];

      /* On x86-64 we pass structures larger than 64 bytes on the stack.  */
      if (bytes > 64)
	return 0;

      for (i = 0; i < words; i++)
	classes[i] = X86_64_NO_CLASS;

      /* Zero sized arrays or structures are NO_CLASS.  We return 0 to
	 signalize memory class, so handle it as special case.  */
      if (!words)
	{
	  classes[0] = X86_64_NO_CLASS;
	  return 1;
	}

      /* Classify each field of record and merge classes.  */
      switch (TREE_CODE (type))
	{
	case RECORD_TYPE:
	  /* And now merge the fields of structure.  */
	  for (field = TYPE_FIELDS (type); field; field = DECL_CHAIN (field))
	    {
	      if (TREE_CODE (field) == FIELD_DECL)
		{
		  int num;

		  if (TREE_TYPE (field) == error_mark_node)
		    continue;

		  /* Bitfields are always classified as integer.  Handle them
		     early, since later code would consider them to be
		     misaligned integers.  */
		  if (DECL_BIT_FIELD (field))
		    {
		      for (i = (int_bit_position (field)
				+ (bit_offset % 64)) / 8 / 8;
			   i < ((int_bit_position (field) + (bit_offset % 64))
			        + tree_to_shwi (DECL_SIZE (field))
				+ 63) / 8 / 8; i++)
			classes[i] =
			  merge_classes (X86_64_INTEGER_CLASS,
					 classes[i]);
		    }
		  else
		    {
		      int pos;

		      type = TREE_TYPE (field);

		      /* Flexible array member is ignored.  */
		      if (TYPE_MODE (type) == BLKmode
			  && TREE_CODE (type) == ARRAY_TYPE
			  && TYPE_SIZE (type) == NULL_TREE
			  && TYPE_DOMAIN (type) != NULL_TREE
			  && (TYPE_MAX_VALUE (TYPE_DOMAIN (type))
			      == NULL_TREE))
			{
			  static bool warned;

			  if (!warned && warn_psabi)
			    {
			      warned = true;
			      inform (input_location,
				      "the ABI of passing struct with"
				      " a flexible array member has"
				      " changed in GCC 4.4");
			    }
			  continue;
			}
		      num = classify_argument (TYPE_MODE (type), type,
					       subclasses,
					       (int_bit_position (field)
						+ bit_offset) % 512);
		      if (!num)
			return 0;
		      pos = (int_bit_position (field)
			     + (bit_offset % 64)) / 8 / 8;
		      for (i = 0; i < num && (i + pos) < words; i++)
			classes[i + pos] =
			  merge_classes (subclasses[i], classes[i + pos]);
		    }
		}
	    }
	  break;

	case ARRAY_TYPE:
	  /* Arrays are handled as small records.  */
	  {
	    int num;
	    num = classify_argument (TYPE_MODE (TREE_TYPE (type)),
				     TREE_TYPE (type), subclasses, bit_offset);
	    if (!num)
	      return 0;

	    /* The partial classes are now full classes.  */
	    if (subclasses[0] == X86_64_SSESF_CLASS && bytes != 4)
	      subclasses[0] = X86_64_SSE_CLASS;
	    if (subclasses[0] == X86_64_INTEGERSI_CLASS
		&& !((bit_offset % 64) == 0 && bytes == 4))
	      subclasses[0] = X86_64_INTEGER_CLASS;

	    for (i = 0; i < words; i++)
	      classes[i] = subclasses[i % num];

	    break;
	  }
	case UNION_TYPE:
	case QUAL_UNION_TYPE:
	  /* Unions are similar to RECORD_TYPE but offset is always 0.
	     */
	  for (field = TYPE_FIELDS (type); field; field = DECL_CHAIN (field))
	    {
	      if (TREE_CODE (field) == FIELD_DECL)
		{
		  int num;

		  if (TREE_TYPE (field) == error_mark_node)
		    continue;

		  num = classify_argument (TYPE_MODE (TREE_TYPE (field)),
					   TREE_TYPE (field), subclasses,
					   bit_offset);
		  if (!num)
		    return 0;
		  for (i = 0; i < num && i < words; i++)
		    classes[i] = merge_classes (subclasses[i], classes[i]);
		}
	    }
	  break;

	default:
	  gcc_unreachable ();
	}

      if (words > 2)
	{
	  /* When size > 16 bytes, if the first one isn't
	     X86_64_SSE_CLASS or any other ones aren't
	     X86_64_SSEUP_CLASS, everything should be passed in
	     memory.  */
	  if (classes[0] != X86_64_SSE_CLASS)
	      return 0;

	  for (i = 1; i < words; i++)
	    if (classes[i] != X86_64_SSEUP_CLASS)
	      return 0;
	}

      /* Final merger cleanup.  */
      for (i = 0; i < words; i++)
	{
	  /* If one class is MEMORY, everything should be passed in
	     memory.  */
	  if (classes[i] == X86_64_MEMORY_CLASS)
	    return 0;

	  /* The X86_64_SSEUP_CLASS should be always preceded by
	     X86_64_SSE_CLASS or X86_64_SSEUP_CLASS.  */
	  if (classes[i] == X86_64_SSEUP_CLASS
	      && classes[i - 1] != X86_64_SSE_CLASS
	      && classes[i - 1] != X86_64_SSEUP_CLASS)
	    {
	      /* The first one should never be X86_64_SSEUP_CLASS.  */
	      gcc_assert (i != 0);
	      classes[i] = X86_64_SSE_CLASS;
	    }

	  /*  If X86_64_X87UP_CLASS isn't preceded by X86_64_X87_CLASS,
	       everything should be passed in memory.  */
	  if (classes[i] == X86_64_X87UP_CLASS
	      && (classes[i - 1] != X86_64_X87_CLASS))
	    {
	      static bool warned;

	      /* The first one should never be X86_64_X87UP_CLASS.  */
	      gcc_assert (i != 0);
	      if (!warned && warn_psabi)
		{
		  warned = true;
		  inform (input_location,
			  "the ABI of passing union with long double"
			  " has changed in GCC 4.4");
		}
	      return 0;
	    }
	}
      return words;
    }

  /* Compute alignment needed.  We align all types to natural boundaries with
     exception of XFmode that is aligned to 64bits.  */
  if (mode != VOIDmode && mode != BLKmode)
    {
      int mode_alignment = GET_MODE_BITSIZE (mode);

      if (mode == XFmode)
	mode_alignment = 128;
      else if (mode == XCmode)
	mode_alignment = 256;
      if (COMPLEX_MODE_P (mode))
	mode_alignment /= 2;
      /* Misaligned fields are always returned in memory.  */
      if (bit_offset % mode_alignment)
	return 0;
    }

  /* for V1xx modes, just use the base mode */
  if (VECTOR_MODE_P (mode) && mode != V1DImode && mode != V1TImode
      && GET_MODE_UNIT_SIZE (mode) == bytes)
    mode = GET_MODE_INNER (mode);

  /* Classification of atomic types.  */
  switch (mode)
    {
    case SDmode:
    case DDmode:
      classes[0] = X86_64_SSE_CLASS;
      return 1;
    case TDmode:
      classes[0] = X86_64_SSE_CLASS;
      classes[1] = X86_64_SSEUP_CLASS;
      return 2;
    case DImode:
    case SImode:
    case HImode:
    case QImode:
    case CSImode:
    case CHImode:
    case CQImode:
      {
	int size = bit_offset + (int) GET_MODE_BITSIZE (mode);

	/* Analyze last 128 bits only.  */
	size = (size - 1) & 0x7f;

	if (size < 32)
	  {
	    classes[0] = X86_64_INTEGERSI_CLASS;
	    return 1;
	  }
	else if (size < 64)
	  {
	    classes[0] = X86_64_INTEGER_CLASS;
	    return 1;
	  }
	else if (size < 64+32)
	  {
	    classes[0] = X86_64_INTEGER_CLASS;
	    classes[1] = X86_64_INTEGERSI_CLASS;
	    return 2;
	  }
	else if (size < 64+64)
	  {
	    classes[0] = classes[1] = X86_64_INTEGER_CLASS;
	    return 2;
	  }
	else
	  gcc_unreachable ();
      }
    case CDImode:
    case TImode:
      classes[0] = classes[1] = X86_64_INTEGER_CLASS;
      return 2;
    case COImode:
    case OImode:
      /* OImode shouldn't be used directly.  */
      gcc_unreachable ();
    case CTImode:
      return 0;
    case SFmode:
      if (!(bit_offset % 64))
	classes[0] = X86_64_SSESF_CLASS;
      else
	classes[0] = X86_64_SSE_CLASS;
      return 1;
    case DFmode:
      classes[0] = X86_64_SSEDF_CLASS;
      return 1;
    case XFmode:
      classes[0] = X86_64_X87_CLASS;
      classes[1] = X86_64_X87UP_CLASS;
      return 2;
    case TFmode:
      classes[0] = X86_64_SSE_CLASS;
      classes[1] = X86_64_SSEUP_CLASS;
      return 2;
    case SCmode:
      classes[0] = X86_64_SSE_CLASS;
      if (!(bit_offset % 64))
	return 1;
      else
	{
	  static bool warned;

	  if (!warned && warn_psabi)
	    {
	      warned = true;
	      inform (input_location,
		      "the ABI of passing structure with complex float"
		      " member has changed in GCC 4.4");
	    }
	  classes[1] = X86_64_SSESF_CLASS;
	  return 2;
	}
    case DCmode:
      classes[0] = X86_64_SSEDF_CLASS;
      classes[1] = X86_64_SSEDF_CLASS;
      return 2;
    case XCmode:
      classes[0] = X86_64_COMPLEX_X87_CLASS;
      return 1;
    case TCmode:
      /* This modes is larger than 16 bytes.  */
      return 0;
    case V8SFmode:
    case V8SImode:
    case V32QImode:
    case V16HImode:
    case V4DFmode:
    case V4DImode:
      classes[0] = X86_64_SSE_CLASS;
      classes[1] = X86_64_SSEUP_CLASS;
      classes[2] = X86_64_SSEUP_CLASS;
      classes[3] = X86_64_SSEUP_CLASS;
      return 4;
    case V8DFmode:
    case V16SFmode:
    case V8DImode:
    case V16SImode:
    case V32HImode:
    case V64QImode:
      classes[0] = X86_64_SSE_CLASS;
      classes[1] = X86_64_SSEUP_CLASS;
      classes[2] = X86_64_SSEUP_CLASS;
      classes[3] = X86_64_SSEUP_CLASS;
      classes[4] = X86_64_SSEUP_CLASS;
      classes[5] = X86_64_SSEUP_CLASS;
      classes[6] = X86_64_SSEUP_CLASS;
      classes[7] = X86_64_SSEUP_CLASS;
      return 8;
    case V4SFmode:
    case V4SImode:
    case V16QImode:
    case V8HImode:
    case V2DFmode:
    case V2DImode:
      classes[0] = X86_64_SSE_CLASS;
      classes[1] = X86_64_SSEUP_CLASS;
      return 2;
    case V1TImode:
    case V1DImode:
    case V2SFmode:
    case V2SImode:
    case V4HImode:
    case V8QImode:
      classes[0] = X86_64_SSE_CLASS;
      return 1;
    case BLKmode:
    case VOIDmode:
      return 0;
    default:
      gcc_assert (VECTOR_MODE_P (mode));

      if (bytes > 16)
	return 0;

      gcc_assert (GET_MODE_CLASS (GET_MODE_INNER (mode)) == MODE_INT);

      if (bit_offset + GET_MODE_BITSIZE (mode) <= 32)
	classes[0] = X86_64_INTEGERSI_CLASS;
      else
	classes[0] = X86_64_INTEGER_CLASS;
      classes[1] = X86_64_INTEGER_CLASS;
      return 1 + (bytes > 8);
    }
}

/* Examine the argument and return set number of register required in each
   class.  Return true iff parameter should be passed in memory.  */

bool
examine_argument (machine_mode mode, const_tree type, int in_return,
		  int *int_nregs, int *sse_nregs)
{
  enum x86_64_reg_class regclass[MAX_CLASSES];
  int n = classify_argument (mode, type, regclass, 0);

  *int_nregs = 0;
  *sse_nregs = 0;

  if (!n)
    return true;
  for (n--; n >= 0; n--)
    switch (regclass[n])
      {
      case X86_64_INTEGER_CLASS:
      case X86_64_INTEGERSI_CLASS:
	(*int_nregs)++;
	break;
      case X86_64_SSE_CLASS:
      case X86_64_SSESF_CLASS:
      case X86_64_SSEDF_CLASS:
	(*sse_nregs)++;
	break;
      case X86_64_NO_CLASS:
      case X86_64_SSEUP_CLASS:
	break;
      case X86_64_X87_CLASS:
      case X86_64_X87UP_CLASS:
      case X86_64_COMPLEX_X87_CLASS:
	if (!in_return)
	  return true;
	break;
      case X86_64_MEMORY_CLASS:
	gcc_unreachable ();
      }

  return false;
}

