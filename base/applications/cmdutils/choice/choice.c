/*
 * PROJECT:     ReactOS Choice Command
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     NT-compatible version of the Choice command.
 * COPYRIGHT:   Copyright 2025 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#include <stdio.h>
#include <stdlib.h> // EXIT_SUCCESS, EXIT_FAILURE

#include <windef.h>
#include <winbase.h>
#include <wincon.h>
/// #include <winuser.h>

#include <conutils.h>

#include "resource.h"

/* ERRORLEVEL value when an error condition is detected */
#define CHOICE_EXIT_FAILURE 0xFF

VOID PrintError(
    _In_ DWORD dwError)
{
    if (dwError == ERROR_SUCCESS)
        return;

    ConMsgPuts(StdErr, FORMAT_MESSAGE_FROM_SYSTEM,
               NULL, dwError, LANG_USER_DEFAULT);
    ConPuts(StdErr, L"\n");
}

BOOL
WINAPI
CtrlCIntercept(
    _In DWORD dwCtrlType)
{
    switch (dwCtrlType)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
        ConPuts(StdOut, L"\n");
        SetConsoleCtrlHandler(NULL, FALSE);
        /* The user pressed Ctrl-C or Ctrl-Break, set the ERRORLEVEL to 0 */
        ExitProcess(EXIT_SUCCESS);
        return TRUE;
    }
    return FALSE;
}


/**
 * @brief   Case-insensitive wcschr().
 **/
static wchar_t*
wcsichr(const wchar_t* str, wchar_t c)
{
    for (; *str; ++str)
    {
        if (towlower(*str) == towlower(c))
            return (wchar_t*)str;
    }
    return NULL;
}

static INT
IsKeyInString(PCWSTR pString, WCHAR cKey, BOOL bCaseSensitive)
{
    PWSTR key = (bCaseSensitive ? wcschr : wcsichr)(pString, cKey);
    if (!key)
        return CHOICE_EXIT_FAILURE;
    return (key - pString);
}

