/*
 *  CMDTABLE.C - table of internal commands.
 *
 *
 *  History:
 *
 *    16 Jul 1998 (Hans B Pufal)
 *        started.
 *        New file to keep the internal command table. I plan on
 *        getting rid of the table real soon now and replacing it
 *        with a dynamic mechanism.
 *
 *    27 Jul 1998  John P. Price
 *        added config.h include
 *
 *    21-Jan-1999 (Eric Kohl)
 *        Unicode ready!
 */

#include "precomp.h"

/*
 * A list of all the internal commands, associating their command names
 * to the functions to process them.
 */

// TODO: Mark the commands available only when extension are enabled, as such.
// (Use an extra flag).
COMMAND cmds[] =
{
    {_T("?"), 0, CommandShowCommands, {0}},

#ifdef INCLUDE_CMD_ACTIVATE
    {_T("activate"), 0, CommandActivate, {STRING_WINDOW_HELP2}},
#endif

#ifdef FEATURE_ALIASES
    {_T("alias"), 0, CommandAlias, {STRING_ALIAS_HELP}},
#endif

#ifdef INCLUDE_CMD_ASSOC
    {_T("assoc"), 0, CommandAssoc, {STRING_ASSOC_HELP}},
#endif

#ifdef INCLUDE_CMD_BEEP
    {_T("beep"), 0, cmd_beep, {STRING_BEEP_HELP}},
#endif

    {_T("call"), CMD_BATCHONLY, cmd_call, {STRING_CALL_HELP}},

#ifdef INCLUDE_CMD_CHDIR
    {_T("cd"), 0, cmd_chdir, {STRING_CD_HELP}},
    {_T("chdir"), 0, cmd_chdir, {STRING_CD_HELP}},
#endif

#ifdef INCLUDE_CMD_CHOICE
    {_T("choice"), 0, CommandChoice, {STRING_CHOICE_HELP}},
#endif

#ifdef INCLUDE_CMD_CLS
    {_T("cls"), 0, cmd_cls, {STRING_CLS_HELP}},
#endif

#ifdef INCLUDE_CMD_COLOR
    {_T("color"), 0, CommandColor, {STRING_COLOR_HELP1}},
#endif

#ifdef INCLUDE_CMD_COPY
    {_T("copy"), 0, cmd_copy, {STRING_COPY_HELP2}},
#endif

#ifdef INCLUDE_CMD_CTTY
    {_T("ctty"), 0, cmd_ctty, {STRING_CTTY_HELP}},
#endif

#ifdef INCLUDE_CMD_DATE
    {_T("date"), 0, cmd_date, {STRING_DATE_HELP4}},
#endif

#ifdef INCLUDE_CMD_DEL
    {_T("del"), 0, CommandDelete, {STRING_DEL_HELP1}},
    {_T("delete"), 0, CommandDelete, {STRING_DEL_HELP1}},
#endif

#ifdef INCLUDE_CMD_DELAY
    {_T("delay"), 0, CommandDelay, {STRING_DELAY_HELP}},
#endif

#ifdef INCLUDE_CMD_DIR
    {_T("dir"), 0, CommandDir, {STRING_DIR_HELP1}},
#endif

#ifdef FEATURE_DIRECTORY_STACK
    {_T("dirs"), 0, CommandDirs, {STRING_DIRSTACK_HELP3}},
#endif

    {_T("echo"), 0, CommandEcho, {STRING_ECHO_HELP4}},
    {_T("echos"), 0, CommandEchos, {STRING_ECHO_HELP1}},
    {_T("echoerr"), 0, CommandEchoerr, {STRING_ECHO_HELP2}},
    {_T("echoserr"), 0, CommandEchoserr, {STRING_ECHO_HELP3}},

    {_T("endlocal"), 0, cmd_endlocal, {0}},

#ifdef INCLUDE_CMD_DEL
    {_T("erase"), 0, CommandDelete, {STRING_DEL_HELP1}},
#endif

    {_T("exit"), 0, CommandExit, {STRING_EXIT_HELP}},

    {_T("for"), CMD_SPECIAL_PARSE, cmd_for, {STRING_FOR_HELP1}},

#ifdef INCLUDE_CMD_FREE
    {_T("free"), 0, CommandFree, {STRING_FREE_HELP2}},
#endif

    {_T("goto"), CMD_BATCHONLY, cmd_goto, {STRING_GOTO_HELP1}},

#ifdef FEATURE_HISTORY
    {_T("history"), 0, CommandHistory, {0}},
#endif

    {_T("if"), CMD_SPECIAL_PARSE, cmd_if, {STRING_IF_HELP1}},

#ifdef INCLUDE_CMD_MEMORY
    {_T("memory"), 0, CommandMemory, {STRING_MEMORY_HELP1}},
#endif

#ifdef INCLUDE_CMD_MKDIR
    {_T("md"), 0, cmd_mkdir, {STRING_MKDIR_HELP}},
    {_T("mkdir"), 0, cmd_mkdir, {STRING_MKDIR_HELP}},
#endif

#ifdef INCLUDE_CMD_MKLINK
    {_T("mklink"), 0, cmd_mklink, {STRING_MKLINK_HELP}},
#endif

#ifdef INCLUDE_CMD_MOVE
    {_T("move"), 0, cmd_move, {STRING_MOVE_HELP2}},
#endif

#ifdef INCLUDE_CMD_MSGBOX
    {_T("msgbox"), 0, CommandMsgbox, {STRING_MSGBOX_HELP}},
#endif

#ifdef INCLUDE_CMD_PATH
    {_T("path"), 0, cmd_path, {STRING_PATH_HELP1}},
#endif

#ifdef INCLUDE_CMD_PAUSE
    {_T("pause"), 0, cmd_pause, {STRING_PAUSE_HELP1}},
#endif

#ifdef FEATURE_DIRECTORY_STACK
    {_T("popd"), 0, CommandPopd, {STRING_DIRSTACK_HELP2}},
#endif

#ifdef INCLUDE_CMD_PROMPT
    {_T("prompt"), 0, cmd_prompt, {(UINT_PTR)cmd_prompt_help}},
#endif

#ifdef FEATURE_DIRECTORY_STACK
    {_T("pushd"), 0, CommandPushd, {STRING_DIRSTACK_HELP1}},
#endif

#ifdef INCLUDE_CMD_RMDIR
    {_T("rd"), 0, cmd_rmdir, {STRING_RMDIR_HELP}},
    {_T("rmdir"), 0, cmd_rmdir, {STRING_RMDIR_HELP}},
#endif

#ifdef INCLUDE_CMD_REM
    {_T("rem"), CMD_SPECIAL_PARSE, NULL, {STRING_REM_HELP}}, /* Dummy command */
#endif

#ifdef INCLUDE_CMD_RENAME
    {_T("ren"), 0, cmd_rename, {STRING_REN_HELP1}},
    {_T("rename"), 0, cmd_rename, {STRING_REN_HELP1}},
#endif

#ifdef INCLUDE_CMD_REPLACE
    {_T("replace"), 0, cmd_replace, {STRING_REPLACE_HELP1}},
#endif

#ifdef INCLUDE_CMD_SCREEN
    {_T("screen"), 0, CommandScreen, {STRING_SCREEN_HELP}},
#endif

#ifdef INCLUDE_CMD_SET
    {_T("set"), 0, cmd_set, {STRING_SET_HELP}},
#endif

    {_T("setlocal"), 0, cmd_setlocal, {0}},

    {_T("shift"), CMD_BATCHONLY, cmd_shift, {STRING_SHIFT_HELP}},

#ifdef INCLUDE_CMD_START
    {_T("start"), 0, cmd_start, {STRING_START_HELP1}},
#endif

#ifdef INCLUDE_CMD_TIME
    {_T("time"), 0, cmd_time, {STRING_TIME_HELP1}},
#endif

#ifdef INCLUDE_CMD_TIMER
    {_T("timer"), 0, CommandTimer, {(UINT_PTR)cmd_timer_help}},
#endif

#ifdef INCLUDE_CMD_TITLE
    {_T("title"), 0, cmd_title, {STRING_TITLE_HELP}},
#endif

#ifdef INCLUDE_CMD_TYPE
    {_T("type"), 0, cmd_type, {STRING_TYPE_HELP1}},
#endif

#ifdef INCLUDE_CMD_VER
    {_T("ver"), 0, cmd_ver, {STRING_VERSION_HELP1}},
#endif

#ifdef INCLUDE_CMD_VERIFY
    {_T("verify"), 0, cmd_verify, {STRING_VERIFY_HELP1}},
#endif

#ifdef INCLUDE_CMD_VOL
    {_T("vol"), 0, cmd_vol, {STRING_VOL_HELP4}},
#endif

#ifdef INCLUDE_CMD_WINDOW
    {_T("window"), 0, CommandWindow, {STRING_WINDOW_HELP1}},
#endif

    {NULL, 0, NULL, {0}}
};

