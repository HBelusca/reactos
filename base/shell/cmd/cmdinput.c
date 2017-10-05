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

static VOID
ClearCommandLine(LPTSTR str, DWORD maxlen, SHORT orgx, SHORT orgy)
{
    DWORD count;

    SetCursorXY(orgx, orgy);
    for (count = 0; count < _tcslen(str); count++)
        ConOutChar(_T(' '));
    SetCursorXY(orgx, orgy);

    _tcsnset(str, _T('\0'), maxlen);
}

static VOID
PrintPartialPrompt(IN LPTSTR str, IN DWORD charcount, IN DWORD tempscreen,
                   IN OUT PSHORT orgx, IN OUT PSHORT orgy,
                   OUT PSHORT curx, OUT PSHORT cury)
{
    //// tempscreen == old_charcount
    /**/ DWORD count; /**/
    /**/ DWORD current; /**/
    /**/ current = charcount; /**/

    /* Print out what we have now */
    SetCursorXY(*orgx, *orgy);
    ConOutPuts(str);

    /* Move cursor accordingly */
    if (tempscreen > charcount)
    {
        GetCursorXY(curx, cury);
        for (count = tempscreen - charcount; count--; )
            ConOutChar(_T(' '));
        SetCursorXY(*curx, *cury);
    }
    else
    {
        if (((charcount + *orgx) / maxx) + *orgy > maxy - 1)
            *orgy += maxy - ((charcount + *orgx) / maxx + *orgy + 1);
    }

    /* Set cursor position */
    SetCursorXY((short)(((int)*orgx + current) % maxx),
                (short)((int)*orgy + ((int)*orgx + current) / maxx));
    GetCursorXY(curx, cury);
}

static BOOL
ReadCommandFromFile(HANDLE hInput, /* HANDLE hOutput, */ LPTSTR str, DWORD maxlen)
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