INT InputWait(
    _In_ INT timerValue,
    _In_ PCWSTR pOptions,
    _In_ WCHAR cDefault,
    _In_ BOOL bCaseSensitive)
{
    INT Status = EXIT_SUCCESS;
    HANDLE hInput;
    BOOL bUseTimer = (timerValue != -1);
    DWORD dwStartTime;
    LONG timeElapsed;
    DWORD dwWaitState;
    INPUT_RECORD InputRecords[5];
    ULONG NumRecords, i;
    BOOL DisplayMsg = TRUE;

    /* Retrieve the current input handle */
    hInput = ConStreamGetOSHandle(StdIn);
    if (hInput == INVALID_HANDLE_VALUE)
    {
        ConResPrintf(StdErr, IDS_ERROR_INVALID_HANDLE_VALUE, GetLastError());
        return CHOICE_EXIT_FAILURE;
    }

    /* Start a new wait if we use the timer */
    if (bUseTimer)
        dwStartTime = GetTickCount();

    /* Monitor for Ctrl-C or Ctrl-Break input */
    SetConsoleCtrlHandler(CtrlCIntercept, TRUE);

    /* Initially flush the console input queue to remove any pending events */
    if (!GetNumberOfConsoleInputEvents(hInput, &NumRecords) ||
        !FlushConsoleInputBuffer(hInput))
    {
        /* A problem happened, bail out */
        PrintError(GetLastError());
        Status = CHOICE_EXIT_FAILURE;
        goto Quit;
    }

    while (TRUE)
    {
        /* Decrease the timer if we use it */
        if (bUseTimer)
        {
            /*
             * Compute how much time the previous operations took.
             * This allows us in particular to take account for any time
             * elapsed if something slowed down, or if the console has been
             * paused in the meantime.
             */
            timeElapsed = GetTickCount() - dwStartTime;
            if (timeElapsed >= 1000)
            {
                /* Increase dwStartTime by steps of 1 second */
                timeElapsed /= 1000;
                dwStartTime += (1000 * timeElapsed);

                if (timeElapsed <= timerValue)
                    timerValue -= timeElapsed;
                else
                    timerValue = 0;

                DisplayMsg = TRUE;
            }

#if 1 // DEBUG
            if (DisplayMsg)
            {
                CHAR TmpBuf[30];
                sprintf(TmpBuf, "Timeout: %d\n", timerValue);
                OutputDebugStringA(TmpBuf);

                DisplayMsg = FALSE;
            }
#endif
            /* Stop when the timer reaches zero */
            if (timerValue <= 0)
                break;
        }

        /* /NOBREAK is not used, check for user key presses */

        /*
         * If the timer is used, use a passive wait of maximum 1 second
         * while monitoring for incoming console input events, so that
         * we are still able to display the timing count.
         * Indeed, ReadConsoleInputW() indefinitely waits until an input
         * event appears. ReadConsoleInputW() is however used to retrieve
         * the input events where there are some, as well as for waiting
         * indefinitely in case we do not use the timer.
         */
        if (bUseTimer)
        {
            /* Wait a maximum of 1 second for input events */
            timeElapsed = GetTickCount() - dwStartTime;
            if (timeElapsed < 1000)
                dwWaitState = WaitForSingleObject(hInput, 1000 - timeElapsed);
            else
                dwWaitState = WAIT_TIMEOUT;

            /* Check whether the input event has been signaled, or a timeout happened */
            if (dwWaitState == WAIT_TIMEOUT)
                continue;
            if (dwWaitState != WAIT_OBJECT_0)
            {
                /* An error happened, bail out */
                PrintError(GetLastError());
                Status = CHOICE_EXIT_FAILURE;
                break;
            }

            /* Be sure there is something in the console input queue */
            if (!PeekConsoleInputW(hInput, InputRecords, ARRAYSIZE(InputRecords), &NumRecords))
            {
                /* An error happened, bail out */
                ConResPrintf(StdErr, IDS_ERROR_READ_INPUT, GetLastError());
                Status = CHOICE_EXIT_FAILURE;
                break;
            }

            if (NumRecords == 0)
                continue;
        }

        /*
         * Some events have been detected, pop them out from the input queue.
         * In case we do not use the timer, wait indefinitely until an input
         * event appears.
         */
        if (!ReadConsoleInputW(hInput, InputRecords, ARRAYSIZE(InputRecords), &NumRecords))
        {
            /* An error happened, bail out */
            ConResPrintf(StdErr, IDS_ERROR_READ_INPUT, GetLastError());
            Status = CHOICE_EXIT_FAILURE;
            break;
        }

        /* Check the input events for a key press */
        for (i = 0; i < NumRecords; ++i)
        {
            /* Ignore any non-key event */
            if (InputRecords[i].EventType != KEY_EVENT)
                continue;

            /* Ignore any system key event */
            if ((InputRecords[i].Event.KeyEvent.wVirtualKeyCode == VK_CONTROL) ||
             // (InputRecords[i].Event.KeyEvent.dwControlKeyState & (LEFT_ALT_PRESSED  | RIGHT_ALT_PRESSED )) ||
             // (InputRecords[i].Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) ||
                (InputRecords[i].Event.KeyEvent.wVirtualKeyCode == VK_MENU))
            {
                continue;
            }

            /* This is a non-system key event, check whether it's part
             * of the options. If so, retrieve its index and stop waiting. */
            Status = IsKeyInString(pOptions, InputRecords[i].Event.KeyEvent.uChar.UnicodeChar, bCaseSensitive);
            if (Status != CHOICE_EXIT_FAILURE)
            {
                // cDefault = InputRecords[i].Event.KeyEvent.uChar.UnicodeChar;
                cDefault = pOptions[Status];
                goto Stop;
            }
            Beep(1500, 500); // Invalid choice, beep the user.
            break;
        }
    }

Stop:
    /* Echo the default or user-selected choice */
    ConPrintf(StdOut, L"%c\n", cDefault);

Quit:
    SetConsoleCtrlHandler(NULL, FALSE);
    return Status;
}

