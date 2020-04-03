/*
 * LICENSE:         GPL - See COPYING in the top level directory
 * PROJECT:         ReactOS Console Server DLL
 * FILE:            win32ss/user/winsrv/consrv/lineinput.h
 * PURPOSE:         Console line input functions
 * PROGRAMMERS:     Jeffrey Morlan
 */

#pragma once

// COOKED mode.
#if 0 // FIXME!!
typedef struct _LINE_EDIT_INFO
{
    PCONSOLE_INPUT_BUFFER InputBuffer;      // The input buffer corresponding to the handle.
    PCONSOLE_SCREEN_BUFFER ScreenBuffer; // PTEXTMODE_SCREEN_BUFFER // Reference to the SB we are echoing on.

    ULONG Mode;

    PHISTORY_BUFFER Hist;
    UNICODE_STRING ExeName; // Used for Aliases resolution.

    PWCHAR  LineBuffer;                     /* Current line being input, in line buffered mode */
    ULONG   LineMaxSize;                    /* Maximum size of line in characters (including CR+LF) */
    ULONG   LineSize;                       /* Current size of line */
    ULONG   LinePos;                        /* Current position within line */
    BOOLEAN LineComplete;                   /* User pressed enter, ready to send back to client */
    BOOLEAN LineUpPressed;
    BOOLEAN LineInsertToggle;               /* Replace character over cursor instead of inserting */
    ULONG   LineWakeupMask;                 /* Bitmap of which control characters will end line input */

} LINE_EDIT_INFO, *PLINE_EDIT_INFO;
#else
typedef struct _LINE_EDIT_INFO LINE_EDIT_INFO, *PLINE_EDIT_INFO;
#endif

VOID
LineInputKeyDown(
    IN PLINE_EDIT_INFO LineEditInfo,
    IN PKEY_EVENT_RECORD KeyEvent);