static BOOL
ReadCommandFromTTY(HANDLE hInput, HANDLE hOutput, LPTSTR str, DWORD maxlen)
{
    BOOL Success;

    /* Screen information */
    SHORT orgx, orgy;   /* origin x/y */
    SHORT curx, cury;   /* current x/y cursor position */

    DWORD tempscreen;
    DWORD count;        /* used in some for loops */
    DWORD current = 0;  /* the position of the cursor in the string (str) */
    DWORD charcount = 0;/* chars in the string (str) */
    KEY_EVENT_RECORD KeyEvent;
    DWORD dwControlKeyState;

    BOOL CheckForDirs;
    BOOL CompletionRestarted;
    COMPLETION_CONTEXT Context;

    TCHAR ch;
    BOOL bReturn = FALSE;
    BOOL bCharInput;
#ifdef FEATURE_HISTORY
    //BOOL bContinue=FALSE;/*is TRUE the second case will not be executed*/
    TCHAR PreviousChar;
#endif

/* Global command line insert/overwrite flag */
static BOOL bInsert = TRUE;

    /* Get screen size and other info */
    GetScreenSize(&maxx, &maxy); // csbi.dwSize.{X,Y}
    GetCursorXY(&orgx, &orgy);   // csbi.dwCursorPosition.{X,Y}
    curx = orgx;
    cury = orgy;

    SetCursorType(bInsert, TRUE);

    /* Initialize the auto-completion context */
    InitCompletionContext(&Context);

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

                        ClearCommandLine(str, maxlen, orgx, orgy);
                        current = charcount = 0;
                        curx = orgx;
                        cury = orgy;
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
                        ClearCommandLine(str, maxlen, orgx, orgy);
                        History_del_current_entry(str);
                        current = charcount = _tcslen(str);
                        ConOutPuts(str);
                        GetCursorXY(&curx, &cury);
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
                    if (current == charcount)
                    {
                        /* if at end of line */
                        str[current - 1] = _T('\0');
                        if (GetCursorX() != 0)
                        {
                            ConOutPuts(_T("\b \b"));
                            curx--;
                        }
                        else
                        {
                            SetCursorXY((SHORT)(maxx - 1), (SHORT)(GetCursorY() - 1));
                            ConOutChar(_T(' '));
                            SetCursorXY((SHORT)(maxx - 1), (SHORT)(GetCursorY() - 1));
                            cury--;
                            curx = maxx - 1;
                        }
                    }
                    else
                    {
                        for (count = current - 1; count < charcount; count++)
                            str[count] = str[count + 1];
                        if (GetCursorX() != 0)
                        {
                            SetCursorXY((SHORT)(GetCursorX() - 1), GetCursorY());
                            curx--;
                        }
                        else
                        {
                            SetCursorXY((SHORT)(maxx - 1), (SHORT)(GetCursorY() - 1));
                            cury--;
                            curx = maxx - 1;
                        }
                        GetCursorXY(&curx, &cury);
                        ConOutPrintf(_T("%s "), &str[current - 1]);
                        SetCursorXY(curx, cury);
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
                    GetCursorXY(&curx, &cury);
                    ConOutPrintf(_T("%s "), &str[current]);
                    SetCursorXY(curx, cury);
                }
                break;
            }

            case VK_HOME:
            {
                /* Go to beginning of string */
                if (current != 0)
                {
                    SetCursorXY(orgx, orgy);
                    curx = orgx;
                    cury = orgy;
                    current = 0;
                }
                break;
            }

            case VK_END:
            {
                /* Go to end of string */
                if (current != charcount)
                {
                    SetCursorXY(orgx, orgy);
                    ConOutPuts(str);
                    GetCursorXY(&curx, &cury);
                    current = charcount;
                }
                break;
            }

            case _T('C'):
            {
                if ((KeyEvent.dwControlKeyState &
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
                ClearCommandLine(str, maxlen, orgx, orgy);
                current = charcount = 0;
                curx = orgx;
                cury = orgy;
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
                ClearCommandLine(str, maxlen, orgx, orgy);
                History(-1, str);
                current = charcount = _tcslen(str);
                if (((charcount + orgx) / maxx) + orgy > maxy - 1)
                    orgy += maxy - ((charcount + orgx) / maxx + orgy + 1);
                ConOutPuts(str);
                GetCursorXY(&curx, &cury);
#endif
                break;
            }

            case VK_DOWN:
            {
#ifdef FEATURE_HISTORY
                /* Get next command from buffer */
                ClearCommandLine(str, maxlen, orgx, orgy);
                History(1, str);
                current = charcount = _tcslen(str);
                if (((charcount + orgx) / maxx) + orgy > maxy - 1)
                    orgy += maxy - ((charcount + orgx) / maxx + orgy + 1);
                ConOutPuts(str);
                GetCursorXY(&curx, &cury);
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
                            if (curx == 0)
                            {
                                cury--;
                                curx = maxx -1;
                            }
                            else
                            {
                                curx--;
                            }
                        }

                        while (current > 0 && str[current -1] != _T(' '))
                        {
                            current--;
                            if (curx == 0)
                            {
                                cury--;
                                curx = maxx -1;
                            }
                            else
                            {
                                curx--;
                            }
                        }

                        SetCursorXY(curx, cury);
                    }
                }
                else
                {
                    /* Move cursor left */
                    if (current > 0)
                    {
                        current--;
                        if (GetCursorX() == 0)
                        {
                            SetCursorXY((SHORT)(maxx - 1), (SHORT)(GetCursorY() - 1));
                            curx = maxx - 1;
                            cury--;
                        }
                        else
                        {
                            SetCursorXY((SHORT)(GetCursorX() - 1), GetCursorY());
                            curx--;
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
                            if (curx == maxx - 1)
                            {
                                cury++;
                                curx = 0;
                            }
                            else
                            {
                                curx++;
                            }
                        }

                        while (current != charcount && str[current] == _T(' '))
                        {
                            current++;
                            if (curx == maxx - 1)
                            {
                                cury++;
                                curx = 0;
                            }
                            else
                            {
                                curx++;
                            }
                        }

                        SetCursorXY(curx, cury);
                    }
                }
                else
                {
                    /* Move cursor right */
                    if (current != charcount)
                    {
                        current++;
                        if (GetCursorX() == maxx - 1)
                        {
                            SetCursorXY(0, (SHORT)(GetCursorY() + 1));
                            curx = 0;
                            cury++;
                        }
                        else
                        {
                            SetCursorXY((SHORT)(GetCursorX() + 1), GetCursorY());
                            curx++;
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
                            GetCursorXY(&curx, &cury);
                            str[current++] = PreviousChar;
                            charcount++;
                        }
                    }
#endif
                }
                break;
            }

            default:
                /* This input is just a normal char */
                bCharInput = TRUE;
        }

#ifdef _UNICODE
        ch = KeyEvent.uChar.UnicodeChar;
#else
        ch = (UCHAR)KeyEvent.uChar.AsciiChar;
#endif /* _UNICODE */