int wmain(int argc, WCHAR* argv[])
{
    PWSTR pOptions;
    WCHAR Options[6];

    BOOL  bHasHelp = FALSE;
    BOOL  bNoPrompt = FALSE;
    BOOL  bCaseSensitive = FALSE;
    // BOOL  bTimeout = FALSE;
    INT   nTimeout = -1;
    PWSTR pcDefault = L"";
    PWSTR pText = NULL;

    enum
    {
        HELP_PARAM = 0,
        HIDE_PARAM,
        CASESENS_PARAM,
        CHOICES_VALUE,
        DELAY_VALUE,
        DEFAULT_VALUE,
        PROMPT_VALUE
        NONE,
    } fValue; // Correlates with options array indices below.
    struct
    {
        PCWSTR OptionName;
        BOOL IsValue:1;
        BOOL IsSet:1;
    } Options[] =
    {
        {L"?" , FALSE, FALSE},
        {L"N" , FALSE, FALSE},
        {L"CS", FALSE, FALSE},
        {L"C" , TRUE , FALSE},
        {L"T" , TRUE , FALSE},
        {L"D" , TRUE , FALSE},
        {L"M" , TRUE , FALSE},
    };

    PCWSTR pCurArg = NULL;
    int index;

    /* Initialize the Console Standard Streams */
    ConInitStdStreams();

    /* Load the default localized options */
    K32LoadStringW(NULL, STRING_CHOICE_OPTION, Options, _countof(Options));
    pOptions = Options;

    /* Parse the command line for options */
    /// pCurArg = NULL;
    for (index = 1; index < argc; ++index)
    {
        PCWSTR arg = argv[index];

        if (arg[0] == L'-' || arg[0] == L'/')
        {
            PCWCH ArgNameSep;
            pCurArg = arg;

            /* Lookup for the option in the list of options */
            for (fValue = 0; fValue < _countof(Options); ++fValue)
            {
                PCWSTR OptionName = Options[fValue].OptionName;
                SIZE_T NameLength = wcslen(OptionName);

                ArgNameSep = &(arg+1)[NameLength];

                // if (_wcsicmp(arg+1, Options[fValue].OptionName) == 0)
                if ((_wcsnicmp(arg+1, OptionName, NameLength) == 0) &&
                    (*ArgNameSep == L':' || !*ArgNameSep))
                {
                    break;
                }
            }
            if (fValue >= _countof(Options))
            {
                ConResMsgPrintf(StdErr, 0, STRING_INVALID_OPTION, arg);
                ConResMsgPuts(StdErr, 0, IDS_USAGE);
                return CHOICE_EXIT_FAILURE;
            }

            /* Check whether the option was already specified */
            if (Options[fValue].IsSet)
            {
                ConResMsgPrintf(StdErr, 0, IDS_OPTION_TOO_MUCH, arg, 1);
                ConResMsgPuts(StdErr, 0, IDS_USAGE);
                return CHOICE_EXIT_FAILURE;
            }
            Options[fValue].IsSet = TRUE;


            if (fValue == HELP_PARAM) /* Help */
            {
                bHasHelp = TRUE;
                continue;
            }
            else if (fValue == HIDE_PARAM) /* Hide choice list */
            {
                bNoPrompt = TRUE;
                continue;
            }
            else if (fValue == CASESENS_PARAM) /* Case-sensitive */
            {
                bCaseSensitive = TRUE;
                continue;
            }
            /* Must be one of these */
            else if (fValue == CHOICES_VALUE ||
                     fValue == DELAY_VALUE   ||
                     fValue == DEFAULT_VALUE ||
                     fValue == PROMPT_VALUE)
                // if (*ArgNameSep == L':' || !*ArgNameSep)
            {
                if (*ArgNameSep == L':')
                    arg = ArgNameSep+1; /* The value follows */
                else
                    continue; /* Go to the value following */
            }
            else
            {
                // ASSERT(FALSE);
                ConResMsgPrintf(StdErr, 0, STRING_INVALID_OPTION, arg);
                ConResMsgPuts(StdErr, 0, IDS_USAGE);
                return CHOICE_EXIT_FAILURE;
            }
        }


        /* Determine which value to read */
        switch (fValue)
        {
            case NONE:
                break;
            case CHOICES_VALUE: /* Choices list */
            {
                pOptions = arg;
                if (!*pOptions)
                {
                    ConResMsgPrintf(StdErr, 0, STRING_MISSING_PARAM, pCurArg);
                    ConResMsgPuts(StdErr, 0, IDS_USAGE);
                    return CHOICE_EXIT_FAILURE;
                }
                break;
            }
            case DELAY_VALUE: /* Delay */
            {
                PWCHAR pszNext;
                nTimeout = wcstol(arg, &pszNext, 10);
                if (*pszNext)
                {
                    ConResPuts(StdErr, IDS_ERROR_OUT_OF_RANGE); // STRING_INVALID_PARAM
                    ConResMsgPuts(StdErr, 0, IDS_USAGE);
                    return CHOICE_EXIT_FAILURE;
                }
                break;
            }
            case DEFAULT_VALUE: /* Default choice */
            {
                pcDefault = arg;
                if (!*pcDefault)
                {
                    ConResMsgPrintf(StdErr, 0, STRING_MISSING_PARAM, pCurArg);
                    ConResMsgPuts(StdErr, 0, IDS_USAGE);
                    return CHOICE_EXIT_FAILURE;
                }
                break;
            }
            case PROMPT_VALUE: /* Prompt */
            {
                pText = arg;
                break;
            }
        }

        /* Reset */
        fValue = _countof(Options);
    }


    if (bHasHelp)
    {
        if (argc > 2) /* Any arguments other than -? exists */
        {
            ConResMsgPrintf(StdErr, 0, IDS_INVALID_SYNTAX);
            ConResMsgPuts(StdErr, 0, IDS_USAGE);
            return CHOICE_EXIT_FAILURE;
        }
        else
        {
            ConResMsgPrintf(StdOut, 0, IDS_HELP);
            return CHOICE_EXIT_FAILURE;
        }
    }


    // TODO: Check whether there are duplicates
    // in the options and if so, throw an error.

    /* Verify for invalid characters. Only supported characters are:
     * 0-9, a-z, A-Z, and ASCII characters from 128 to 254 */
    PCWCH pch;
    for (pch = pOptions; *pch; ++pch)
    {
        if (!iswalnum(*pch) && !(128 <= *pch && *pch <= 254))
        {
            ConResPuts(StdErr, STRING_CHOICE_ERROR);
            ConResMsgPuts(StdErr, 0, IDS_USAGE);
            return CHOICE_EXIT_FAILURE;
        }
    }


    /* The default choice must only have one single character */
    if (pcDefault[0] && pcDefault[1])
    {
        ConResPuts(StdErr, STRING_CHOICE_ERROR_TXT); // FIXME
        ConResMsgPuts(StdErr, 0, IDS_USAGE);
        return CHOICE_EXIT_FAILURE;
    }

    /* The default choice must be in the choices list */
    if (!wcschr(pOptions, pcDefault[0]))
    {
        ConResPuts(StdErr, STRING_CHOICE_ERROR_TXT); // FIXME
        ConResMsgPuts(StdErr, 0, IDS_USAGE);
        return CHOICE_EXIT_FAILURE;
    }


    /* Make sure the timeout value is within range */
    if ((nTimeout < 0) || (nTimeout > 9999))
    {
        ConResMsgPrintf(StdErr, 0, IDS_ERROR_OUT_OF_RANGE, 0, 9999);
        ConResMsgPuts(StdErr, 0, IDS_USAGE);
        return CHOICE_EXIT_FAILURE;
    }



#if 0
    ConPrintf(StdOut, L"%s%s[%s]?",
              pText ? pText : L"",
              pText ? L" " : L"",
              pOptions);
#else
    /* Print text */
    if (pText)
        ConPrintf(StdOut, L"%s ", pText);

    /* Print options */
    if (!bNoPrompt)
    {
        PCWCH pch;
        ConPuts(StdOut, L"[%c", pOptions[0]);
        for (pch = &pOptions[1]; *pch; ++pch)
            ConPuts(StdOut, L",%c", *pch);
        ConPuts(StdOut, L"]?");
    }
#endif

    /* Wait for user input and return the choice */
    return InputWait(nTimeout, pOptions, pcDefault[0], bCaseSensitive);
}

/* EOF */
