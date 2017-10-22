/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Console Utilities Library
 * FILE:            sdk/lib/conutils/readinput.h
 * PURPOSE:         Console/terminal screen management.
 * PROGRAMMERS:     - Hermes Belusca-Maito (for the library);
 *                  - All programmers who wrote the different console applications
 *                    from which I took those functions and improved them.
 */

#ifndef __READINPUT_H__
#define __READINPUT_H__

#ifndef _UNICODE
#error The ConUtils library at the moment only supports compilation with _UNICODE defined!
#endif

/*****************************************************************************\
 **     G E N E R A L   A U T O C O M P L E T I O N   I N T E R F A C E     **
\*****************************************************************************/

#define IS_COMPLETION_DISABLED(CompletionCtrl)  \
    ((CompletionCtrl) == 0x00 || (CompletionCtrl) == 0x0D || (CompletionCtrl) >= 0x20)

struct _COMPLETION_CONTEXT;

typedef BOOL (*COMPLETER_CALLBACK)(
    IN OUT struct _COMPLETION_CONTEXT* Context,
    IN PVOID CompleterParameter OPTIONAL,
    IN OUT LPTSTR str,
    IN UINT cursor,
    IN UINT charcount,
    IN TCHAR CompletionChar,
    IN ULONG ControlKeyState,
    IN OUT PBOOL RestartCompletion);

typedef VOID (*COMPLETER_CLEANUP)(IN OUT struct _COMPLETION_CONTEXT* Context);

typedef struct _COMPLETION_CONTEXT
{
    LPTSTR CmdLine;         // --> String / TextLine
    // UINT InsertPos;
    UINT MaxSize;

    PVOID CompleterContext;
    COMPLETER_CALLBACK CompleterCallback;
    COMPLETER_CLEANUP  CompleterCleanup;
} COMPLETION_CONTEXT, *PCOMPLETION_CONTEXT;

VOID
InitCompletionContext(
    IN OUT PCOMPLETION_CONTEXT Context,
    IN COMPLETER_CALLBACK CompleterCallback /* ,
    IN PVOID CompleterContext */);

VOID
FreeCompletionContext(
    IN OUT PCOMPLETION_CONTEXT Context);

BOOL
DoCompletion(
    IN PCOMPLETION_CONTEXT Context,
    IN PVOID CompleterParameter OPTIONAL,
    IN OUT LPTSTR CmdLine,      // --> String / TextLine
    IN UINT cursor,
    IN UINT charcount,
    IN TCHAR CompletionChar,
    IN ULONG ControlKeyState,
    OUT PBOOL CompletionRestarted OPTIONAL);

#endif  /* __READINPUT_H__ */
