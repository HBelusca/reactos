/*
 *  CMDINPUT.C - handles command input (tab completion, history, etc.).
 *
 *
 *  History:
 *
 *    01/14/95 (Tim Norman)
 *        started.
 *
 *    08/08/95 (Matt Rains)
 *        i have cleaned up the source code. changes now bring this source
 *        into guidelines for recommended programming practice.
 *        i have added some constants to help making changes easier.
 *
 *    12/12/95 (Tim Norman)
 *        added findxy() function to get max x/y coordinates to display
 *        correctly on larger screens
 *
 *    12/14/95 (Tim Norman)
 *        fixed the Tab completion code that Matt Rains broke by moving local
 *        variables to a more global scope and forgetting to initialize them
 *        when needed
 *
 *    8/1/96 (Tim Norman)
 *        fixed a bug in tab completion that caused filenames at the beginning
 *        of the command-line to have their first letter truncated
 *
 *    9/1/96 (Tim Norman)
 *        fixed a silly bug using printf instead of fputs, where typing "%i"
 *        confused printf :)
 *
 *    6/14/97 (Steffan Kaiser)
 *        ctrl-break checking
 *
 *    6/7/97 (Marc Desrochers)
 *        recoded everything! now properly adjusts when text font is changed.
 *        removed findxy(), reposition(), and reprint(), as these functions
 *        were inefficient. added goxy() function as gotoxy() was buggy when
 *        the screen font was changed. the printf() problem with %i on the
 *        command line was fixed by doing printf("%s",str) instead of
 *        printf(str). Don't ask how I find em just be glad I do :)
 *
 *    7/12/97 (Tim Norman)
 *        Note: above changes preempted Steffan's ctrl-break checking.
 *
 *    7/7/97 (Marc Desrochers)
 *        rewrote a new findxy() because the new dir() used it.  This
 *        findxy() simply returns the values of *maxx *maxy.  In the
 *        future, please use the pointers, they will always be correct
 *        since they point to BIOS values.
 *
 *    7/8/97 (Marc Desrochers)
 *        once again removed findxy(), moved the *maxx, *maxy pointers
 *        global and included them as externs in command.h.  Also added
 *        insert/overstrike capability
 *
 *    7/13/97 (Tim Norman)
 *        added different cursor appearance for insert/overstrike mode
 *
 *    7/13/97 (Tim Norman)
 *        changed my code to use _setcursortype until I can figure out why
 *        my code is crashing on some machines.  It doesn't crash on mine :)
 *
 *    27-Jul-1998 (John P Price <linux-guru@gcfl.net>)
 *        added config.h include
 *
 *    28-Jul-1998 (John P Price <linux-guru@gcfl.net>)
 *        put ifdef's around filename completion code.
 *
 *    30-Jul-1998 (John P Price <linux-guru@gcfl.net>)
 *        moved filename completion code to filecomp.c
 *        made second TAB display list of filename matches
 *
 *    31-Jul-1998 (John P Price <linux-guru@gcfl.net>)
 *        Fixed bug where if you typed something, then hit HOME, then tried
 *        to type something else in insert mode, it crashed.
 *
 *    07-Aug-1998 (John P Price <linux-guru@gcfl.net>)
 *        Fixed carriage return output to better match MSDOS with echo
 *        on or off.(marked with "JPP 19980708")
 *
 *    13-Dec-1998 (Eric Kohl)
 *        Added insert/overwrite cursor.
 *
 *    25-Jan-1998 (Eric Kohl)
 *        Replaced CRT io functions by Win32 console io functions.
 *        This can handle <Shift>-<Tab> for 4NT filename completion.
 *        Unicode and redirection safe!
 *
 *    04-Feb-1999 (Eric Kohl)
 *        Fixed input bug. A "line feed" character remained in the keyboard
 *        input queue when you pressed <RETURN>. This sometimes caused
 *        some very strange effects.
 *        Fixed some command line editing annoyances.
 *
 *    30-Apr-2004 (Filip Navara <xnavara@volny.cz>)
 *        Fixed problems when the screen was scrolled away.
 *
 *    28-September-2007 (Hervé Poussineau)
 *        Added history possibilities to right key.
 */

#include "precomp.h"

#include <strsafe.h>

/*
 * Inserts a NULL-terminated string pszSrc starting position nPos
 * into the NULL-terminated string pszDest of maximum buffer size cchDest.
 * Note that this a generalization of StringCchCat(), where in the latter case
 * the string is "inserted" (appended) at the end.
 */
HRESULT
StringCchInsertStringEx(
    _Inout_   LPTSTR  pszDest,
    _In_      size_t  cchDest,
    _In_      LPCTSTR pszSrc,
    _In_      size_t  nPos,
    _Out_opt_ LPTSTR  *ppszDestEnd,
    _Out_opt_ size_t  *pcchRemaining
    // , _In_      DWORD   dwFlags
    )
{
    size_t cchSrc = _tcslen(pszSrc);
    size_t cchDestOld = _tcslen(pszDest);

    /* Parameters validation */
    if ((cchDest == 0) || (cchDest > STRSAFE_MAX_CCH))
        return STRSAFE_E_INVALID_PARAMETER;
    if (nPos > cchDestOld)
        return STRSAFE_E_INVALID_PARAMETER;
    if (cchDestOld + cchSrc + 1 > cchDest)
        return STRSAFE_E_INSUFFICIENT_BUFFER;

    /* Perform the insertion */
    pszDest += nPos;
    memmove(pszDest + cchSrc, pszDest,
            (cchDestOld + 1) * sizeof(TCHAR));
    memmove(pszDest, pszSrc,
            cchSrc * sizeof(TCHAR));

    if (ppszDestEnd)
        *ppszDestEnd = pszDest + cchSrc;
    if (pcchRemaining)
        *pcchRemaining = cchDest - cchSrc;

    return S_OK;
}

