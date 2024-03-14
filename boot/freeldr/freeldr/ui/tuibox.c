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
            temp[j] = 0;
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
    CHAR key;

    /* Draw the common parts of the message box */
    TuiDrawMsgBoxCommon(MessageText, &BoxRect);

    /* Draw centered OK button */
    UiDrawText((BoxRect.Left + BoxRect.Right) / 2 - 3,
               BoxRect.Bottom - 2,
               "   OK   ",
               ATTR(COLOR_BLACK, COLOR_GRAY));

    /* Draw status text */
    UiDrawStatusText("Press ENTER to continue");

    VideoCopyOffScreenBufferToVRAM();

    for (;;)
    {
        if (MachConsKbHit())
        {
            key = MachConsGetCh();
            if (key == KEY_EXTENDED)
                key = MachConsGetCh();

            if ((key == KEY_ENTER) || (key == KEY_SPACE) || (key == KEY_ESC))
                break;
        }

        TuiUpdateDateTime();

        VideoCopyOffScreenBufferToVRAM();

        MachHwIdle();
    }
}

BOOLEAN TuiEditBox(PCSTR MessageText, PCHAR EditTextBuffer, ULONG Length)
{
    CHAR    key;
    BOOLEAN Extended;
    INT     EditBoxLine;
    ULONG   EditBoxStartX, EditBoxEndX;
    INT     EditBoxCursorX;
    ULONG   EditBoxTextLength, EditBoxTextPosition;
    INT     EditBoxTextDisplayIndex;
    BOOLEAN ReturnCode;
    SMALL_RECT BoxRect;
    PVOID ScreenBuffer;

    /* Save the screen contents */
    ScreenBuffer = TuiSaveScreen();

    /* Draw the common parts of the message box */
    TuiDrawMsgBoxCommon(MessageText, &BoxRect);

    EditBoxTextLength = (ULONG)strlen(EditTextBuffer);
    EditBoxTextLength = min(EditBoxTextLength, Length - 1);
    EditBoxTextPosition = 0;
    EditBoxLine = BoxRect.Bottom - 2;
    EditBoxStartX = BoxRect.Left + 3;
    EditBoxEndX = BoxRect.Right - 3;

    // Draw the edit box background and the text
    UiFillArea(EditBoxStartX, EditBoxLine, EditBoxEndX, EditBoxLine, ' ', ATTR(UiEditBoxTextColor, UiEditBoxBgColor));
    UiDrawText2(EditBoxStartX, EditBoxLine, EditBoxEndX - EditBoxStartX + 1, EditTextBuffer, ATTR(UiEditBoxTextColor, UiEditBoxBgColor));

    // Show the cursor
    EditBoxCursorX = EditBoxStartX;
    MachVideoSetTextCursorPosition(EditBoxCursorX, EditBoxLine);
    MachVideoHideShowTextCursor(TRUE);

    // Draw status text
    UiDrawStatusText("Press ENTER to continue, or ESC to cancel");

    VideoCopyOffScreenBufferToVRAM();

    //
    // Enter the text. Please keep in mind that the default input mode
    // of the edit boxes is in insertion mode, that is, you can insert
    // text without erasing the existing one.
    //
    for (;;)
    {
        if (MachConsKbHit())
        {
            Extended = FALSE;
            key = MachConsGetCh();
            if (key == KEY_EXTENDED)
            {
                Extended = TRUE;
                key = MachConsGetCh();
            }

            if (key == KEY_ENTER)
            {
                ReturnCode = TRUE;
                break;
            }
            else if (key == KEY_ESC)
            {
                ReturnCode = FALSE;
                break;
            }
            else if (key == KEY_BACKSPACE) // Remove a character
            {
                if ( (EditBoxTextLength > 0) && (EditBoxTextPosition > 0) &&
                     (EditBoxTextPosition <= EditBoxTextLength) )
                {
                    EditBoxTextPosition--;
                    memmove(EditTextBuffer + EditBoxTextPosition,
                            EditTextBuffer + EditBoxTextPosition + 1,
                            EditBoxTextLength - EditBoxTextPosition);
                    EditBoxTextLength--;
                    EditTextBuffer[EditBoxTextLength] = 0;
                }
                else
                {
                    MachBeep();
                }
            }
            else if (Extended && key == KEY_DELETE) // Remove a character
            {
                if ( (EditBoxTextLength > 0) &&
                     (EditBoxTextPosition < EditBoxTextLength) )
                {
                    memmove(EditTextBuffer + EditBoxTextPosition,
                            EditTextBuffer + EditBoxTextPosition + 1,
                            EditBoxTextLength - EditBoxTextPosition);
                    EditBoxTextLength--;
                    EditTextBuffer[EditBoxTextLength] = 0;
                }
                else
                {
                    MachBeep();
                }
            }
            else if (Extended && key == KEY_HOME) // Go to the start of the buffer
            {
                EditBoxTextPosition = 0;
            }
            else if (Extended && key == KEY_END) // Go to the end of the buffer
            {
                EditBoxTextPosition = EditBoxTextLength;
            }
            else if (Extended && key == KEY_RIGHT) // Go right
            {
                if (EditBoxTextPosition < EditBoxTextLength)
                    EditBoxTextPosition++;
                else
                    MachBeep();
            }
            else if (Extended && key == KEY_LEFT) // Go left
            {
                if (EditBoxTextPosition > 0)
                    EditBoxTextPosition--;
                else
                    MachBeep();
            }
            else if (!Extended) // Add this key to the buffer
            {
                if ( (EditBoxTextLength   < Length - 1) &&
                     (EditBoxTextPosition < Length - 1) )
                {
                    memmove(EditTextBuffer + EditBoxTextPosition + 1,
                            EditTextBuffer + EditBoxTextPosition,
                            EditBoxTextLength - EditBoxTextPosition);
                    EditTextBuffer[EditBoxTextPosition] = key;
                    EditBoxTextPosition++;
                    EditBoxTextLength++;
                    EditTextBuffer[EditBoxTextLength] = 0;
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
        }

        // Draw the edit box background
        UiFillArea(EditBoxStartX, EditBoxLine, EditBoxEndX, EditBoxLine, ' ', ATTR(UiEditBoxTextColor, UiEditBoxBgColor));

        // Fill the text in
        if (EditBoxTextPosition > (EditBoxEndX - EditBoxStartX))
        {
            EditBoxTextDisplayIndex = EditBoxTextPosition - (EditBoxEndX - EditBoxStartX);
            EditBoxCursorX = EditBoxEndX;
        }
        else
        {
            EditBoxTextDisplayIndex = 0;
            EditBoxCursorX = EditBoxStartX + EditBoxTextPosition;
        }
        UiDrawText2(EditBoxStartX, EditBoxLine, EditBoxEndX - EditBoxStartX + 1, &EditTextBuffer[EditBoxTextDisplayIndex], ATTR(UiEditBoxTextColor, UiEditBoxBgColor));

        // Move the cursor
        MachVideoSetTextCursorPosition(EditBoxCursorX, EditBoxLine);

        TuiUpdateDateTime();

        VideoCopyOffScreenBufferToVRAM();

        MachHwIdle();
    }

    // Hide the cursor again
    MachVideoHideShowTextCursor(FALSE);

    /* Restore the screen contents */
    TuiRestoreScreen(ScreenBuffer);

    return ReturnCode;
}

/* EOF */
