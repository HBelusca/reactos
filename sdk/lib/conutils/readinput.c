/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Console Utilities Library
 * FILE:            sdk/lib/conutils/readinput.c
 * PURPOSE:         Console/terminal screen management.
 * PROGRAMMERS:     - Hermes Belusca-Maito (for the library);
 *                  - All programmers who wrote the different console applications
 *                    from which I took those functions and improved them.
 */

/* FIXME: Temporary HACK before we cleanly support UNICODE functions */
#define UNICODE
#define _UNICODE

#include <tchar.h>

#include <windef.h>
#include <winbase.h>
// #include <winnls.h>
#include <wincon.h>  // Console APIs (only if kernel32 support included)
#include <strsafe.h>

// HACK!
#define NDEBUG
#include <debug.h>

#include "conutils.h"
#include "stream.h"
#include "readinput.h"


/*****************************************************************************\
 **     G E N E R A L   A U T O C O M P L E T I O N   I N T E R F A C E     **
\*****************************************************************************/

VOID
InitCompletionContext(
    IN OUT PCOMPLETION_CONTEXT Context,
    IN COMPLETER_CALLBACK CompleterCallback /* ,
    IN PVOID CompleterContext */)
{
    Context->BeginLine = NULL;
    Context->MaxSize = 0; // InsertPos

    Context->CompleterContext  = NULL; // CompleterContext;
    Context->CompleterCallback = CompleterCallback;
    Context->CompleterCleanup  = NULL;
}

VOID
FreeCompletionContext(
    IN OUT PCOMPLETION_CONTEXT Context)
{
    /* Call the completer cleaning routine */
    if (Context->CompleterCleanup)
        Context->CompleterCleanup(Context);
    Context->CompleterCleanup = NULL;

#if 0 // The CompleterCleanup() function should do this.
    if (Context->CompleterContext)
        HeapFree(GetProcessHeap(), 0, Context->CompleterContext);
#endif
    Context->CompleterContext = NULL;

    if (Context->BeginLine)
        HeapFree(GetProcessHeap(), 0, Context->BeginLine);
    Context->BeginLine = NULL;

    Context->MaxSize = 0; // InsertPos
}


/*
 * TODO:
 * - Call the "BASH-like" mode: "Advanced" / list mode;
 * - Call the "CMD-like" mode : "Simple" / enum mode.
 * In both cases, we also have either "insert" or "overwrite" mode.
 * In the former mode, the completions are inserted, while in the latter
 * the completions replace and thus, erase, the end of the command-line.
 *
 * For example, BASH works in advanced/list + insert mode,
 * while cmd.exe works in simple/enum + overwrite mode.
 */

/*
 * CmdLine : String to complete.
 * charcount: Maximum size of the buffer CmdLine;
 * cursor: Insertion point in CmdLine, but we might edit a bit more inside it.
 */
BOOL
DoCompletion(
    IN PCOMPLETION_CONTEXT Context,
    IN PVOID CompleterParameter OPTIONAL,
    IN OUT LPTSTR CmdLine,
    IN UINT charcount,          // maxlen
    IN UINT cursor,             // Insertion point "hint"
/*
    IN BOOL AdvancedCompletionMode, // TRUE: à la BASH ; FALSE: à la CMD
*/
    IN BOOL InsertMode,     // TRUE: insert ; FALSE: overwrite
    IN TCHAR CompletionChar,
    IN ULONG ControlKeyState,   // Which keys are pressed during the completion
    OUT PBOOL CompletionRestarted OPTIONAL)
{
    BOOL Success;
    BOOL RestartCompletion = TRUE;
    // LPTSTR CompletingWord = NULL;

    SIZE_T LineSize;

    /* Pointer to start of the end of the string */
    LPTSTR EndLine = NULL;

    /*
     * Suppose we are going to restart a new completion.
     * This is what will happen if we fail somewhere in the process.
     * Otherwise the real completion state will be returned afterwards.
     */
    if (CompletionRestarted)
        *CompletionRestarted = RestartCompletion;

    /* Sanity checks */
    if (!Context)
        return FALSE;
    if (!CmdLine /* || charcount == 0 */)
        return TRUE;
    if (cursor >= charcount)
        return FALSE;

    /*
     * Check whether we are likely to restart (or not) a new completion,
     * by comparing the current text line, up to the insertion point, with
     * the previous one that has been cached in the completion context.
     */
    if (!Context->BeginLine || (Context->MaxSize != cursor + 1) ||
        memcmp(Context->BeginLine, CmdLine, cursor*sizeof(TCHAR)) != 0)
    {
        RestartCompletion = TRUE;
    }
    else
    {
        RestartCompletion = FALSE;
    }

    /*
     * Save the original character at the insertion point in the string
     * to be completed, so that it can be restored in case completion fails,
     * ....
     */
    EndLine = NULL;
    if (InsertMode)
    {
        LineSize = charcount - cursor;
        EndLine = HeapAlloc(GetProcessHeap(), 0, LineSize * sizeof(TCHAR));
        if (EndLine == NULL)
        {
            DPRINT("DEBUG: Cannot allocate memory for EndLine!\n");
            return FALSE;
        }
        StringCchCopy(EndLine, LineSize, CmdLine + cursor);
    }
#if 0
    else
    {
        /* Check whether the cursor is not at the end of the string */
        if ((cursor + 1) < _tcslen(CmdLine))
            CmdLine[cursor] = _T('\0');
    }
#endif

    /*
     * NOTE: The custom completer can modify RestartCompletion
     * in case other criteria are met...
     */
    Context->Touched = FALSE;
    Success = Context->CompleterCallback(Context, CompleterParameter,
                                         CmdLine, charcount, cursor,
                                         CompletionChar, ControlKeyState,
                                         // &CompletingWord,
                                         &RestartCompletion);
#if 0
    if (!Success /* || !CompletingWord || !*CompletingWord*/)
    {
        /* Restore the original string */
        // _tcscpy(CmdLine, szOriginal);
    }
#endif

    //
    // TODO: *WE* (and only us) MUST complete the command line string.
    // The completer callback just should compute the possible completions
    // and insert one (or part of) if we can.
    //

    /* If we failed we will need to restart a new completion anyway */
    if (!Success)
        RestartCompletion = TRUE;

    /* Return the actual state of the completion */
    if (CompletionRestarted)
        *CompletionRestarted = RestartCompletion;


    if (Context->BeginLine) HeapFree(GetProcessHeap(), 0, Context->BeginLine);
    Context->BeginLine = NULL;
    Context->MaxSize = 0;


    if (Success)
    {
        LineSize = _tcslen(CmdLine) + 1;
        Context->BeginLine = HeapAlloc(GetProcessHeap(), 0, LineSize * sizeof(TCHAR));
        if (Context->BeginLine == NULL)
        {
            DPRINT("DEBUG: Cannot allocate memory for Context->BeginLine!\n");
            return FALSE;
        }
        Context->MaxSize = LineSize;
        StringCchCopy(Context->BeginLine, LineSize, CmdLine);

        if (Context->Touched && InsertMode /* && EndLine */)
            StringCchCat(CmdLine, charcount, EndLine);
    }


    if (EndLine)
        HeapFree(GetProcessHeap(), 0, EndLine);
    EndLine = NULL;

    return Success;
}