HRESULT
StringCchInsertString(
    _Inout_ LPTSTR  pszDest,
    _In_    size_t  cchDest,
    _In_    LPCTSTR pszSrc,
    _In_    size_t  nPos)
{
    return StringCchInsertStringEx(pszDest, cchDest,
                                   pszSrc, nPos,
                                   NULL, NULL);
}




/* Set to TRUE to use BASH-like completion instead of default CMD one */
BOOL bUseBashCompletion = FALSE;

/*
 * See https://technet.microsoft.com/en-us/library/cc978715.aspx
 * and https://technet.microsoft.com/en-us/library/cc940805.aspx
 * to know the differences between those two settings.
 * Values 0x00, 0x0D (carriage return) and 0x20 (space) disable completion.
 */
TCHAR AutoCompletionChar = _T('\t'); // Default is 0x20
TCHAR PathCompletionChar = _T('\t'); // Default is 0x20

// FIXME: Those are globals, that's bad!!
SHORT maxx;
SHORT maxy;
// COORD max;

static VOID
ClearLine(LPTSTR str, DWORD maxlen, COORD org)
{
    DWORD count;

    SetCursorXY(org.X, org.Y);
    for (count = 0; count < _tcslen(str); count++)
        ConOutChar(_T(' '));
    SetCursorXY(org.X, org.Y);

    _tcsnset(str, _T('\0'), maxlen);
}

static VOID
PrintPartialLine(
    IN LPTSTR str,
    IN DWORD charcount,
    IN DWORD tempscreen,
    IN OUT PCOORD org)
{
    COORD cur;
    DWORD count;
    // DWORD current;
    // current = charcount;

    // tempscreen == old_charcount

    /* Print out what we have now */
    SetCursorXY(org->X, org->Y);
    ConOutPuts(str);

    /* Move cursor accordingly */
    if (tempscreen > charcount)
    {
        GetCursorXY(&cur.X, &cur.Y);
        for (count = tempscreen - charcount; count--; )
            ConOutChar(_T(' '));
        SetCursorXY(cur.X, cur.Y);
    }
    else
    {
        if (((charcount + org->X) / maxx) + org->Y > maxy - 1)
            org->Y += maxy - ((charcount + org->X) / maxx + org->Y + 1);
    }
}

static BOOL
ReadLineFromFile(
    IN HANDLE hInput,
    // IN HANDLE hOutput,
    IN OUT LPTSTR str,
    IN DWORD maxlen)
{
    DWORD dwRead;
    CHAR chr;
    DWORD charcount = 0;/*chars in the string (str)*/

    do
    {
        if (!ReadFile(hInput, &chr, 1, &dwRead, NULL) || !dwRead)
            return FALSE;
// #ifdef _UNICODE
        // MultiByteToWideChar(/*InputCodePage*/ CP_UTF8, 0, &chr, 1, &str[charcount++], 1);
// #else
        str[charcount++] = chr;
// #endif
        // /***/ConOutChar(str[charcount-1]);/***/
        ConOutChar(chr);
    } while (chr != '\n' && charcount < maxlen); // || chr != '\r'

    str[charcount] = _T('\0');

    return TRUE;
}


typedef BOOL (*READ_CONSOLE_LINE)(
    IN HANDLE hConsoleInput,
    IN HANDLE hConsoleOutput,
    OUT LPTSTR lpBuffer,
    IN DWORD nNumberOfCharsToRead,
    OUT PDWORD lpNumberOfCharsRead,
    IN PCONSOLE_READCONSOLE_CONTROL pInputControl OPTIONAL,
    IN DWORD nInsertPoint OPTIONAL,
    OUT PDWORD pControlPoint OPTIONAL,
    OUT PTCHAR pControlChar OPTIONAL);

