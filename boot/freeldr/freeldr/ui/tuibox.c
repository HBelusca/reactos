/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Text UI *Box Functions
 * COPYRIGHT:   Copyright 1998-2003 Brian Palmer <brianp@sginet.com>
 *              Copyright 2013-2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

/* INCLUDES ******************************************************************/

#include <freeldr.h>

typedef struct _SMALL_RECT
{
    SHORT Left;
    SHORT Top;
    SHORT Right;
    SHORT Bottom;
} SMALL_RECT, *PSMALL_RECT;

/* FUNCTIONS *****************************************************************/

static VOID
TuiDrawMsgBoxCommon(
    _In_ PCSTR MessageText,
    _Out_ PSMALL_RECT MsgBoxRect)
{
    INT width = 8;
    ULONG height = 1;
    INT curline = 0;
    INT k;
    size_t i, j;
    INT x1, x2, y1, y2;
    CHAR temp[260];

    /* Find the height */
    for (i = 0; i < strlen(MessageText); i++)
    {
        if (MessageText[i] == '\n')
            height++;
    }

    /* Find the width */
    for (i = j = k = 0; i < height; i++)
    {
        while ((MessageText[j] != '\n') && (MessageText[j] != ANSI_NULL))
        {
            j++;
            k++;
        }

        if (k > width)
            width = k;

        k = 0;
        j++;
    }

    /* Calculate box area */
    x1 = (UiScreenWidth - (width+2))/2;
    x2 = x1 + width + 3;
    y1 = ((UiScreenHeight - height - 2)/2) + 1;
    y2 = y1 + height + 4;

    MsgBoxRect->Left = x1; MsgBoxRect->Right  = x2;
    MsgBoxRect->Top  = y1; MsgBoxRect->Bottom = y2;


    /* Draw the box */
    TuiDrawBox(x1, y1, x2, y2, D_VERT, D_HORZ, TRUE, TRUE,
               ATTR(UiMessageBoxFgColor, UiMessageBoxBgColor));

    /* Draw the text */
    for (i = j = 0; i < strlen(MessageText) + 1; i++)
    {
        if ((MessageText[i] == '\n') || (MessageText[i] == ANSI_NULL))
        {
            temp[j] = ANSI_NULL;
            j = 0;
            UiDrawText(x1 + 2, y1 + 1 + curline, temp,
                       ATTR(UiMessageBoxFgColor, UiMessageBoxBgColor));
            curline++;
        }
        else
        {
            temp[j++] = MessageText[i];
        }
    }
}

// UI_EVENT_PROC
static ULONG_PTR
NTAPI
TuiMsgBoxProc(
    _In_ PVOID UiContext,
    /**/_In_opt_ PVOID UserContext,/**/
    _In_ UI_EVENT Event,
    _In_ ULONG_PTR EventParam)
{
    if (Event == UiKeyPress)
    {
        CHAR key = (CHAR)EventParam;
        if ((key == KEY_ENTER) || (key == KEY_SPACE) || (key == KEY_ESC))
        {
            UiEndUi(UiContext, 0);
            return TRUE;
        }
    }

    /* Perform default action */
    return FALSE;
}

VOID
TuiMessageBox(
    _In_ PCSTR MessageText)
{
    PVOID ScreenBuffer;

    /* Save the screen contents */
    ScreenBuffer = TuiSaveScreen();

    /* Display the message box */
    TuiMessageBoxCritical(MessageText);

    /* Restore the screen contents */
    TuiRestoreScreen(ScreenBuffer);
}

VOID
TuiMessageBoxCritical(
    _In_ PCSTR MessageText)
{
    SMALL_RECT BoxRect;

    /* Draw the common parts of the message box */
    TuiDrawMsgBoxCommon(MessageText, &BoxRect);

    /* Draw centered OK button */
    UiDrawText((BoxRect.Left + BoxRect.Right) / 2 - 3,
               BoxRect.Bottom - 2,
               "   OK   ",
               ATTR(COLOR_BLACK, COLOR_GRAY));

    /* Draw status text */
    UiDrawStatusText("Press ENTER to continue");

    UiDispatch(TuiMsgBoxProc, NULL);
}

