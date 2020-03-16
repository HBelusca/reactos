/*
 * LICENSE:         GPL - See COPYING in the top level directory
 * PROJECT:         ReactOS Console Server DLL
 * FILE:            win32ss/user/winsrv/consrv/lineinput.c
 * PURPOSE:         Console line input functions
 * PROGRAMMERS:     Jeffrey Morlan
 */

/* INCLUDES *******************************************************************/

#include "consrv.h"
#include "history.h"
#include "popup.h"

#define NDEBUG
#include <debug.h>


/* PRIVATE FUNCTIONS **********************************************************/

static VOID
LineInputSetPos(
    IN PLINE_EDIT_INFO LineEditInfo,
    IN UINT Pos)
{
    if (Pos != LineEditInfo->LinePos && (LineEditInfo->Mode & ENABLE_ECHO_INPUT))
    {
        PCONSOLE_SCREEN_BUFFER Buffer = LineEditInfo->ScreenBuffer;
        SHORT OldCursorX, OldCursorY;
        INT XY;

        ASSERT(Buffer);

        OldCursorX = Buffer->CursorPosition.X;
        OldCursorY = Buffer->CursorPosition.Y;
        XY = OldCursorY * Buffer->ScreenBufferSize.X + OldCursorX;

        XY += (Pos - LineEditInfo->LinePos);
        if (XY < 0)
            XY = 0;
        else if (XY >= Buffer->ScreenBufferSize.Y * Buffer->ScreenBufferSize.X)
            XY = Buffer->ScreenBufferSize.Y * Buffer->ScreenBufferSize.X - 1;

        Buffer->CursorPosition.X = XY % Buffer->ScreenBufferSize.X;
        Buffer->CursorPosition.Y = XY / Buffer->ScreenBufferSize.X;
        TermSetScreenInfo(Buffer->Header.Console, Buffer, OldCursorX, OldCursorY);
    }

    LineEditInfo->LinePos = Pos;
}

static VOID
LineInputEdit(
    IN PLINE_EDIT_INFO LineEditInfo,
    IN UINT NumToDelete,
    IN UINT NumToInsert,
    IN PWCHAR Insertion)
{
    UINT Pos = LineEditInfo->LinePos;
    UINT NewSize = LineEditInfo->LineSize - NumToDelete + NumToInsert;
    UINT i;

    /* Make sure there is always enough room for ending \r\n */
    if (NewSize + 2 > LineEditInfo->LineMaxSize)
        return;

    memmove(&LineEditInfo->LineBuffer[Pos + NumToInsert],
            &LineEditInfo->LineBuffer[Pos + NumToDelete],
            (LineEditInfo->LineSize - (Pos + NumToDelete)) * sizeof(WCHAR));
    memcpy(&LineEditInfo->LineBuffer[Pos], Insertion, NumToInsert * sizeof(WCHAR));

    if (LineEditInfo->Mode & ENABLE_ECHO_INPUT)
    {
        PTEXTMODE_SCREEN_BUFFER Buffer;

        ASSERT(LineEditInfo->ScreenBuffer);
        if (GetType(LineEditInfo->ScreenBuffer) != TEXTMODE_BUFFER) return;
        Buffer = (PTEXTMODE_SCREEN_BUFFER)LineEditInfo->ScreenBuffer;

        if (Pos < NewSize)
        {
            TermWriteStream(Buffer->Header.Console, Buffer,
                            &LineEditInfo->LineBuffer[Pos],
                            NewSize - Pos,
                            TRUE);
        }
        for (i = NewSize; i < LineEditInfo->LineSize; ++i)
        {
            TermWriteStream(Buffer->Header.Console, Buffer, L" ", 1, TRUE);
        }
        LineEditInfo->LinePos = i;
    }

    LineEditInfo->LineSize = NewSize;
    LineInputSetPos(LineEditInfo, Pos + NumToInsert);
}