/* Equivalent of ReadConsoleLine() and of the Win32 ReadConsole() API, adapted for TTY */
BOOL
ReadTTYLine(
    IN HANDLE hConsoleInput,
    IN HANDLE hConsoleOutput,
    OUT LPTSTR lpBuffer,
    IN DWORD nNumberOfCharsToRead,
    OUT PDWORD lpNumberOfCharsRead,
    IN PCONSOLE_READCONSOLE_CONTROL pInputControl OPTIONAL,
    IN DWORD nInsertPoint OPTIONAL,
    OUT PDWORD pControlPoint OPTIONAL,
    OUT PTCHAR pControlChar OPTIONAL)
{
    // BOOL Success;

    LPTSTR str = (LPTSTR)lpBuffer;
    DWORD maxlen = nNumberOfCharsToRead;

    CONSOLE_READCONSOLE_CONTROL InputControl;

    /* Screen information */
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    COORD org;  /* origin x/y */
    COORD cur;  /* current x/y cursor position */

    DWORD tempscreen; // FIXME!
    DWORD count;        /* used in some for loops */
    DWORD current;      /* the position of the cursor in the string (str) */
    DWORD charcount;    /* chars in the string (str) */
    KEY_EVENT_RECORD KeyEvent;
    DWORD dwControlKeyState;

    TCHAR ch;
    BOOL bReturn = FALSE;
    BOOL bCharInput;
#ifdef FEATURE_HISTORY
    //BOOL bContinue=FALSE;/*is TRUE the second case will not be executed*/
    TCHAR PreviousChar;
#endif

/* Global command line insert/overwrite flag */
static BOOL bInsert = TRUE;

    InputControl.nLength = sizeof(InputControl);
    InputControl.nInitialChars = 0;
#ifdef _UNICODE
    if (pInputControl)
        InputControl = *pInputControl;
#endif

    /* The value of nInitialChars must be less than nNumberOfCharsToRead */
    InputControl.nInitialChars = min(InputControl.nInitialChars, nNumberOfCharsToRead);
    charcount = InputControl.nInitialChars;

    /* The insertion position must be less than the number of characters kept in the string */
    nInsertPoint = min(nInsertPoint, InputControl.nInitialChars);
    current = nInsertPoint;

    /* Get screen size and other info */
    if (!ConGetScreenInfo(&StdOutScreen, &csbi,
                          CON_SCREEN_SBSIZE | CON_SCREEN_CURSORPOS))
    {
        csbi.dwSize.X = 80;
        csbi.dwSize.Y = 25;
        csbi.dwCursorPosition.X = csbi.dwCursorPosition.Y = 0;
    }
    maxx = csbi.dwSize.X;
    maxy = csbi.dwSize.Y;

    org = csbi.dwCursorPosition; // InitialCursorPos.{X/Y}
    // InitialCursorPos = csbi.dwCursorPosition;
    {
    LONG XY = ((LONG)org.Y * maxx + (LONG)org.X) - (LONG)InputControl.nInitialChars;
    XY = min(max(XY, 0), (LONG)maxx * maxy - 1);

    org.X = XY % maxx;
    org.Y = XY / maxx;
    }

    cur = org;

#if 1
    /* Reset cursor position */
    SetCursorXY((org.X + current) % maxx,
                org.Y + (org.X + current) / maxx);
    GetCursorXY(&cur.X, &cur.Y);
#endif


    /* Initialize the control character values */
    if (pControlPoint)
        *pControlPoint = 0;
    if (pControlChar)
        *pControlChar = _T('\0');

    SetCursorType(bInsert, TRUE);

    do
    {
        bReturn = FALSE;
        ConInKey(&KeyEvent);

        dwControlKeyState = KeyEvent.dwControlKeyState;

        if ( dwControlKeyState &
             (RIGHT_ALT_PRESSED |LEFT_ALT_PRESSED|
              RIGHT_CTRL_PRESSED|LEFT_CTRL_PRESSED) )
        {
            switch (KeyEvent.wVirtualKeyCode)
            {
#ifdef FEATURE_HISTORY
                case _T('K'):
                {
                    /* Add the current command line to the history */
                    if (dwControlKeyState &
                        (LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED))
                    {
                        if (str[0])
                            History(0,str);

                        ClearLine(str, maxlen, org);
                        current = charcount = 0;
                        cur = org;
                        //bContinue=TRUE;
                        break;
                    }
                    // FIXME: Fallback??
                }

                case _T('D'):
                {
                    /* Delete current history entry */
                    if (dwControlKeyState &
                        (LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED))
                    {
                        ClearLine(str, maxlen, org);
                        History_del_current_entry(str);
                        current = charcount = _tcslen(str);
                        ConOutPuts(str);
                        GetCursorXY(&cur.X, &cur.Y);
                        //bContinue=TRUE;
                        break;
                    }
                    // FIXME: Fallback??
                }

#endif /*FEATURE_HISTORY*/

                case _T('M'):
                {
                    /* ^M does the same as return */
                    if (dwControlKeyState &
                        (LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED))
                    {
                        /* End input, return to main */
#ifdef FEATURE_HISTORY
                        /* Add to the history */
                        if (str[0])
                            History(0, str);
#endif /*FEATURE_HISTORY*/
                        str[charcount++] = _T('\n');
                        str[charcount] = _T('\0');
                        ConOutChar(_T('\n'));
                        bReturn = TRUE;
                        break;
                    }
                }
            }
        }

        bCharInput = FALSE;

        switch (KeyEvent.wVirtualKeyCode)
        {
            case VK_BACK:
            {
                /* <BACKSPACE> - delete character to left of cursor */
                if (current > 0 && charcount > 0)
                {
                    SHORT xcur, ycur;
                    GetCursorXY(&xcur, &ycur);

                    if (current == charcount)
                    {
                        /* if at end of line */
                        str[current - 1] = _T('\0');
                        if (xcur != 0)
                        {
                            ConOutPuts(_T("\b \b"));
                            cur.X--;
                        }
                        else
                        {
                            SetCursorXY((SHORT)(maxx - 1), (SHORT)(GetCursorY() - 1));
                            ConOutChar(_T(' '));
                            SetCursorXY((SHORT)(maxx - 1), (SHORT)(GetCursorY() - 1));
                            cur.Y--;
                            cur.X = maxx - 1;
                        }
                    }
                    else
                    {
                        for (count = current - 1; count < charcount; count++)
                            str[count] = str[count + 1];

                        if (xcur != 0)
                        {
                            SetCursorXY((SHORT)(xcur - 1), ycur);
                            cur.X--;
                        }
                        else
                        {
                            SetCursorXY((SHORT)(maxx - 1), (SHORT)(ycur - 1));
                            cur.Y--;
                            cur.X = maxx - 1;
                        }
                        GetCursorXY(&cur.X, &cur.Y);
                        ConOutPrintf(_T("%s "), &str[current - 1]);
                        SetCursorXY(cur.X, cur.Y);
                    }
                    charcount--;
                    current--;
                }
                break;
            }

            case VK_INSERT:
            {
                /* Toggle insert/overstrike mode */
                bInsert ^= TRUE;
                SetCursorType(bInsert, TRUE);
                break;
            }

            case VK_DELETE:
            {
                /* Delete character under cursor */
                if (current != charcount && charcount > 0)
                {
                    for (count = current; count < charcount; count++)
                        str[count] = str[count + 1];
                    charcount--;
                    GetCursorXY(&cur.X, &cur.Y);
                    ConOutPrintf(_T("%s "), &str[current]);
                    SetCursorXY(cur.X, cur.Y);
                }
                break;
            }

            case VK_HOME:
            {
                /* Go to beginning of string */
                if (current != 0)
                {
                    SetCursorXY(org.X, org.Y);
                    cur = org;
                    current = 0;
                }
                break;
            }

            case VK_END:
            {
                /* Go to end of string */
                if (current != charcount)
                {
                    SetCursorXY(org.X, org.Y);
                    ConOutPuts(str);
                    GetCursorXY(&cur.X, &cur.Y);
                    current = charcount;
                }
                break;
            }

            case _T('C'):
            {
                if ((dwControlKeyState &
                    (RIGHT_CTRL_PRESSED|LEFT_CTRL_PRESSED)))
                {
                    /* Ignore the Ctrl-C key event if it has already been handled */
                    if (!bCtrlBreak)
                        break;

                    /*
                     * Fully print the entered string
                     * so the command prompt would not overwrite it.
                     */
                    SetCursorXY(orgx, orgy);
                    ConOutPrintf(_T("%s"), str);

                    /*
                     * A Ctrl-C. Do not clear the command line,
                     * but return an empty string in str.
                     */
                    str[0] = _T('\0');
                    curx = orgx;
                    cury = orgy;
                    current = charcount = 0;
                    bReturn = TRUE;
                }
                else
                {
                    /* Just a normal 'C' character */
                    bCharInput = TRUE;
                }
                break;
            }

            case VK_RETURN:
            {
                /* End input, return to main */
#ifdef FEATURE_HISTORY
                /* Add to the history */
                if (str[0])
                    History(0, str);
#endif
                str[charcount++] = _T('\n');
                str[charcount] = _T('\0');
                ConOutChar(_T('\n'));
                bReturn = TRUE;
                break;
            }

            case VK_ESCAPE:
            {
                /* Clear str. Make this callable! */
                ClearLine(str, maxlen, org);
                current = charcount = 0;
                cur = org;
                break;
            }

#ifdef FEATURE_HISTORY
            case VK_F3:
                History_move_to_bottom();
                // break;
#endif
            case VK_UP:
            case VK_F5:
            {
#ifdef FEATURE_HISTORY
                /* Get previous command from buffer */
                ClearLine(str, maxlen, org);
                History(-1, str);
                current = charcount = _tcslen(str);
                if (((charcount + org.X) / maxx) + org.Y > maxy - 1)
                    org.Y += maxy - ((charcount + org.X) / maxx + org.Y + 1);
                ConOutPuts(str);
                GetCursorXY(&cur.X, &cur.Y);
#endif
                break;
            }

            case VK_DOWN:
            {
#ifdef FEATURE_HISTORY
                /* Get next command from buffer */
                ClearLine(str, maxlen, org);
                History(1, str);
                current = charcount = _tcslen(str);
                if (((charcount + org.X) / maxx) + org.Y > maxy - 1)
                    org.Y += maxy - ((charcount + org.X) / maxx + org.Y + 1);
                ConOutPuts(str);
                GetCursorXY(&cur.X, &cur.Y);
#endif
                break;
            }

            case VK_LEFT:
            {
                if (dwControlKeyState & (RIGHT_CTRL_PRESSED | LEFT_CTRL_PRESSED))
                {
                    /* Move cursor to the previous word */
                    if (current > 0)
                    {
                        while (current > 0 && str[current - 1] == _T(' '))
                        {
                            current--;
                            if (cur.X == 0)
                            {
                                cur.Y--;
                                cur.X = maxx -1;
                            }
                            else
                            {
                                cur.X--;
                            }
                        }

                        while (current > 0 && str[current -1] != _T(' '))
                        {
                            current--;
                            if (cur.X == 0)
                            {
                                cur.Y--;
                                cur.X = maxx -1;
                            }
                            else
                            {
                                cur.X--;
                            }
                        }

                        SetCursorXY(cur.X, cur.Y);
                    }
                }
                else
                {
                    /* Move cursor left */
                    if (current > 0)
                    {
                        SHORT xcur, ycur;
                        GetCursorXY(&xcur, &ycur);

                        current--;
                        if (xcur == 0)
                        {
                            SetCursorXY((SHORT)(maxx - 1), (SHORT)(ycur - 1));
                            cur.X = maxx - 1;
                            cur.Y--;
                        }
                        else
                        {
                            SetCursorXY((SHORT)(xcur - 1), ycur);
                            cur.X--;
                        }
                    }
                    else
                    {
                        MessageBeep(-1);
                    }
                }
                break;
            }

            case VK_RIGHT:
            case VK_F1:
            {
                if (dwControlKeyState & (RIGHT_CTRL_PRESSED | LEFT_CTRL_PRESSED))
                {
                    /* Move cursor to the next word */
                    if (current != charcount)
                    {
                        while (current != charcount && str[current] != _T(' '))
                        {
                            current++;
                            if (cur.X == maxx - 1)
                            {
                                cur.Y++;
                                cur.X = 0;
                            }
                            else
                            {
                                cur.X++;
                            }
                        }

                        while (current != charcount && str[current] == _T(' '))
                        {
                            current++;
                            if (cur.X == maxx - 1)
                            {
                                cur.Y++;
                                cur.X = 0;
                            }
                            else
                            {
                                cur.X++;
                            }
                        }

                        SetCursorXY(cur.X, cur.Y);
                    }
                }
                else
                {
                    /* Move cursor right */
                    if (current != charcount)
                    {
                        SHORT xcur, ycur;
                        GetCursorXY(&xcur, &ycur);

                        current++;
                        if (xcur == maxx - 1)
                        {
                            SetCursorXY(0, (SHORT)(ycur + 1));
                            cur.X = 0;
                            cur.Y++;
                        }
                        else
                        {
                            SetCursorXY((SHORT)(xcur + 1), ycur);
                            cur.X++;
                        }
                    }
#ifdef FEATURE_HISTORY
                    else
                    {
                        LPCTSTR last = PeekHistory(-1);
                        if (last && charcount < _tcslen(last))
                        {
                            PreviousChar = last[current];
                            ConOutChar(PreviousChar);
                            GetCursorXY(&cur.X, &cur.Y);
                            str[current++] = PreviousChar;
                            charcount++;
                        }
                    }
#endif
                }
                break;
            }

            default:
                /* This input is just a regular char */
                bCharInput = TRUE;
        }

        /* If this is not a regular char (including completion character), don't do anything anymore */
        if (!bCharInput)
            continue;

#ifdef _UNICODE
        ch = KeyEvent.uChar.UnicodeChar;
#else
        ch = (UCHAR)KeyEvent.uChar.AsciiChar;
#endif

        if (bCharInput && ch >= 0x20 && (charcount != (maxlen - 2)))
        {
            /* Insert the character into the string */
            if (bInsert && current != charcount)
            {
                /* If this character insertion will cause screen scrolling,
                 * adjust the saved origin of the command prompt. */
                tempscreen = _tcslen(str + current) + cur.X;
                if ((tempscreen % maxx) == (maxx - 1) &&
                    (tempscreen / maxx) + cur.Y == (maxy - 1))
                {
                    org.Y--;
                    cur.Y--;
                }

                for (count = charcount; count > current; count--)
                    str[count] = str[count - 1];
                str[current++] = ch;
                if (cur.X == maxx - 1)
                    cur.X = 0, cur.Y++;
                else
                    cur.X++;
                ConOutPrintf(_T("%s"), &str[current - 1]);
                SetCursorXY(cur.X, cur.Y);
                charcount++;
            }
            else
            {
                SHORT xcur, ycur;
                GetCursorXY(&xcur, &ycur);

                if (current == charcount)
                    charcount++;
                str[current++] = ch;
                if (xcur == maxx - 1 && ycur == maxy - 1)
                    org.Y--, cur.Y--;
                if (xcur == maxx - 1)
                    cur.X = 0, cur.Y++;
                else
                    cur.X++;
                ConOutChar(ch);
            }

            continue;
        }

/* See win32ss/user/winsrv/consrv/lineinput.c!LineInputKeyDown() for more details */
        if ((ch != 0x00) && (ch < 0x20) &&
            (InputControl.dwCtrlWakeupMask & (1 << ch)))
        {
            /* Set bCharInput to FALSE so that we won't insert this character */
            bCharInput = FALSE;

            /* Append a space character for compability with ReadConsole() */
            str[charcount++] = _T(' ');

            /* The user entered a control character, return it if needed */
            if (pControlPoint)
                *pControlPoint = current;
            if (pControlChar)
                *pControlChar = ch;

            /* We have found a completion character, break out */
            break;
        }

    } while (!bReturn);

    SetCursorType(bInsert, TRUE);

#ifdef _UNICODE
    if (pInputControl)
        pInputControl->dwControlKeyState = dwControlKeyState;
#endif

    *lpNumberOfCharsRead = charcount;

    return TRUE;
}