typedef struct _TUI_EDIT_CONTEXT
{
    PCHAR Buffer;   //< Text buffer
    ULONG Length;   //< Text buffer size  // --> rename to BufferLength ?
    ULONG TextLength;   //< Current text length
    ULONG TextPosition; //< Current text position where the cursor points to

    ULONG Line;     //< Top/Bottom coordinate of the edit box
    ULONG StartX;   //< Left coordinate of the edit box
    ULONG EndX;     //< Right coordinate of the edit box
    ULONG CursorX;  //< Horizontal cursor coordinate (vertical coordinate given by 'Line')
    ULONG TextDisplayIndex;
} TUI_EDIT_CONTEXT, *PTUI_EDIT_CONTEXT;

// UI_EVENT_PROC
static ULONG_PTR
NTAPI
TuiEditBoxProc(
    _In_ PVOID UiContext,
    /**/_In_opt_ PVOID UserContext,/**/
    _In_ UI_EVENT Event,
    _In_ ULONG_PTR EventParam)
{
    switch (Event)
    {
    case UiKeyPress:
    {
        PTUI_EDIT_CONTEXT editCtx = (PTUI_EDIT_CONTEXT)UserContext;
        CHAR key = (CHAR)EventParam;
        BOOLEAN Extended = !!(EventParam & 0x0100);

        /* Default MsgBox control */
        if (key == KEY_ENTER)
        {
            UiEndUi(UiContext, TRUE);
            return TRUE;
        }
        else if (key == KEY_ESC)
        {
            UiEndUi(UiContext, FALSE);
            return TRUE;
        }
        else
        /* Enter the text. Please keep in mind that the default input mode
         * of the edit boxes is in insertion mode, that is, you can insert
         * text without erasing the existing one. */
        if (key == KEY_BACKSPACE) // Remove a character
        {
            if ( (editCtx->TextLength > 0) && (editCtx->TextPosition > 0) &&
                 (editCtx->TextPosition <= editCtx->TextLength) )
            {
                editCtx->TextPosition--;
                memmove(editCtx->Buffer + editCtx->TextPosition,
                        editCtx->Buffer + editCtx->TextPosition + 1,
                        editCtx->TextLength - editCtx->TextPosition);
                editCtx->TextLength--;
                editCtx->Buffer[editCtx->TextLength] = ANSI_NULL;
                UiRedraw(UiContext);
            }
            else
            {
                MachBeep();
            }
        }
        else if (Extended && key == KEY_DELETE) // Remove a character
        {
            if ( (editCtx->TextLength > 0) &&
                 (editCtx->TextPosition < editCtx->TextLength) )
            {
                memmove(editCtx->Buffer + editCtx->TextPosition,
                        editCtx->Buffer + editCtx->TextPosition + 1,
                        editCtx->TextLength - editCtx->TextPosition);
                editCtx->TextLength--;
                editCtx->Buffer[editCtx->TextLength] = ANSI_NULL;
                UiRedraw(UiContext);
            }
            else
            {
                MachBeep();
            }
        }
        else if (Extended && key == KEY_HOME) // Go to the start of the buffer
        {
            editCtx->TextPosition = 0;
            UiRedraw(UiContext);
        }
        else if (Extended && key == KEY_END) // Go to the end of the buffer
        {
            editCtx->TextPosition = editCtx->TextLength;
            UiRedraw(UiContext);
        }
        else if (Extended && key == KEY_RIGHT) // Go right
        {
            if (editCtx->TextPosition < editCtx->TextLength)
            {
                editCtx->TextPosition++;
                UiRedraw(UiContext);
            }
            else
            {
                MachBeep();
            }
        }
        else if (Extended && key == KEY_LEFT) // Go left
        {
            if (editCtx->TextPosition > 0)
            {
                editCtx->TextPosition--;
                UiRedraw(UiContext);
            }
            else
            {
                MachBeep();
            }
        }
        else if (!Extended) // Add this key to the buffer
        {
            if ( (editCtx->TextLength   < editCtx->Length - 1) &&
                 (editCtx->TextPosition < editCtx->Length - 1) )
            {
                memmove(editCtx->Buffer + editCtx->TextPosition + 1,
                        editCtx->Buffer + editCtx->TextPosition,
                        editCtx->TextLength - editCtx->TextPosition);
                editCtx->Buffer[editCtx->TextPosition] = key;
                editCtx->TextPosition++;
                editCtx->TextLength++;
                editCtx->Buffer[editCtx->TextLength] = ANSI_NULL;
                UiRedraw(UiContext);
            }
            else
            {
                MachBeep();
            }
        }
        else
        {
            MachBeep();
        }

        return TRUE;
    }

    case UiPaint:
    {
        PTUI_EDIT_CONTEXT editCtx = (PTUI_EDIT_CONTEXT)UserContext;

        // TODO Improve: Detect whether it's just the cursor that moves,
        // and not the text (i.e. TextDisplayIndex), in which case we
        // don't need to redraw everything.

        /* Draw the edit box background */
        UiFillArea(editCtx->StartX,
                   editCtx->Line,
                   editCtx->EndX,
                   editCtx->Line,
                   ' ', ATTR(UiEditBoxTextColor, UiEditBoxBgColor));

        /* Draw the text */
        if (editCtx->TextPosition > (editCtx->EndX - editCtx->StartX))
        {
            editCtx->TextDisplayIndex = editCtx->TextPosition - (editCtx->EndX - editCtx->StartX);
            editCtx->CursorX = editCtx->EndX;
        }
        else
        {
            editCtx->TextDisplayIndex = 0;
            editCtx->CursorX = editCtx->StartX + editCtx->TextPosition;
        }
        UiDrawText2(editCtx->StartX,
                    editCtx->Line,
                    editCtx->EndX - editCtx->StartX + 1,
                    &editCtx->Buffer[editCtx->TextDisplayIndex],
                    ATTR(UiEditBoxTextColor, UiEditBoxBgColor));

        /* Move the cursor */
        MachVideoSetTextCursorPosition(editCtx->CursorX, editCtx->Line);

        break;
    }

    default:
        break;
    }

    /* Perform default action */
    return FALSE;
}

