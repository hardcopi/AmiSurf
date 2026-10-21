/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * amiga_compiler.h - Written for the AmiStudio suite.
 * Copyright (C) 2026 the AmiStudio authors. Licensed under the GNU GPL v3 (see COPYING).
 *
 * The few register-argument helpers the AmiStudio MUI code needs for hook and
 * custom-class dispatcher functions. Only what AmiStudio actually uses is here.
 *
 *   static ULONG ASM SAVEDS my_hook(REG(a0, struct Hook *h), REG(a2, Object *o), REG(a1, APTR msg));
 */
#ifndef AMISTUDIO_AMIGA_COMPILER_H
#define AMISTUDIO_AMIGA_COMPILER_H

#if defined(__GNUC__) && defined(__mc68000__)
  /* m68k-amigaos-gcc: each parameter names its own register. */
# ifndef REG
#  define REG(r, decl) decl __asm(#r)
# endif
# ifndef SAVEDS
#  define SAVEDS __saveds
# endif
#else
  /* Anything else (e.g. host-side syntax checks): plain C parameters. */
# ifndef REG
#  define REG(r, decl) decl
# endif
# ifndef SAVEDS
#  define SAVEDS
# endif
#endif

/* Register placement is done per parameter by REG(), so ASM adds nothing. */
#ifndef ASM
# define ASM
#endif

#endif /* AMISTUDIO_AMIGA_COMPILER_H */