/* Wrapper for the Win32 ReadConsole() API */
BOOL
ReadConsoleLine(
    IN HANDLE hConsoleInput,
    IN HANDLE hConsoleOutput,
    OUT LPTSTR lpBuffer,
    IN DWORD nNumberOfCharsToRead,
    OUT PDWORD lpNumberOfCharsRead,
    IN PCONSOLE_READCONSOLE_CONTROL pInputControl OPTIONAL,
    IN DWORD nInsertPoint OPTIONAL,
    OUT PDWORD pControlPoint OPTIONAL,
    OUT PTCHAR pControlChar OPTIONAL)
{
    DWORD nInitialChars;
    DWORD dwWritten;
    DWORD dwInputMode;
    TCHAR ErasedChar;

    /* The value of nInitialChars must be less than nNumberOfCharsToRead */
    nInitialChars = 0;
    if (pInputControl)
    {
        pInputControl->nInitialChars = min(pInputControl->nInitialChars, nNumberOfCharsToRead);
        nInitialChars = pInputControl->nInitialChars;
    }

    /* The insertion position must be less than the number of characters kept in the string */
    nInsertPoint = min(nInsertPoint, nInitialChars);

    /*
     * Simulate direction key presses so as to move the cursor back to our
     * wanted position. We cannot use SetConsoleCursorPosition here because
     * we would loose cursor position synchronization with the position
     * inside the string buffer held by Read/WriteConsole.
     * The simulation is done at once in a packed way, so that we are not
     * interrupted by other inputs that could mess up the cursor position.
     */
    if (nInsertPoint >= 0)
    {
        INPUT_RECORD Ir;
        PINPUT_RECORD InputRecords, pIr;

        /* Prepare the common data */
        Ir.EventType = KEY_EVENT;
        Ir.Event.KeyEvent.wRepeatCount = 1;
        Ir.Event.KeyEvent.uChar.UnicodeChar = 0;
        Ir.Event.KeyEvent.dwControlKeyState = 0;

        /* Initialize the key presses */
        dwWritten = 1 + nInsertPoint + 1;
        InputRecords = (PINPUT_RECORD)HeapAlloc(GetProcessHeap(), 0,
                                                dwWritten * sizeof(*InputRecords));

        pIr = InputRecords;

        /* Go to the beginning of the line */
        Ir.Event.KeyEvent.wVirtualKeyCode = VK_HOME;
        Ir.Event.KeyEvent.wVirtualScanCode = MapVirtualKeyW(VK_HOME, MAPVK_VK_TO_CHAR);
        Ir.Event.KeyEvent.bKeyDown = TRUE;
        *pIr++ = Ir;

#if 0 // If enabled, add + 1 to dwWritten above.
        Ir.Event.KeyEvent.bKeyDown = FALSE;
        *pIr++ = Ir;
#endif

        /* Press the right-arrow key as many times as needed to go to the insertion point */
        Ir.Event.KeyEvent.wVirtualKeyCode = VK_RIGHT;
        Ir.Event.KeyEvent.wVirtualScanCode = MapVirtualKeyW(VK_RIGHT, MAPVK_VK_TO_CHAR);
        Ir.Event.KeyEvent.bKeyDown = TRUE;
        while (nInsertPoint--)
        {
            *pIr++ = Ir;
        }
        Ir.Event.KeyEvent.bKeyDown = FALSE;
        *pIr = Ir;

        WriteConsoleInput(hConsoleInput, InputRecords, dwWritten, &dwWritten);

        HeapFree(GetProcessHeap(), 0, InputRecords);
    }

    if (!ReadConsole(hConsoleInput,
                     lpBuffer,
                     nNumberOfCharsToRead,
                     lpNumberOfCharsRead,
                     pInputControl))
    {
        return FALSE;
    }

    /* Nothing to do if an empty string was read */
    if (lpNumberOfCharsRead == 0)
        return TRUE;

    /* Nothing more to do if there is no input control checks */
    if (!pInputControl)
        return TRUE;

    /* Initialize the control character values */
    if (pControlPoint)
        *pControlPoint = 0;
    if (pControlChar)
        *pControlChar = _T('\0');

    /*
     * Check whether the user has entered a control character. If so we should
     * find the control character inside the string and we are 100% sure it is
     * present because of that. Indeed the console filters this character.
     *
     * '*lpNumberOfCharsRead' always contains the total number of characters
     * in 'lpBuffer'.
     * In order to know where the user entered the control character we just
     * have to find the (first) control character in the string.
     */
    dwWritten = *lpNumberOfCharsRead;
    while (dwWritten > 0)
    {
        if ((*lpBuffer != 0x00) && (*lpBuffer < 0x20) &&
            (pInputControl->dwCtrlWakeupMask & (1 << *lpBuffer)))
        {
            /* We have found a control character, break out */
            break;
        }

        dwWritten--;
        lpBuffer++;
    }

    /* If the user actually validated the line, there is nothing more to do */
    if (dwWritten == 0)
        return TRUE;

    /* The user entered a control character, return it if needed */
    if (pControlPoint)
        *pControlPoint = *lpNumberOfCharsRead - dwWritten;
    if (pControlChar)
        *pControlChar = *lpBuffer;

    /*
     * If we are not at the end of the string and if we are in echo mode,
     * attempt to restore the overwritten character.
     *
     * ReadConsole overwrites the character on which we are doing the
     * autocompletion by e.g. TAB, so we cannot know it by just looking
     * at its results. We retrieve the erased character by directly
     * reading it from the screen. This is *REALLY* hackish but it works!
     *
     * WARNING: Only works if we are in ECHO mode!!
     */
    ErasedChar = _T('\0');

    GetConsoleMode(hConsoleInput, &dwInputMode);
    if ((dwInputMode & ENABLE_LINE_INPUT) && (dwInputMode & ENABLE_ECHO_INPUT))
    {
        /* csbi.dwCursorPosition holds the cursor position where the erased character is */
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(hConsoleOutput, &csbi);

        dwWritten = 0;
        ReadConsoleOutputCharacter(hConsoleOutput, &ErasedChar, 1,
                                   csbi.dwCursorPosition, &dwWritten);
        if (dwWritten == 0) ErasedChar = _T('\0');
    }

    *lpBuffer = ErasedChar;

    return TRUE;
}