VOID PrintCommandList(VOID)
{
    LPCOMMAND cmdptr;
    INT y;

    y = 0;
    cmdptr = cmds;
    while (cmdptr->name)
    {
        if (!(cmdptr->flags & CMD_HIDE))
        {
            if (++y == 8)
            {
                ConOutPuts(cmdptr->name);
                ConOutChar(_T('\n'));
                y = 0;
            }
            else
            {
                ConOutPrintf (_T("%-10s"), cmdptr->name);
            }
        }

        cmdptr++;
    }

    if (y != 0)
        ConOutChar(_T('\n'));
}

/* Handle help support for the internal command */
BOOL
CheckForHelp(
    IN LPCOMMAND cmdptr,
    IN LPCTSTR param)
{
    BOOL bHelp;
    LPCTSTR start;
    SIZE_T len;

    if ((cmdptr->func == CommandEcho)    || // "ECHO"
        (cmdptr->func == CommandEchos)   ||
        (cmdptr->func == CommandEchoerr) ||
        (cmdptr->func == CommandEchoserr)
#ifdef INCLUDE_CMD_SET
     || (cmdptr->func == cmd_set)           // "SET"
#endif
        )
    {
        /* /? must be in prefix within any of the first '/'-separated options */
        BOOL bExpectSwitch = TRUE;

        bHelp = FALSE;
        while (param)
        {
            start = TokStrIter(&param, &len, TS_DELIMS_AS_TOKENS,
                               TRUE, STANDARD_SEPS, _T("/"), NULL);
            if (!start)
                break;

            /* If we expect an option delimiter but we don't have one, bail out */
            if (bExpectSwitch && !(*start == _T('/') && len == 1))
                break;

            /* If we expect an option parameter and it starts with '?', help was found */
            if (!bExpectSwitch && (*start == _T('?')))
            {
                bHelp = TRUE;
                break;
            }

            /* Alternate between option delimiter and option parameter */
            bExpectSwitch ^= TRUE;
        }
    }
#ifdef INCLUDE_CMD_START
    else if (cmdptr->func == cmd_start)     // "START"
    {
        /* Help is handled internally by the command */
        bHelp = FALSE;
    }
#endif
    else if (cmdptr->flags & CMD_SPECIAL_PARSE)
        // "FOR" (cmd_for), "IF" (cmd_if), "REM" (NULL)
    {
        /* Special parsed command: FOR,IF,REM: /? must be the only option */
        bHelp = !_tcscmp(param, _T("/?"));
    }
    else
    {
        /* /? can be anywhere */
        BOOL bSeenSwitch = FALSE;

        bHelp = FALSE;
        while (param)
        {
            start = TokStrIter(&param, &len, TS_DELIMS_AS_TOKENS,
                               TRUE, STANDARD_SEPS, _T("/"), NULL);
            if (!start)
                break;

            if (!bSeenSwitch && (*start == _T('/') && len == 1))
            {
                bSeenSwitch = TRUE;
                continue;
            }

            /* If we have previously seen an option delimiter, and the current
             * option parameter starts with '?', help was found. */
            if (bSeenSwitch && (*start == _T('?')))
            {
                bHelp = TRUE;
                break;
            }

            /* This was an option parameter */
            bSeenSwitch = FALSE;
        }
    }

    return bHelp;
}

/* EOF */
