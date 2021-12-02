/*
 *  PROMPT.C - prompt handling.
 *
 *
 *  History:
 *
 *    14/01/95 (Tim Normal)
 *        started.
 *
 *    08/08/95 (Matt Rains)
 *        i have cleaned up the source code. changes now bring this source
 *        into guidelines for recommended programming practice.
 *
 *    01/06/96 (Tim Norman)
 *        added day of the week printing (oops, forgot about that!)
 *
 *    08/07/96 (Steffan Kaiser)
 *        small changes for speed
 *
 *    20-Jul-1998 (John P Price <linux-guru@gcfl.net>)
 *        removed redundant day strings. Use ones defined in date.c.
 *
 *    27-Jul-1998 (John P Price <linux-guru@gcfl.net>)
 *        added config.h include
 *
 *    28-Jul-1998 (John P Price <linux-guru@gcfl.net>)
 *        moved cmd_prompt from internal.c to here
 *
 *    09-Dec-1998 (Eric Kohl)
 *        Added help text ("/?").
 *
 *    14-Dec-1998 (Eric Kohl)
 *        Added "$+" option.
 *
 *    09-Jan-1999 (Eric Kohl)
 *        Added "$A", "$C" and "$F" option.
 *        Added locale support.
 *        Fixed "$V" option.
 *
 *    20-Jan-1999 (Eric Kohl)
 *        Unicode and redirection safe!
 *
 *    24-Jan-1999 (Eric Kohl)
 *        Fixed Win32 environment handling.
 *
 *    30-Apr-2005 (Magnus Olsen <magnus@greatlord.com>)
 *        Remove all hardcoded strings in En.rc
 */
#include "precomp.h"

/* The default prompt */
static TCHAR DefaultPrompt[] = _T("$P$G");

/*
 * Initialize prompt support.
 */
VOID InitPrompt(VOID)
{
    TCHAR Buffer[2];

    /*
     * Set the PROMPT environment variable if it doesn't exist already.
     * You can change the PROMPT environment variable before cmd starts.
     */
    if (GetEnvironmentVariable(_T("PROMPT"), Buffer, _countof(Buffer)) == 0)
        SetEnvironmentVariable(_T("PROMPT"), DefaultPrompt);
}

/*
 * Print an information line on top of the screen (for $I).
 */
static VOID PrintInfoLine(VOID)
{
#define FOREGROUND_WHITE (FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_INTENSITY)

    HANDLE hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    COORD coPos;
    DWORD dwWritten;

    PTSTR pszInfoLine = NULL;
    INT iInfoLineLen;

    /* Return directly if the output handle is not a console handle */
    if (!GetConsoleScreenBufferInfo(hOutput, &csbi))
        return;

    iInfoLineLen = LoadString(CMD_ModuleHandle, STRING_CMD_INFOLINE, (PTSTR)&pszInfoLine, 0);
    if (!pszInfoLine || iInfoLineLen == 0)
        return;

    /* Display the localized information line */
    coPos.X = 0;
    coPos.Y = 0;
    FillConsoleOutputAttribute(hOutput, BACKGROUND_BLUE | FOREGROUND_WHITE,
                               csbi.dwSize.X,
                               coPos, &dwWritten);
    FillConsoleOutputCharacter(hOutput, _T(' '),
                               csbi.dwSize.X,
                               coPos, &dwWritten);

    WriteConsoleOutputCharacter(hOutput, pszInfoLine, iInfoLineLen,
                                coPos, &dwWritten);
}

/*
 * Print the command-line prompt.
 */