//
// FIXME: This is a test function only!!
//
BOOL
DoCompletion2(
    IN PCOMPLETION_CONTEXT Context,
    IN PVOID CompleterParameter OPTIONAL,
    IN OUT LPTSTR CmdLine,
    IN UINT charcount,          // maxlen
    IN UINT cursor,             // Insertion point (NULL-terminated) // FIXME!!
    IN BOOL InsertMode,     // TRUE: insert ; FALSE: overwrite
    IN TCHAR CompletionChar,
    IN ULONG ControlKeyState,   // Which keys are pressed during the completion
    OUT PBOOL CompletionRestarted OPTIONAL)
{
static const LPCTSTR CompletingStrings[] =
{
    _T("CompleteString1"),
    _T("Short2"),
    _T("MediumStrg3"),
};

static UINT index = 0;

    HRESULT hr;

    /* Insert the arbitrary string */
    if (InsertMode)
    {
        hr = StringCchInsertString(CmdLine + cursor, charcount - cursor,
                                   CompletingStrings[index],
                                   0);
    }
    else
    {
        hr = StringCchCopy(CmdLine + cursor, charcount - cursor,
                           CompletingStrings[index]);
    }
    if (FAILED(hr))
    {
        MessageBeep(-1);
        return FALSE;
    }

    index++;
    index %= ARRAYSIZE(CompletingStrings);

    return TRUE;
}