BOOLEAN TuiEditBox(PCSTR MessageText, PCHAR EditTextBuffer, ULONG Length)
{
    SMALL_RECT BoxRect;
    PVOID ScreenBuffer;
    BOOLEAN ReturnCode;

    TUI_EDIT_CONTEXT editCtx = {EditTextBuffer, Length, 0, 0};

    /* Save the screen contents */
    ScreenBuffer = TuiSaveScreen();

    /* Draw the common parts of the message box */
    TuiDrawMsgBoxCommon(MessageText, &BoxRect);

    /* Draw status text */
    UiDrawStatusText("Press ENTER to continue, or ESC to cancel");

    editCtx.TextLength = (ULONG)strlen(EditTextBuffer);
    editCtx.TextLength = min(editCtx.TextLength, Length - 1);
    editCtx.TextPosition = 0;
    editCtx.Line = BoxRect.Bottom - 2;
    editCtx.StartX = BoxRect.Left + 3;
    editCtx.EndX = BoxRect.Right - 3;

    /* Show the cursor */
    editCtx.CursorX = editCtx.StartX;
    MachVideoHideShowTextCursor(TRUE);

    ReturnCode = (BOOLEAN)UiDispatch(TuiEditBoxProc, &editCtx);

    /* Hide the cursor again */
    MachVideoHideShowTextCursor(FALSE);

    /* Restore the screen contents */
    TuiRestoreScreen(ScreenBuffer);

    return ReturnCode;
}

/* EOF */