static VOID
LineInputRecallHistory(
    IN PLINE_EDIT_INFO LineEditInfo,
    IN INT Offset)
{
    UNICODE_STRING Entry;

    if (!HistoryRecallHistory(LineEditInfo->Hist, Offset, &Entry)) return;

    LineInputSetPos(LineEditInfo, 0);
    LineInputEdit(LineEditInfo, LineEditInfo->LineSize,
                  Entry.Length / sizeof(WCHAR),
                  Entry.Buffer);
}


// TESTS!!
PPOPUP_WINDOW Popup = NULL;

PPOPUP_WINDOW
HistoryDisplayCurrentHistory(
    IN PCONSOLE_SCREEN_BUFFER ScreenBuffer,
    IN PHISTORY_BUFFER Hist);

VOID
LineInputKeyDown(
    IN PLINE_EDIT_INFO LineEditInfo,
    IN PKEY_EVENT_RECORD KeyEvent)
{
    PCONSRV_CONSOLE Console;
    UNICODE_STRING Entry;
    UINT Pos = LineEditInfo->LinePos;

    Console = LineEditInfo->InputBuffer->Header.Console;

    /*
     * Validity check:
     * both the input buffer and screen buffer must be on the same console.
     */
    if (LineEditInfo->ScreenBuffer)
        ASSERT(LineEditInfo->ScreenBuffer->Header.Console == Console);

    /*
     * First, deal with control keys...
     */

    switch (KeyEvent->wVirtualKeyCode)
    {
        case VK_ESCAPE:
        {
            /* Clear the entire line */
            LineInputSetPos(LineEditInfo, 0);
            LineInputEdit(LineEditInfo, LineEditInfo->LineSize, 0, NULL);

            // TESTS!!
            if (Popup)
            {
                DestroyPopupWindow(Popup);
                Popup = NULL;
            }
            return;
        }

        case VK_HOME:
        {
            /* Move to start of line. With CTRL, erase everything left of cursor. */
            LineInputSetPos(LineEditInfo, 0);
            if (KeyEvent->dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
                LineInputEdit(LineEditInfo, Pos, 0, NULL);
            return;
        }

        case VK_END:
        {
            /* Move to end of line. With CTRL, erase everything right of cursor. */
            if (KeyEvent->dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
                LineInputEdit(LineEditInfo, LineEditInfo->LineSize - Pos, 0, NULL);
            else
                LineInputSetPos(LineEditInfo, LineEditInfo->LineSize);
            return;
        }

        case VK_LEFT:
        {
            /* Move to the left. With CTRL, move to beginning of previous word. */
            if (KeyEvent->dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
            {
                while (Pos > 0 && LineEditInfo->LineBuffer[Pos - 1] == L' ') Pos--;
                while (Pos > 0 && LineEditInfo->LineBuffer[Pos - 1] != L' ') Pos--;
            }
            else
            {
                Pos -= (Pos > 0);
            }
            LineInputSetPos(LineEditInfo, Pos);
            return;
        }

        case VK_RIGHT:
        case VK_F1:
        {
            /* Move to the right. With CTRL, move to beginning of next word. */
            if (KeyEvent->dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
            {
                while (Pos < LineEditInfo->LineSize && LineEditInfo->LineBuffer[Pos] != L' ') Pos++;
                while (Pos < LineEditInfo->LineSize && LineEditInfo->LineBuffer[Pos] == L' ') Pos++;
                LineInputSetPos(LineEditInfo, Pos);
            }
            else
            {
                /* Recall one character (but don't overwrite current line) */
                HistoryGetCurrentEntry(LineEditInfo->Hist, &Entry);
                if (Pos < LineEditInfo->LineSize)
                    LineInputSetPos(LineEditInfo, Pos + 1);
                else if (Pos * sizeof(WCHAR) < Entry.Length)
                    LineInputEdit(LineEditInfo, 0, 1, &Entry.Buffer[Pos]);
            }
            return;
        }

        case VK_INSERT:
        {
            /* Toggle between insert and overstrike */
            ASSERT(!LineEditInfo->LineComplete);
            LineEditInfo->LineInsertToggle = !LineEditInfo->LineInsertToggle;

            /* Set the cursor size */
            if (LineEditInfo->ScreenBuffer)
            {
                LineEditInfo->ScreenBuffer->CursorIsDouble =
                    (Console->InsertMode != LineEditInfo->LineInsertToggle);
                TermSetCursorInfo(Console, LineEditInfo->ScreenBuffer);
            }
            return;
        }

        case VK_DELETE:
        {
            /* Remove one character to right of cursor */
            if (Pos != LineEditInfo->LineSize)
                LineInputEdit(LineEditInfo, 1, 0, NULL);
            return;
        }

        case VK_PRIOR:
        {
            /* Recall the first history entry */
            LineInputRecallHistory(LineEditInfo, -((WORD)-1));
            return;
        }

        case VK_NEXT:
        {
            /* Recall the last history entry */
            LineInputRecallHistory(LineEditInfo, +((WORD)-1));
            return;
        }

        case VK_UP:
        case VK_F5:
        {
            /*
             * Recall the previous history entry. On first time, actually recall
             * the current (usually last) entry; on subsequent times go back.
             */
            LineInputRecallHistory(LineEditInfo, LineEditInfo->LineUpPressed ? -1 : 0);
            LineEditInfo->LineUpPressed = TRUE;
            return;
        }

        case VK_DOWN:
        {
            /* Recall the next history entry */
            LineInputRecallHistory(LineEditInfo, +1);
            return;
        }

        case VK_F3:
        {
            /* Recall the remainder of the current history entry */
            HistoryGetCurrentEntry(LineEditInfo->Hist, &Entry);
            if (Pos * sizeof(WCHAR) < Entry.Length)
            {
                UINT InsertSize = (Entry.Length / sizeof(WCHAR) - Pos);
                UINT DeleteSize = min(LineEditInfo->LineSize - Pos, InsertSize);
                LineInputEdit(LineEditInfo, DeleteSize, InsertSize, &Entry.Buffer[Pos]);
            }
            return;
        }

        case VK_F6:
        {
            /* Insert a ^Z character */
            KeyEvent->uChar.UnicodeChar = 26;
            break;
        }

        case VK_F7:
        {
            if (KeyEvent->dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED))
            {
                HistoryDeleteBuffer(LineEditInfo->Hist); // FIXME: Actually we should just expunge !!
            }
            else
            {
                if (Popup) DestroyPopupWindow(Popup);
                if (LineEditInfo->ScreenBuffer)
                    Popup = HistoryDisplayCurrentHistory(LineEditInfo->ScreenBuffer, LineEditInfo->Hist);
                else
                    Popup = NULL;
            }
            return;
        }

        case VK_F8:
        {
            UNICODE_STRING EntryFound;

            Entry.Length = LineEditInfo->LinePos * sizeof(WCHAR); // == Pos * sizeof(WCHAR)
            Entry.Buffer = LineEditInfo->LineBuffer;

            /*
             * Like Up/F5, on first time start from current (usually last) entry,
             * but on subsequent times start at previous entry.
             */
            if (HistoryFindEntryByPrefix(LineEditInfo->Hist,
                                         LineEditInfo->LineUpPressed,
                                         &Entry, &EntryFound))
            {
                LineEditInfo->LineUpPressed = TRUE;
                LineInputEdit(LineEditInfo, LineEditInfo->LineSize - Pos,
                              EntryFound.Length / sizeof(WCHAR) - Pos,
                              &EntryFound.Buffer[Pos]);
                /* Cursor stays where it was */
                LineInputSetPos(LineEditInfo, Pos);
            }

            return;
        }
    }


    /*
     * OK, we deal with normal keys, we can continue...
     */

    if (KeyEvent->uChar.UnicodeChar == L'\b' && (LineEditInfo->Mode & ENABLE_PROCESSED_INPUT))
    {
        /*
         * Backspace handling - if processed input enabled then we handle it
         * here, otherwise we treat it like a normal character.
         */
        if (Pos > 0)
        {
            LineInputSetPos(LineEditInfo, Pos - 1);
            LineInputEdit(LineEditInfo, 1, 0, NULL);
        }
    }
    else if (KeyEvent->uChar.UnicodeChar == L'\r')
    {
        /*
         * Only add a history entry if console echo is enabled. This ensures
         * that anything sent to the console when echo is disabled (e.g.
         * binary data, or secrets like passwords...) does not remain stored
         * in memory.
         */
        if (LineEditInfo->Mode & ENABLE_ECHO_INPUT)
        {
            Entry.Length = Entry.MaximumLength = LineEditInfo->LineSize * sizeof(WCHAR);
            Entry.Buffer = LineEditInfo->LineBuffer;
            HistoryAddEntry(LineEditInfo->Hist, &Entry, Console->HistoryNoDup);
        }

        /* TODO: Expand aliases */
        DPRINT1("TODO: Expand aliases for ExeName %wZ\n", &LineEditInfo->ExeName);

        LineInputSetPos(LineEditInfo, LineEditInfo->LineSize);
        LineEditInfo->LineBuffer[LineEditInfo->LineSize++] = L'\r';
        if (LineEditInfo->Mode & ENABLE_ECHO_INPUT)
        {
            ASSERT(LineEditInfo->ScreenBuffer);
            if (GetType(LineEditInfo->ScreenBuffer) == TEXTMODE_BUFFER)
            {
                TermWriteStream(Console,
                                (PTEXTMODE_SCREEN_BUFFER)(LineEditInfo->ScreenBuffer),
                                L"\r", 1, TRUE);
            }
        }

        /*
         * Add \n if processed input. There should usually be room for it,
         * but an exception to the rule exists: the buffer could have been
         * pre-filled with LineMaxSize - 1 characters.
         */
        if ((LineEditInfo->Mode & ENABLE_PROCESSED_INPUT) &&
            LineEditInfo->LineSize < LineEditInfo->LineMaxSize)
        {
            LineEditInfo->LineBuffer[LineEditInfo->LineSize++] = L'\n';
            if (LineEditInfo->Mode & ENABLE_ECHO_INPUT)
            {
                ASSERT(LineEditInfo->ScreenBuffer);
                if (GetType(LineEditInfo->ScreenBuffer) == TEXTMODE_BUFFER)
                {
                    TermWriteStream(Console,
                                    (PTEXTMODE_SCREEN_BUFFER)(LineEditInfo->ScreenBuffer),
                                    L"\n", 1, TRUE);
                }
            }
        }
        LineEditInfo->LineComplete = TRUE;
        LineEditInfo->LinePos = 0;
    }
    else if (KeyEvent->uChar.UnicodeChar != L'\0')
    {
        if (KeyEvent->uChar.UnicodeChar < 0x20 &&
            LineEditInfo->LineWakeupMask & (1 << KeyEvent->uChar.UnicodeChar))
        {
            /* Control key client wants to handle itself (e.g. for tab completion) */
            LineEditInfo->LineBuffer[LineEditInfo->LineSize++] = L' ';
            LineEditInfo->LineBuffer[LineEditInfo->LinePos] = KeyEvent->uChar.UnicodeChar;
            LineEditInfo->LineComplete = TRUE;
            LineEditInfo->LinePos = 0;
        }
        else
        {
            /* Normal character */
            BOOL Overstrike = !LineEditInfo->LineInsertToggle && (LineEditInfo->LinePos != LineEditInfo->LineSize);
            DPRINT("Overstrike = %s\n", Overstrike ? "true" : "false");
            LineInputEdit(LineEditInfo, (Overstrike ? 1 : 0), 1, &KeyEvent->uChar.UnicodeChar);
        }
    }
}


/* PUBLIC SERVER APIS *********************************************************/

/* EOF */