static BOOL
ReadLineConsoleHelper(
    IN HANDLE hInput,
    IN HANDLE hOutput,
    IN OUT LPTSTR str,
    IN DWORD maxlen,
    IN READ_CONSOLE_LINE ReadConsoleLineProc,
    IN COMPLETER_CALLBACK CompleterCallback,
    IN PVOID CompleterParameter OPTIONAL)
{
    BOOL Success;

    CONSOLE_READCONSOLE_CONTROL InputControl;

    DWORD dwOldMode, dwNewMode;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    // COORD InitialCursorPos;

    DWORD charcount = 0; /* chars in the string (str) */
    DWORD ControlPoint = 0; // current // count /* the position of the cursor in the string (str) */
    TCHAR CompletionChar;
    BOOL InsertMode;
    BOOL CompletionRestarted;
    COMPLETION_CONTEXT Context;

#if 1 /************** For temporary CODE REUSE!!!! ********************/
    /* Screen information */
    COORD org;  /* origin x/y */
    DWORD tempscreen;
#endif


    if (!str || maxlen == 0)
        return TRUE;


    /*
     * Retrieve the original console input modes, and check explicitely
     * the existing extended modes. For these to be valid,  the explicit
     * presence of ENABLE_EXTENDED_FLAGS is required.
     */
    GetConsoleMode(hInput, &dwOldMode);
    if (!(dwOldMode & ENABLE_EXTENDED_FLAGS))
        dwOldMode &= ~(ENABLE_EXTENDED_FLAGS | ENABLE_QUICK_EDIT_MODE | ENABLE_INSERT_MODE);

    /* Set new console line-input modes, keeping only the existing extended modes */
    /*
     * ENABLE_LINE_INPUT: Enable line-editing features of ReadConsole;
     * ENABLE_ECHO_INPUT: Echo what we are typing;
     * ENABLE_PROCESSED_INPUT: The console deals with editing characters.
     */
    dwNewMode = ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT;
    dwNewMode |= dwOldMode & (ENABLE_EXTENDED_FLAGS | ENABLE_QUICK_EDIT_MODE | ENABLE_INSERT_MODE);

    SetConsoleMode(hInput, dwNewMode);


    /* If autocompletion is disabled, just call directly ReadConsole */

    if (IS_COMPLETION_DISABLED(AutoCompletionChar) &&
        IS_COMPLETION_DISABLED(PathCompletionChar))
    {
        Success = ReadConsoleLineProc(hInput, hOutput,
                                      str, maxlen,
                                      &charcount,
                                      NULL, 0,
                                      NULL, NULL);
        goto Quit;
    }


    /* Autocompletion is enabled */


    // HACK!!!!
    SetConsoleMode(hOutput, ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT);
    //

    /* Get screen size and other info */
    // FIXME due to the fact hOutput could be != StdOutScreen
    // GetConsoleScreenBufferInfo(hOutput, &csbi);
    if (!ConGetScreenInfo(&StdOutScreen, &csbi,
                          CON_SCREEN_SBSIZE | CON_SCREEN_CURSORPOS))
    {
        csbi.dwSize.X = 80;
        csbi.dwSize.Y = 25;
        csbi.dwCursorPosition.X = csbi.dwCursorPosition.Y = 0;
    }
    // InitialCursorPos = csbi.dwCursorPosition;
#if 1 /************** For temporary CODE REUSE!!!! ********************/
    maxx = csbi.dwSize.X;
    maxy = csbi.dwSize.Y;
    org = csbi.dwCursorPosition; // InitialCursorPos.{X/Y}
#endif

    InputControl.nLength = sizeof(InputControl);
    InputControl.nInitialChars = 0; // Initially zero, but will change during autocompletion.
    ControlPoint = 0;

    /*
     * Now, Da Magicks: initialize the wakeup mask with the characters
     * that trigger the autocompletion. Only for characters < 32 (SPACE).
     */
    InputControl.dwCtrlWakeupMask =
        ((1 << AutoCompletionChar) | (1 << PathCompletionChar));

    /* Initialize the auto-completion context */
    InitCompletionContext(&Context, CompleterCallback);

    while (TRUE)
    {
        InputControl.dwControlKeyState = 0;

        /*
         * Current cursor position is expected to be at (orig) + InputControl.nInitialChars
         * so that the ReadConsole procedure knows the line starts at (orig)
         * while keeping nInitialChars initial characters of the text line,
         * and the cursor be placed at ControlPoint.
         */
        Success = ReadConsoleLineProc(hInput, hOutput,
                                      str, maxlen,
                                      &charcount,
                                      &InputControl,
                                      ControlPoint,
                                      &ControlPoint,
                                      &CompletionChar);

        // FIXME: Check for Ctrl-C / Ctrl-Break !!
        /* If we failed or broke, bail out */
        if (!Success || charcount == 0)
            break;

        /* If the user validated the command, no autocompletion to perform */
        if (!CompletionChar /* && ControlPoint == 0 */)
            break;


        /* Autocompletion is pending */

        /* We keep the first characters, but do not take into account the TAB */

        /* NULL-terminate the line */
        // 'charcount' always contains the total number of characters of 'str'.
        str[charcount - 1] = _T('\0');
        /**/charcount--;/**/
        InputControl.nInitialChars = charcount; // - 1;


        /* Perform the line completion */

        /*
         * Refresh the cached console input modes.
         * Again, check explicitely the existing extended modes.
         */
        GetConsoleMode(hInput, &dwNewMode);
        if (!(dwNewMode & ENABLE_EXTENDED_FLAGS))
            dwNewMode &= ~(ENABLE_EXTENDED_FLAGS | ENABLE_QUICK_EDIT_MODE | ENABLE_INSERT_MODE);

        /* Determine the current insertion mode */
        InsertMode = ( (dwNewMode & ENABLE_INSERT_MODE) && !(GetKeyState(VK_INSERT) & 0x0001)) ||
                     (!(dwNewMode & ENABLE_INSERT_MODE) &&  (GetKeyState(VK_INSERT) & 0x0001));

        /* Used later to see if we went down to the next line */
        tempscreen = charcount; // tempscreen == old_charcount

        CompletionRestarted = TRUE;
        /**/bUseBashCompletion = FALSE;/**/
        // DoCompletion2
        Success = DoCompletion(&Context, CompleterParameter,
                               str, maxlen /* charcount */, ControlPoint,
                               InsertMode, CompletionChar,
                               InputControl.dwControlKeyState,
                               &CompletionRestarted);

        InputControl.nInitialChars = _tcslen(str);

        if (Success)
        {
            /*
             * Update the number of characters to keep;
             * do not take the NULL terminator into account.
             */
            InputControl.nInitialChars = _tcslen(str);

            ++charcount; // Just because charcount has already been decremented before!

            if (InsertMode)
            {
                if (InputControl.nInitialChars + 1 >= charcount)
                    ControlPoint += InputControl.nInitialChars + 1 - charcount;
                else
                    ControlPoint -= charcount - (InputControl.nInitialChars + 1);
            }
            else
            {
                ControlPoint = InputControl.nInitialChars + 1;
            }
        }

        if (bUseBashCompletion)
        {
            // if ((bUseBashCompletion && CompletionRestarted) || (!bUseBashCompletion))
            if (CompletionRestarted)
            {
                /* If first TAB, complete the line */

                // /* Used later to see if we went down to the next line */
                // tempscreen = charcount; // tempscreen == old_charcount

                /* Figure out where the cursor is going to be after we print it */
                // ControlPoint = charcount = InputControl.nInitialChars;

                /* Print out what we have now */
                PrintPartialLine(str, /*charcount*/ InputControl.nInitialChars, tempscreen, &org);
            }
            // else if (bUseBashCompletion && !CompletionRestarted && Success)
            else if (Success)
            {
                /* If second TAB, list matches */

                /* Restore the prompt with uncompleted command */
                PrintPrompt();
                GetCursorXY(&org.X, &org.Y);
                ConOutPuts(str);
            }
#if 0
            /* Reset cursor position */
            SetCursorXY((org.X + ControlPoint) % maxx,
                        org.Y + (org.X + ControlPoint) / maxx);
#endif
        }
        else
        {
            /* Figure out where the cursor is going to be after we print it */
            // ControlPoint = charcount = InputControl.nInitialChars;

            /* Print out what we have now */
            // PrintPartialLine(str, charcount, tempscreen, &org);

            // if (Success)
            {
                DWORD dwWritten;


        //
        // FIXME: NOTE: The redrawing code + cursor stuff may behave strangely
        // if the string contains embedded control characters (for example,
        // 0x0D, 0x0A and so on...)
        //

        /* Update the string on screen */
        SetConsoleCursorPosition(hOutput, org /*InitialCursorPos*/);
        WriteConsole(hOutput, str, InputControl.nInitialChars, &dwWritten, NULL);
        /* Fill with whitespace *iif* the completed string is smaller than the original one */
        if (charcount > dwWritten)
        {
            csbi.dwCursorPosition = org /*InitialCursorPos*/;
            csbi.dwCursorPosition.X += (SHORT)dwWritten;
            FillConsoleOutputCharacter(hOutput, _T(' '),
                                       charcount - dwWritten,
                                       csbi.dwCursorPosition,
                                       &dwWritten);
        }
#if 0
        /*
         * Place the cursor where ReadConsole() expects it to be:
         *     CurrentPosition = InitialNumChars;
         *     OriginalCursorPosition = TextInfo.CursorPosition;
         *     OriginalCursorPosition.X -= CurrentPosition;
         */
        csbi.dwCursorPosition = org /*InitialCursorPos*/;
        csbi.dwCursorPosition.X += InputControl.nInitialChars;
        SetConsoleCursorPosition(hOutput, csbi.dwCursorPosition);
#endif


            }


        }

#if 0
        /* Reset cursor position */
        SetCursorXY((org.X + ControlPoint) % maxx,
                    org.Y + (org.X + ControlPoint) / maxx);
#endif
    }

    /* Free the auto-completion context */
    FreeCompletionContext(&Context);

Quit:
    /* Restore the original console input modes */
    SetConsoleMode(hInput, dwOldMode);
    return Success;
}