/*****
 ** JdBP:

  $$          The $ (dollar) character
  $A  $a      The & (ampersand) character
  $B  $b      The | (bar) character
  $C  $c      The ( character
  $D          The date in standard ISO 8601 form
  $d          The date in your country's local form
  $E  $e      The escape character (ASCII 27)
  $F  $f      The ) character
  $G  $g      The > (greater than) character
  $H  $h      The backspace character (ASCII 8)
  $I  $i      The help banner
  $L  $l      The < (less than) character
  $M          The time in HH:MM form
  $m          The time in HH:MM:SS XM form (12-hour)
  $N  $n      The drive letter of the current drive
  $P  $p      The current directory on the current drive
  $Q  $q      The = (equals) character
  $R  $r      The return value of the most recent command
  $S  $s      The space character (ASCII 32)
  $T          The time in standard ISO 8601 form
  $t          The time in your country's local form
  $U          The timezone as an offset from UTC
  $u          The timezone name
  $V  $v      The command interpreter and operating system version
  $Xd $xd     The current directory on the drive 'd'
  $_          A carriage return and line-feed


DATE:
  By default, the date will be displayed in ISO 8601 standard form.

  /N  Do not prompt for a new date
  /F  Specify a format string to use when displaying the date
  /UTC          Display the date in UTC rather than in local time
  /LOCALFORMAT  Use the local country's date and time formats


DIR:
...
  /LOCALTZ      Display timestamps in local time rather than in UTC
  /LOCALFORMAT  Use the local country's date and time formats

TIME:
  By default, the time will be displayed in ISO 8601 standard form.

  /N  Do not prompt for a new time
  /F  Specify a format string to use when displaying the date
  /UTC          Display the time in UTC rather than in local time
  /LOCALFORMAT  Use the local country's date and time formats

*****/
VOID PrintPrompt(VOID)
{
    LPTSTR pr, Prompt;
    TCHAR szPrompt[256];
    TCHAR szPath[MAX_PATH];

    if (GetEnvironmentVariable(_T("PROMPT"), szPrompt, _countof(szPrompt)))
        Prompt = szPrompt;
    else
        Prompt = DefaultPrompt;

    /*
     * Special pre-handling for $I: If the information line is displayed
     * on top of the screen, ensure that the prompt won't be hidden below it.
     */
    for (pr = Prompt; *pr;)
    {
        if (*pr++ != _T('$'))
            continue;
        if (!*pr || _totupper(*pr++) != _T('I'))
            continue;

        if (GetCursorY() == 0)
            ConOutChar(_T('\n'));
        break;
    }

    /* Parse the prompt string */
    for (pr = Prompt; *pr; ++pr)
    {
        if (*pr != _T('$'))
        {
            ConOutChar(*pr);
        }
        else
        {
            ++pr;
            if (!*pr) break;
            switch (_totupper(*pr))
            {
                case _T('A'):
                    ConOutChar(_T('&'));
                    break;

                case _T('B'):
                    ConOutChar(_T('|'));
                    break;

                case _T('C'):
                    ConOutChar(_T('('));
                    break;

                case _T('D'):
                    // 4DOS/4NT: case-sensitive for different date formats.
                    // JdBP: $D: date in standard ISO 8601 form; $d: date in your country's local form.
                    ConOutPuts(GetDateString());
                    break;

                case _T('E'):
                    ConOutChar(_T('\x1B'));
                    break;

                case _T('F'):
                    ConOutChar(_T(')'));
                    break;

                case _T('G'):
                    ConOutChar(_T('>'));
                    break;

                case _T('H'):
                    ConOutPuts(_T("\x08 \x08"));
                    break;

                case _T('I'):
                    // TODO, when extended commands available: /* ReactOS-only: if no batch context active, print the info line */
                    PrintInfoLine();
                    break;

                // case _T('J'): // 4DOS/4NT: date in four-year ISO format.
                // case _T('K'): // 4DOS/4NT: date in ISO week date format.

                case _T('L'):
                    ConOutChar(_T('<'));
                    break;

#if 0
                case _T('M'):
                    // TODO: UNC name for current drive if remote (NT).
                    // 4DOS/4NT: current time w/o seconds (case-sensitive).
                    break;
#endif

                case _T('N'):
                {
                    GetCurrentDirectory(_countof(szPath), szPath);
                    ConOutChar(szPath[0]);
                    break;
                }

                case _T('P'):
                {
                    GetCurrentDirectory(_countof(szPath), szPath);
                    ConOutPuts(szPath);
                    break;
                }

                case _T('Q'):
                    ConOutChar(_T('='));
                    break;

#if 0
                case _T('R'):
                    // OS/2, 4DOS/4NT: Errorlevel.
                    ConOutPrintf(_T("%i"), nErrorLevel);
                    break;
#endif

                case _T('S'):
                    ConOutChar(_T(' '));
                    break;

                case _T('T'):
                    // 4DOS/4NT: another time format (case-sensitive).
                    // JdPB: $T: time in standard ISO 8601 form; $t: time in your country's local form
                    ConOutPuts(GetTimeString());
                    break;

                // case _T('U'):
                    // // 4DOS/4NT: Current user.
                    // // JdBP: $U: timezone as an offset from UTC; $u: timezone name
                    // break;

                case _T('V'):
                // case _T('X'): // Version revision
                    PrintOSVersion();
                    break;

                // case _T('W'):
                    // // 4DOS/4NT: working directory in truncated form (w/ ellipses) (case-sensitive format).
                    // break;

                // case _T('X'):
                    // // 4DOS/4NT: default dir for specified drive (case-sensitive format).
                    // break;

                // case _T('Z'):
                    // // 4DOS/4NT: shell nesting level.
                    // break;

                case _T('_'):
                    ConOutChar(_T('\n'));
                    break;

                case _T('$'):
                    ConOutChar(_T('$'));
                    break;

#ifdef FEATURE_DIRECTORY_STACK
                case _T('+'):
                {
                    INT i;
                    for (i = 0; i < GetDirectoryStackDepth(); i++)
                        ConOutChar(_T('+'));
                    break;
                }
#endif
            }
        }
    }
}


#ifdef INCLUDE_CMD_PROMPT

INT cmd_prompt(LPTSTR param)
{
    INT retval = 0;

    if (!_tcsncmp(param, _T("/?"), 2))
    {
        ConOutResPaging(TRUE, STRING_PROMPT_HELP1);
#ifdef FEATURE_DIRECTORY_STACK
        ConOutResPaging(FALSE, STRING_PROMPT_HELP2);
#endif
        ConOutResPaging(FALSE, STRING_PROMPT_HELP3);
        return 0;
    }

    /*
     * Set the PROMPT environment variable. If 'param' is NULL or is
     * an empty string (the user entered "prompt" only), then remove
     * the environment variable and therefore use the default prompt.
     * Otherwise, use the new prompt.
     */
    if (!SetEnvironmentVariable(_T("PROMPT"),
                                (param && param[0] != _T('\0') ? param : NULL)))
    {
        retval = 1;
    }

    if (BatType != CMD_TYPE)
    {
        if (retval != 0)
            nErrorLevel = retval;
    }
    else
    {
        nErrorLevel = retval;
    }

    return retval;
}
#endif

/* EOF */
