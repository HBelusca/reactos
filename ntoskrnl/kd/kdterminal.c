/*
 * PROJECT:     ReactOS KDBG Kernel Debugger Terminal Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Terminal Management for the Kernel Debugger
 * COPYRIGHT:   Copyright 2005 Gregor Anich <blight@blight.eu.org>
 *              Copyright 2022-2023 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include "kd.h"
#include "kdterminal.h"

#define KdbpGetCharKeyboard(ScanCode) KdpTryGetCharKeyboard((ScanCode), 0)
CHAR
KdpTryGetCharKeyboard(
    _Out_ PULONG ScanCode,
    _In_ ULONG Retry);

#define KdbpGetCharSerial()  KdpTryGetCharSerial(0)
CHAR
KdpTryGetCharSerial(
    _In_ ULONG Retry);

VOID
KdpSendCommandSerial(
    _In_ PCSTR Command);

VOID
KbdDisableMouse(VOID);

VOID
KbdEnableMouse(VOID);


/* GLOBALS *******************************************************************/

static BOOLEAN
NTAPI
KdpTermUpdateSize(VOID);

/* KD Controlling Terminal */
KD_TERMINAL KdTerminal =
{
    {-1, -1}, FALSE, {2},
    KdpTermSetState,
    KdpTermUpdateSize,
    KdpTermReadKey
};
static LONG KdTermEnableCount = 0;     /* Terminal enabled counter */
static CHAR KdTermNextKey = ANSI_NULL; /* 1-character input queue buffer */


/* FUNCTIONS *****************************************************************/

/**
 * @brief   Initializes the controlling terminal.
 *
 * @return
 * TRUE if the controlling terminal is serial and detected
 * as being connected, or FALSE if not.
 **/
BOOLEAN
KdpTermInit(VOID)
{
    /* Determine whether the controlling terminal is a serial terminal:
     * serial output is enabled *and* KDSERIAL is set (i.e. user input
     * through serial). */
    KD_TERM.Serial =
#if 0
    // Old logic where KDSERIAL also enables serial output.
    KD_TERM.SerialInput ||
    (KdpDebugMode.Serial && !KdpDebugMode.Screen);
#else
    // New logic where KDSERIAL does not necessarily enable serial output.
    KdpDebugMode.Serial &&
    (KD_TERM.SerialInput || !KdpDebugMode.Screen);
#endif

    /* Flush the input buffer */
    KdpTermFlushInput();

    if (KD_TERM.Serial)
    {
        ULONG Length;

        /* Enable line-wrap */
        KdpSendCommandSerial("\x1b[?7h");

        /*
         * Query terminal type.
         * Historically it was done with CTRL-E ('\x05'), however nowadays
         * terminals respond to it with an empty (or a user-configurable)
         * string. Instead, use the VT52-compatible 'ESC Z' sequence or the
         * VT100-compatible 'ESC[c' one.
         */
        KdpSendCommandSerial("\x1b[c");
        KeStallExecutionProcessor(100000);

        Length = 0;
        for (;;)
        {
            /* Verify we get an answer, but don't care about it */
            if (KdpTryGetCharSerial(5000) == -1)
                break;
            ++Length;
        }

        /* Terminal is connected (TRUE) or not connected (FALSE) */
        KD_TERM.Connected = (Length > 0);
    }
    else
    {
        /* Terminal is not serial, assume it's *not* connected */
        KD_TERM.Connected = FALSE;
    }

    /* Retrieve the initial terminal size */
    KdpTermUpdateSize();

    return KD_TERM.Connected; // NOTE: Old code kind of returned "the last status".
}

static inline
VOID
KdpTermEnable(VOID)
{
    if (InterlockedIncrement(&KdTermEnableCount) == 1)
    {
        if (!KD_TERM.SerialInput)
            KbdDisableMouse();

        /* Take control of the display */
        if (KdpDebugMode.Screen)
            KdpScreenAcquire();
    }

    /* Update the terminal size */
    KdpTermUpdateSize();
}

static inline
VOID
KdpTermDisable(VOID)
{
    if (InterlockedDecrement(&KdTermEnableCount) == 0)
    {
        /* Release the display */
        if (KdpDebugMode.Screen)
            KdpScreenRelease();

        if (!KD_TERM.SerialInput)
            KbdEnableMouse();
    }

    /* Update the terminal size */
    KdpTermUpdateSize();
}

/**
 * @brief   Enables or disable the KD terminal.
 **/
VOID
NTAPI
KdpTermSetState(
    _In_ BOOLEAN Enable)
{
    if (Enable)
        KdpTermEnable();
    else
        KdpTermDisable();
}

/**
 * @brief   Retrieves the size of the controlling terminal.
 *
 * @return  TRUE if the terminal reports its size, FALSE if not.
 * @note    Defaults to standard 80x24 size if no size could be retrieved.
 **/