BOOL
ReadLine(
    IN HANDLE hInput,
    IN HANDLE hOutput,
    IN OUT LPTSTR str,
    IN DWORD maxlen,
    IN COMPLETER_CALLBACK CompleterCallback,
    IN PVOID CompleterParameter OPTIONAL)
{
    BOOL Success = FALSE;
    // DWORD dwOldMode;

    // GetConsoleMode(hInput, &dwOldMode);
    // SetConsoleMode(hInput, /* dwOldMode | */ ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);

    ZeroMemory(str, maxlen * sizeof(TCHAR));

#if 0
    if (IsConsoleHandle(hInput) && IsConsoleHandle(hOutput))
    {
        if (!ReadLineConsoleHelper(hInput, hOutput,
                                   str, maxlen,
                                   ReadConsoleLine,
                                   CompleterCallback,
                                   CompleterParameter))
        {
            goto Quit;
        }
    }
    else
#endif
    if (IsTTYHandle(hInput) && IsTTYHandle(hOutput)) // Either hInput or hOutput can be console, but not both.
    {
        if (!ReadLineConsoleHelper(hInput, hOutput,
                                   str, maxlen,
                                   ReadTTYLine,
                                   CompleterCallback,
                                   CompleterParameter))
        {
            goto Quit;
        }
    }
    else
    {
        /* No console or TTY (this is a file) */
        if (!ReadLineFromFile(hInput, /* hOutput, */ str, maxlen))
            goto Quit;
    }

    Success = TRUE;

Quit:
    // SetConsoleMode(hInput, dwOldMode);
    return Success;
}

/* Read a command from the command line */
BOOL ReadCommand(LPTSTR str, DWORD maxlen)
{
    if (!ReadLine(ConStreamGetOSHandle(StdIn),
                  ConStreamGetOSHandle(StdOut),
                  str, maxlen,
                  CompleteFilename, NULL))
    {
        return FALSE;
    }

#ifdef FEATURE_ALIASES
    /* Expand all aliases */
    ExpandAlias(str, maxlen);
#endif /* FEATURE_ALIAS */

    return TRUE;
}
