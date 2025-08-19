/*
 * PROJECT:     NativeRun Command
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Implements NT Native compatible printf-like functions.
 * COPYRIGHT:   Copyright 2025 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

/* INCLUDES ******************************************************************/

#if defined(__GNUC__) && !defined(__clang__)
  #define _CRT_DISABLE_GCC_PRINTF_WARNINGS \
            _Pragma("GCC diagnostic push") \
            /*_Pragma("GCC diagnostic ignored \"-Wbuiltin-declaration-mismatch\"")*/ \
            _Pragma("GCC diagnostic ignored \"-Wformat\"") \
            _Pragma("GCC diagnostic ignored \"-Wformat-extra-args\"")
            // _Pragma("GCC diagnostic ignored \"-Wunknown-pragmas\"")
  #define _CRT_RESTORE_GCC_PRINTF_WARNINGS _Pragma("GCC diagnostic pop")
#else // __GNUC__
  #define _CRT_DISABLE_GCC_PRINTF_WARNINGS
  #define _CRT_RESTORE_GCC_PRINTF_WARNINGS
#endif // __GNUC__

#define _FILE_DEFINED       // Disable FILE type to get ours instead.
typedef void** FILE;
#if 0
typedef struct _iobuf
{
    void** phandle;
} FILE;
#endif
#include <stdio.h>

#ifndef STDIO_HANDLES_COUNT
#define STDIO_HANDLES_COUNT 3
#endif

#ifdef _IOB_ENTRIES
#undef _IOB_ENTRIES
#define _IOB_ENTRIES        STDIO_HANDLES_COUNT
#endif

// extern FILE _iob[_IOB_ENTRIES];
// extern FILE* __cdecl __iob_func(void);


/* FUNCTIONS *****************************************************************/

typedef void VOID;
typedef long NTSTATUS;

NTSTATUS
InitNtCrtIO(VOID);

VOID
FiniNtCrtIO(VOID);
