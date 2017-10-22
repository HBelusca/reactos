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
    Context->CmdLine = NULL;
    // Context->InsertPos = 0;
    Context->MaxSize = 0;

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

    if (Context->CompleterContext)
        HeapFree(GetProcessHeap(), 0, Context->CompleterContext);
    Context->CompleterContext = NULL;

    if (Context->CmdLine)
        HeapFree(GetProcessHeap(), 0, Context->CmdLine);
    Context->CmdLine = NULL;

    // Context->InsertPos = 0;
    Context->MaxSize = 0;
}

/*
 * CmdLine : String to complete, with CmdLine[cursor] == L'\0';
 * cursor: Insertion point in CmdLine, but we might edit a bit more inside CmdLine;
 * charcount: Maximum size of the buffer CmdLine.
 */
BOOL
DoCompletion(
    IN PCOMPLETION_CONTEXT Context,
    IN PVOID CompleterParameter OPTIONAL,
    IN OUT LPTSTR CmdLine,
    IN UINT cursor,             // Insertion point (NULL-terminated)
    IN UINT charcount,          // maxlen
    IN TCHAR CompletionChar,
    IN ULONG ControlKeyState,   // Which keys are pressed during the completion
    OUT PBOOL CompletionRestarted OPTIONAL)
{
    BOOL Success;
    BOOL RestartCompletion = TRUE;

    // FIXME: All that stuff should be dynamic-allocated strings!
    TCHAR szOriginal[MAX_PATH];
    /* Editable string of what was passed in */
    TCHAR str[MAX_PATH];

    if (CompletionRestarted)
        *CompletionRestarted = RestartCompletion;

    if (!Context)
        return FALSE;

    if (!CmdLine)
        return TRUE;

    // FIXME: Use charcount !!

    //
    // FIXME BIG IMPROVEMENT:
    // Find the *real* command start within CmdLine !!
    // (useful in case CmdLine == "some command && cd <completion>" .
    // In that case the current code would check 'some' and think
    // we can complete files+dirs, whereas we really want only dirs
    // because the real command here starts at 'cd').
    // Valid for both UNIX and NT completion. CmdLine and charcount
    // must then be readjusted internally.
    //


    // if (_tcscmp(CmdLine, Context->CmdLine) || !_tcslen(CmdLine))
    // if (_tcscmp(str, LastReturned) || !_tcslen(str))
#if 0
    if (!Context->CmdLine || (Context->MaxSize != charcount + 1) ||
        memcmp(Context->CmdLine, CmdLine, charcount*sizeof(TCHAR)) != 0)
#else
    if (!Context->CmdLine || (Context->MaxSize != cursor + 1) ||
        memcmp(Context->CmdLine, CmdLine, cursor*sizeof(TCHAR)) != 0)
#endif
    {
        RestartCompletion = TRUE;
    }
    else
    {
        RestartCompletion = FALSE;
    }

    // CompletionRestarted will be reset after...


    //
    // FIXME! FIXME! FIXME! FIXME! FIXME! FIXME! FIXME! FIXME! FIXME! FIXME!
    //
    // FIXME: Be sure we don't overwrite past charcount characters!!!
    //
    // FIXME! FIXME! FIXME! FIXME! FIXME! FIXME! FIXME! FIXME! FIXME! FIXME!
    //

    /* Copy the string, str can be edited and original should not be */
    _tcscpy(str, CmdLine);        // Save a working copy
    _tcscpy(szOriginal, CmdLine); // Save the original string for restoration in case of failure


    /*
     * NOTE: The custom completer can modify RestartCompletion
     * in case other criteria are met...
     */
    Success = Context->CompleterCallback(Context, CompleterParameter,
                                         str, cursor, charcount,
                                         CompletionChar, ControlKeyState,
                                         &RestartCompletion);
    if (!Success)
    {
        /* Restore the original string */
        _tcscpy(CmdLine, szOriginal);
    }
    else
    {
        /* Everything is deleted, lets add it back in */
        _tcscpy(CmdLine, str);
    }

    /* If we failed we will need to restart a new completion anyway */
    if (!Success)
        RestartCompletion = TRUE;


    if (CompletionRestarted)
        *CompletionRestarted = RestartCompletion;


    if (Context->CmdLine) HeapFree(GetProcessHeap(), 0, Context->CmdLine);
    Context->CmdLine = NULL;

/**************** FIXME!!!!!!!! **********************************************/

    if (Success)
    {

    //
    // FIXME!
    //
    charcount = _tcslen(CmdLine) + 1;
    //
    //
    Context->CmdLine = HeapAlloc(GetProcessHeap(), 0, charcount * sizeof(TCHAR));
    if (Context->CmdLine == NULL)
    {
        // Context->MaxSize = 0;
        DPRINT("DEBUG: Cannot allocate memory for Context->CmdLine!\n");
        return FALSE;
    }
    Context->MaxSize = charcount; /// MaxBufSize!!
    _tcscpy(Context->CmdLine, CmdLine);

    }
    else
    {
        // ????
    }

    return Success;
}