static BOOLEAN
NTAPI
KdpTermUpdateSize(VOID)
{
    static CHAR Buffer[128];
    CHAR c;
    LONG NumberOfCols = -1; // Or initialize to KD_TERM.Size.cx ??
    LONG NumberOfRows = -1; // Or initialize to KD_TERM.Size.cy ??

    /* Retrieve the size of the controlling terminal only when it is serial */
    if (KD_TERM.Connected && KD_TERM.Serial && KD_TERM.ReportsSize)
    {
        /* Flush the input buffer */
        KdpTermFlushInput();

        /* Try to query the terminal size. A reply looks like "\x1b[8;24;80t" */
        KD_TERM.ReportsSize = FALSE;
        KdpSendCommandSerial("\x1b[18t");
        KeStallExecutionProcessor(100000);

        c = KdpTryGetCharSerial(5000);
        if (c == KEY_ESC)
        {
            c = KdpTryGetCharSerial(5000);
            if (c == '[')
            {
                ULONG Length = 0;
                for (;;)
                {
                    c = KdpTryGetCharSerial(5000);
                    if (c == -1)
                        break;

                    Buffer[Length++] = c;
                    if (isalpha(c) || Length >= (sizeof(Buffer) - 1))
                        break;
                }
                Buffer[Length] = ANSI_NULL;

                if (Buffer[0] == '8' && Buffer[1] == ';')
                {
                    SIZE_T i;
                    for (i = 2; (i < Length) && (Buffer[i] != ';'); i++);

                    if (Buffer[i] == ';')
                    {
                        Buffer[i++] = ANSI_NULL;

                        /* Number of rows is now at Buffer + 2
                         * and number of columns at Buffer + i */
                        NumberOfRows = strtoul(Buffer + 2, NULL, 0);
                        NumberOfCols = strtoul(Buffer + i, NULL, 0);
                        KD_TERM.ReportsSize = TRUE;
                    }
                }
            }
            /* Clear further characters */
            while (KdpTryGetCharSerial(5000) != -1);
        }
    }

    if (NumberOfCols <= 0)
    {
        /* Set the number of columns to the default */
        if (KdpDebugMode.Screen && !KD_TERM.Serial)
            NumberOfCols = (SCREEN_WIDTH / 8 /*BOOTCHAR_WIDTH*/);
        else
            NumberOfCols = 80;
    }
    if (NumberOfRows <= 0)
    {
        /* Set the number of rows to the default */
        if (KdpDebugMode.Screen && !KD_TERM.Serial)
            NumberOfRows = (SCREEN_HEIGHT / (13 /*BOOTCHAR_HEIGHT*/ + 1));
        else
            NumberOfRows = 24;
    }

    KD_TERM.Size.cx = NumberOfCols;
    KD_TERM.Size.cy = NumberOfRows;

    // KdIoPrintf("Cols/Rows: %dx%d\n", KD_TERM.Size.cx, KD_TERM.Size.cy);

    return KD_TERM.ReportsSize;
}

/**
 * @brief   Flushes terminal input (either serial or PS/2).
 **/
VOID
NTAPI
KdpTermFlushInput(VOID)
{
    KdTermNextKey = ANSI_NULL;
    if (KD_TERM.SerialInput)
    {
        while (KdpTryGetCharSerial(1) != -1);
    }
    else
    {
        ULONG ScanCode;
        while (KdpTryGetCharKeyboard(&ScanCode, 1) != -1);
    }
}

/**
 * @brief
 * Reads one character from the terminal. This function returns
 * a scan code even when reading is done from a serial terminal.
 **/
CHAR
NTAPI
KdpTermReadKey(
    _Out_ PULONG ScanCode)
{
    CHAR Key;

    *ScanCode = 0;

    if (KD_TERM.SerialInput)
    {
        Key = (!KdTermNextKey ? KdbpGetCharSerial() : KdTermNextKey);
        KdTermNextKey = ANSI_NULL;
        if (Key == KEY_ESC) /* ESC */
        {
            Key = KdbpGetCharSerial();
            if (Key == '[')
            {
                Key = KdbpGetCharSerial();
                switch (Key)
                {
                    case 'A':
                        *ScanCode = KEY_SCAN_UP;
                        break;
                    case 'B':
                        *ScanCode = KEY_SCAN_DOWN;
                        break;
                    case 'C':
                        break;
                    case 'D':
                        break;
                }
            }
        }
    }
    else
    {
        Key = (!KdTermNextKey ? KdbpGetCharKeyboard(ScanCode) : KdTermNextKey);
        KdTermNextKey = ANSI_NULL;
    }

    /* Check for return */
    if (Key == '\r')
    {
        /*
         * We might need to discard the next '\n' which most clients
         * should send after \r. Wait a bit to make sure we receive it.
         */
        KeStallExecutionProcessor(100000);

        if (KD_TERM.SerialInput)
            KdTermNextKey = KdpTryGetCharSerial(5);
        else
            KdTermNextKey = KdpTryGetCharKeyboard(ScanCode, 5);

        if (KdTermNextKey == '\n' || KdTermNextKey == -1) /* \n or no response at all */
            KdTermNextKey = ANSI_NULL;
    }

    return Key;
}

/* EOF */