/* See win32ss/user/winsrv/consrv/lineinput.c!LineInputKeyDown() for more details */
        if (ch < 0x20 &&
#if 0
            (Console->LineWakeupMask & (1 << ch))
#else
            ((ch == AutoCompletionChar) || (ch == PathCompletionChar))
#endif
           )
        {
            /* Set bCharInput to FALSE so that we won't insert this character */
            bCharInput = FALSE;

            /*
             * Since 'AutoCompletionChar' has priority over 'PathCompletionChar'
             * (as they can be the same), we perform the checks in this order.
             */
            CheckForDirs = FALSE;
            if (ch == AutoCompletionChar)
                CheckForDirs = FALSE;
            else if (ch == PathCompletionChar)
                CheckForDirs = TRUE;

            /* Expand current file name */

            // FIXME: We note that we perform the completion only if we
            // are at the end of the string. We should implement doing
            // the completion in the middle of the string.

#ifdef FEATURE_UNIX_FILENAME_COMPLETION
            // if ((current == charcount) ||           // We are at the end
                // (current == charcount - 1 &&
                 // str[current] == _T('"'))) /* only works at end of line */
#endif

            /* Used to later see if we went down to the next line */
            tempscreen = charcount; // tempscreen == old_charcount

            CompletionRestarted = TRUE;
            Success = DoCompletion(&Context, str, current, maxlen, // charcount,
                                   dwControlKeyState, CheckForDirs,
                                   &CompletionRestarted);
            if (!Success)
                MessageBeep(-1);

            if (bUseBashCompletion)
            {
                if (CompletionRestarted)
                {
                    /* If first TAB, complete filename */

                    // /* Used to later see if we went down to the next line */
                    // tempscreen = charcount; // tempscreen == old_charcount

                    /* Figure out where the cursor is going to be after we print it */
                    current = charcount = _tcslen(str);

                    /* Print out what we have now */
                    PrintPartialPrompt(str, charcount, tempscreen,
                                       &orgx, &orgy, &curx, &cury);
                }
                else if (Success)
                {
                    /* If second TAB, list matches */

                    /* Restore the prompt with uncompleted command */
                    PrintPrompt();
                    GetCursorXY(&orgx, &orgy);
                    ConOutPuts(str);

                    /* Set cursor position */
                    SetCursorXY((orgx + current) % maxx,
                                 orgy + (orgx + current) / maxx);
                    GetCursorXY(&curx, &cury);
                }
            }
            else
            {
                /* Figure out where the cursor is going to be after we print it */
                current = charcount = _tcslen(str);

                /* Print out what we have now */
                PrintPartialPrompt(str, charcount, tempscreen,
                                   &orgx, &orgy, &curx, &cury);
            }
        }

        if (bCharInput && ch >= 32 && (charcount != (maxlen - 2)))
        {
            /* insert character into string... */
            if (bInsert && current != charcount)
            {
                /* If this character insertion will cause screen scrolling,
                 * adjust the saved origin of the command prompt. */
                tempscreen = _tcslen(str + current) + curx;
                if ((tempscreen % maxx) == (maxx - 1) &&
                    (tempscreen / maxx) + cury == (maxy - 1))
                {
                    orgy--;
                    cury--;
                }

                for (count = charcount; count > current; count--)
                    str[count] = str[count - 1];
                str[current++] = ch;
                if (curx == maxx - 1)
                    curx = 0, cury++;
                else
                    curx++;
                ConOutPrintf(_T("%s"), &str[current - 1]);
                SetCursorXY(curx, cury);
                charcount++;
            }
            else
            {
                if (current == charcount)
                    charcount++;
                str[current++] = ch;
                if (GetCursorX() == maxx - 1 && GetCursorY() == maxy - 1)
                    orgy--, cury--;
                if (GetCursorX() == maxx - 1)
                    curx = 0, cury++;
                else
                    curx++;
                ConOutChar(ch);
            }
        }
    }
    while (!bReturn);

    /* Free the auto-completion context */
    FreeCompletionContext(&Context);

    SetCursorType(bInsert, TRUE);
    return TRUE;
}

// static
BOOL
ReadCommandFromConsole(HANDLE hInput, HANDLE hOutput, LPTSTR str, DWORD maxlen)
{
    BOOL Success;
#ifdef _UNICODE
    CONSOLE_READCONSOLE_CONTROL InputControl;
#endif
    DWORD dwOldMode, dwNewMode;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    COORD InitialCursorPos;
    DWORD count;        /*used in some for loops*/
    DWORD charcount = 0;/*chars in the string (str)*/
    DWORD dwWritten;
    TCHAR ErasedChar;
    LPTSTR insert = NULL; // , tmp = NULL;
    SIZE_T sizeInsert, sizeAppend;

    BOOL CheckForDirs;
    BOOL CompletionRestarted;
    COMPLETION_CONTEXT Context;

#if 1 /************** For temporary CODE REUSE!!!! ********************/
    SHORT orgx, orgy;     /* origin x/y */
    SHORT curx, cury;     /*current x/y cursor position*/
    DWORD tempscreen;
    DWORD current = 0;  /*the position of the cursor in the string (str)*/
#endif


    // NOTE that this function is a more throughout version of ConInString!



    /* If autocompletion is disabled, just call directly ReadConsole */
    if (IS_COMPLETION_DISABLED(AutoCompletionChar) &&
        IS_COMPLETION_DISABLED(PathCompletionChar))
    {
        return ReadConsole(hInput,
                           str,
                           maxlen,
                           &charcount,
                           NULL);
    }

    /* Autocompletion is enabled */


    /* Retrieve the original console input modes */
    GetConsoleMode(hInput, &dwOldMode);

    /* Set the new console input modes */
    // FIXME: Maybe *add* to the old modes, the new ones??
    /*
     * ENABLE_LINE_INPUT: Enable line-editing features of ReadConsole;
     * ENABLE_ECHO_INPUT: Echo what we are typing;
     * ENABLE_PROCESSED_INPUT: The console deals with editing characters.
     */
    dwNewMode = ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT;

    /*
     * Explicitely add the existing extended flags.
     * If the extended flags are not reported, add them and explicitely enable insert mode
     * (so that the original insert mode is reset).
     */
    dwNewMode |= dwOldMode & (ENABLE_EXTENDED_FLAGS | ENABLE_QUICK_EDIT_MODE | ENABLE_INSERT_MODE);
    if (!(dwOldMode & ENABLE_EXTENDED_FLAGS))
        dwNewMode |= (ENABLE_EXTENDED_FLAGS | ENABLE_INSERT_MODE);

    SetConsoleMode(hInput, dwNewMode);
    // HACK!!!!
    SetConsoleMode(hOutput, ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT);

    GetConsoleScreenBufferInfo(hOutput, &csbi);
    InitialCursorPos = csbi.dwCursorPosition;

#if 1 /************** For temporary CODE REUSE!!!! ********************/
    /* Get screen size */
    maxx = csbi.dwSize.X;
    maxy = csbi.dwSize.Y;

    curx = orgx = csbi.dwCursorPosition.X; // InitialCursorPos.X
    cury = orgy = csbi.dwCursorPosition.Y; // InitialCursorPos.Y
#endif

    InputControl.nLength = sizeof(InputControl);
    InputControl.nInitialChars = 0; // Initially, zero, but will be different during autocompletion.

    /*
     * Now, Da Magicks: initialize the wakeup mask with the characters
     * that trigger the autocompletion. Only for characters < 32 (SPACE).
     */
    InputControl.dwCtrlWakeupMask =
        ((1 << AutoCompletionChar) | (1 << PathCompletionChar));

    /* Initialize the auto-completion context */
    InitCompletionContext(&Context);

    do
    {
        InputControl.dwControlKeyState = 0;

        Success = ReadConsole(hInput,
                              str,
                              maxlen,
                              &charcount,
#ifdef _UNICODE
                              &InputControl
#else
                              NULL
#endif
                              );

        // FIXME: Check for Ctrl-C / Ctrl-Break !!
        /* If we failed or broke, bail out */
        if (!Success || charcount == 0)
            break;

        /*
         * Check whether the user has performed autocompletion.
         * If so we should find the control character inside the string
         * and we are 100% sure it is present because of this. Indeed
         * the console filters the character.
         *
         * 'charcount' always contains the total number of characters of 'str'.
         * To find where the user pressed TAB we need to find the (first) TAB
         * character in the string.
         */
        CheckForDirs = FALSE;
        count = charcount;
        insert = str;
        while (count)
        {
            /*
             * Since 'AutoCompletionChar' has priority over 'PathCompletionChar'
             * (as they can be the same), we perform the checks in this order.
             */
            if (*insert == AutoCompletionChar)
            {
                break;
            }
            else if (*insert == PathCompletionChar)
            {
                CheckForDirs = TRUE;
                break;
            }

            count--;
            insert++;
        }

        /* If the user validated the command, no autocompletion to perform */
        if (count == 0)
            break;

        /* Autocompletion is pending */

        /* We keep the first characters, but do not take into account the TAB */

        /*
         * Refresh the cached console input modes.
         * Again, explicitely check the existing extended flags.
         * If the extended flags are not reported, add them and explicitely enable insert mode
         * (so that the original insert mode is reset).
         */
        GetConsoleMode(hInput, &dwNewMode);
        dwNewMode |= dwNewMode & (ENABLE_EXTENDED_FLAGS | ENABLE_QUICK_EDIT_MODE | ENABLE_INSERT_MODE);
        if (!(dwNewMode & ENABLE_EXTENDED_FLAGS))
            dwNewMode |= (ENABLE_EXTENDED_FLAGS | ENABLE_INSERT_MODE);




#if 00000000000000000000 /****** My NEW method *******/ // 000000000000000000000

        /* Replace the TAB by NULL, and NULL-terminate the command */
        *insert = _T('\0'); // str[charcount - count];
        str[charcount - 1] = _T('\0');

        /* We keep the first characters, but do not take into account the TAB */

        /*
         * ReadConsole overwrites the character on which we are doing the
         * autocompletion by e.g. TAB, so we cannot know it by just looking
         * at its results. We retrieve the erased character by directly
         * reading it from the screen. This is *REALLY* hackish but it works!
         *
         * WARNING: Only works if we are in ECHO mode!!
         * csbi.dwCursorPosition holds the cursor position where the erased character is.
         */
        ErasedChar = _T('\0');
        if (dwNewMode & ENABLE_ECHO_INPUT)
        {
            GetConsoleScreenBufferInfo(hOutput, &csbi);
            dwWritten = 0;
            ReadConsoleOutputCharacter(hOutput, &ErasedChar, 1, csbi.dwCursorPosition, &dwWritten);
            if (dwWritten == 0) ErasedChar = _T('\0');
        }

        /* Save the buffer after the insertion (BASH-like autocompletion) */
        sizeAppend = (charcount - (insert - str + 1)) * sizeof(TCHAR);
        tmp = (LPTSTR)HeapAlloc(GetProcessHeap(), 0, sizeAppend);
        memcpy(tmp, insert + 1, sizeAppend);




        /* Insert the arbitrary string */
        // FIXME: Take into account the fact that the full buffer (starting from 'str') is 'maxlen' long!
        // NOTE: InputControl.dwControlKeyState can be used to know
        // how to perform the autocompletion...
        sizeInsert = sizeof(L"HI!") - sizeof(TCHAR);
        memcpy(insert, (PVOID)L"HI!", sizeInsert);
        insert += sizeInsert/sizeof(TCHAR);




        /* Copy the erased character if there is one */
        if (ErasedChar) *insert++ = ErasedChar;
        /* Insert the saved buffer part */
        memcpy(insert, tmp, sizeAppend);

        HeapFree(GetProcessHeap(), 0, tmp);


        /* Update the number of characters to keep. Do not take the NULL terminator into account. */
        // InputControl.nInitialChars = (old_insert - str) + (sizeInsert + possible_erased_char + sizeAppend)/sizeof(TCHAR);
        InputControl.nInitialChars = charcount + sizeInsert/sizeof(TCHAR) - 1;

        //
        // FIXME: NOTE: The redrawing code + cursor stuff may behave strangely
        // if the string contains embedded control characters (for example,
        // 0x0D, 0x0A and so on...)
        //

        /* Update the string on screen */
        SetConsoleCursorPosition(hOutput, InitialCursorPos);
        WriteConsole(hOutput, str, InputControl.nInitialChars, &dwWritten, NULL);
        /* Fill with whitespace *iif* completed string is smaller than original string */
        if (/*iCompletionCh*/charcount > dwWritten)
        {
            csbi.dwCursorPosition = InitialCursorPos;
            csbi.dwCursorPosition.X += (SHORT)dwWritten;
            FillConsoleOutputCharacter(hOutput, _T(' '),
                                       /*iCompletionCh*/charcount - dwWritten,
                                       csbi.dwCursorPosition,
                                       &dwWritten);
        }

        /*
         * Simulate Left-Arrow key presses in order to move the cursor to our
         * wanted position. We cannot use SetConsoleCursorPosition here because
         * we would loose cursor position synchronization with the position
         * inside the string buffer held by ReadConsole.
         */
        // FIXME: Do it at once (packed) so that we are not interrupted
        // by other inputs that could mess up the cursor.
        sizeAppend /= sizeof(TCHAR);
        while (sizeAppend--)
        {
            INPUT_RECORD ir;

            ir.EventType = KEY_EVENT;
            ir.Event.KeyEvent.wRepeatCount = 1;
            ir.Event.KeyEvent.wVirtualKeyCode = VK_LEFT;
            ir.Event.KeyEvent.wVirtualScanCode = MapVirtualKeyW(VK_LEFT, MAPVK_VK_TO_VSC);
            ir.Event.KeyEvent.uChar.UnicodeChar = 0;
            ir.Event.KeyEvent.dwControlKeyState = 0;

            ir.Event.KeyEvent.bKeyDown = TRUE;
            WriteConsoleInput(hInput, &ir, 1, &dwWritten);

            ir.Event.KeyEvent.bKeyDown = FALSE;
            WriteConsoleInput(hInput, &ir, 1, &dwWritten);
        }

#endif

        /* Setup stuff for code reuse... */
        current = insert - str;
        /* Replace the TAB by NULL, and NULL-terminate the command */
        // *insert = _T('\0'); // str[charcount - count];
        str[charcount - 1] = _T('\0');
        /**/charcount--;/**/


        /* Expand current file name */

        /* Used to later see if we went down to the next line */
        tempscreen = charcount; // tempscreen == old_charcount

        CompletionRestarted = TRUE;
        Success = DoCompletion(&Context, str, current, maxlen, // charcount,
                               InputControl.dwControlKeyState, CheckForDirs,
                               &CompletionRestarted);
        if (!Success)
            MessageBeep(-1);

        if (bUseBashCompletion)
        {
            if (CompletionRestarted)
            {
                // if ((bUseBashCompletion && CompletionRestarted) || (!bUseBashCompletion))
                /* If first TAB, complete filename */

                // /* Used to later see if we went down to the next line */
                // tempscreen = charcount; // tempscreen == old_charcount

                /* Figure out where the cursor is going to be after we print it */
                current = charcount = _tcslen(str);

                /* Print out what we have now */
                PrintPartialPrompt(str, charcount, tempscreen,
                                   &orgx, &orgy, &curx, &cury);
            }
            else if (Success)
            {
                // if (bUseBashCompletion && !CompletionRestarted && Success)
                /* If second TAB, list matches */

                /* Restore the prompt with uncompleted command */
                PrintPrompt();
                GetCursorXY(&orgx, &orgy);
                ConOutPuts(str);

                /* Set cursor position */
                SetCursorXY((orgx + current) % maxx,
                             orgy + (orgx + current) / maxx);
                GetCursorXY(&curx, &cury);
            }
        }
        else
        {
            /* Figure out where the cursor is going to be after we print it */
            current = charcount = _tcslen(str);

            /* Print out what we have now */
            PrintPartialPrompt(str, charcount, tempscreen,
                               &orgx, &orgy, &curx, &cury);
        }

        InputControl.nInitialChars = _tcslen(str);

    } while (TRUE);

    /* Free the auto-completion context */
    FreeCompletionContext(&Context);

    /* Restore the original console input modes */
    SetConsoleMode(hInput, dwOldMode);
    return Success;
}

/* Read a command from the command line */
BOOL ReadCommand(LPTSTR str, DWORD maxlen)
{
    HANDLE hInput  = ConStreamGetOSHandle(StdIn);
    HANDLE hOutput = ConStreamGetOSHandle(StdOut);

    memset(str, 0, maxlen * sizeof(TCHAR));

#if 0
    if (IsConsoleHandle(hInput) && IsConsoleHandle(hOutput))
    {
        if (!ReadCommandFromConsole(hInput, hOutput, str, maxlen))
            return FALSE;
    }
    else
#endif
    if (IsTTYHandle(hInput) && IsTTYHandle(hOutput)) // Either hInput or hOutput can be console, but not both.
    {
        if (!ReadCommandFromTTY(hInput, hOutput, str, maxlen))
            return FALSE;
    }
    else
    {
        /* No console or TTY (this is a file) */
        if (!ReadCommandFromFile(hInput, /* hOutput, */ str, maxlen))
            return FALSE;
        // return TRUE;
    }

#ifdef FEATURE_ALIASES
    /* Expand all aliases */
    ExpandAlias(str, maxlen);
#endif /* FEATURE_ALIAS */
    return TRUE;
}
