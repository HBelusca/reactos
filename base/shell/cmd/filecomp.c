/*
 *  FILECOMP.C - handles filename completion.
 *
 *
 *  Comments:
 *
 *    30-Jul-1998 (John P Price <linux-guru@gcfl.net>)
 *       moved from command.c file
 *       made second TAB display list of filename matches
 *       made filename be lower case if last character typed is lower case
 *
 *    25-Jan-1999 (Eric Kohl)
 *       Cleanup. Unicode safe!
 *
 *    30-Apr-2004 (Filip Navara <xnavara@volny.cz>)
 *       Make the file listing readable when there is a lot of long names.
 *

 *    05-Jul-2004 (Jens Collin <jens.collin@lakhei.com>)
 *       Now expands lfn even when trailing " is omitted.
 */

/*
 * IMPLEMENTATION NOTE:
 *    When a new autocompletion is started, the list of enumerated
 *    files/directories is cached such that it can be directly reused
 *    between two autocompletion attempts on the same string.
 *    This reduces unnecessary disk or network I/O, thus less latency.
 *    The downside, of course, is that if a file/directory is created/removed
 *    between two successive autocompletion attempts, we do not notice it.
 */

#include "precomp.h"
#include <strsafe.h>

typedef struct _FILE_COMPLETION_CONTEXT
{
#define ITEMS_INCREMENT 32

    BOOL OnlyDirs;              // Only enumerate directories, not files
    DWORD TotalItems;           // Total items in list (for memory management)
    PWIN32_FIND_DATA FileList;  // List of all the files
    DWORD NumberOfFiles;        // Real number of files enumerated in list

} FILE_COMPLETION_CONTEXT, *PFILE_COMPLETION_CONTEXT;

static BOOL
BuildFileList(
    IN PFILE_COMPLETION_CONTEXT FileContext,
    IN LPTSTR szSearchPathSpec)
{
    PWIN32_FIND_DATA OldFileList;
    WIN32_FIND_DATA file;
    HANDLE hFile;

    hFile = FindFirstFile(szSearchPathSpec, &file);
    if (hFile == INVALID_HANDLE_VALUE)
        return FALSE;

    /* Find anything */
    do
    {
        /* Ignore "." and ".." */
        if (!_tcscmp(file.cFileName, _T(".")) ||
            !_tcscmp(file.cFileName, _T("..")))
        {
            continue;
        }

        /* Keep only directories if needed */
        if (FileContext->OnlyDirs &&
            !(file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            continue;
        }

        /* Initialize the list of files if needed */
        if (FileContext->FileList == NULL)
        {
            FileContext->TotalItems = ITEMS_INCREMENT;
            FileContext->FileList = cmd_alloc(FileContext->TotalItems * sizeof(WIN32_FIND_DATA));
            if (FileContext->FileList == NULL)
            {
                // FileContext->FileList = NULL;
                FileContext->TotalItems = 0;

                FileContext->NumberOfFiles = 0;

                WARN("DEBUG: Cannot allocate memory for FileContext->FileList!\n");
                // error_out_of_memory();
                // ConErrFormatMessage(GetLastError());
                FindClose(hFile);
                return FALSE;
            }
        }
        else
        {
            OldFileList = FileContext->FileList;

            FileContext->TotalItems += ITEMS_INCREMENT;
            FileContext->FileList = cmd_realloc(FileContext->FileList, FileContext->TotalItems * sizeof(WIN32_FIND_DATA));
            if (FileContext->FileList == NULL)
            {
                /* Do not leak old buffer */
                cmd_free(OldFileList);
                // FileContext->FileList = NULL;
                FileContext->TotalItems = 0;

                FileContext->NumberOfFiles = 0;

                WARN("DEBUG: Cannot reallocate memory for FileContext->FileList!\n");
                // error_out_of_memory();
                // ConErrFormatMessage(GetLastError());
                FindClose(hFile);
                return FALSE;
            }
        }

        /* Add the file to the list of files */
        // memcpy(FileContext->FileList[FileContext->NumberOfFiles++], file, sizeof(WIN32_FIND_DATA));
        FileContext->FileList[FileContext->NumberOfFiles++] = file;
    }
    while (FindNextFile(hFile, &file));

    FindClose(hFile);

    return TRUE;
}

static VOID
FreeFileList(IN PFILE_COMPLETION_CONTEXT FileContext)
{
    if (FileContext->FileList == NULL)
        return;

    cmd_free(FileContext->FileList);
    FileContext->FileList = NULL;
    FileContext->TotalItems = 0;
    FileContext->NumberOfFiles = 0;
}

static VOID
FreeFileCompletionContext(IN PCOMPLETION_CONTEXT Context)
{
    FreeFileList((PFILE_COMPLETION_CONTEXT)Context->CompleterContext);
}

int __cdecl compare(const void *arg1, const void *arg2)
{
    // return _tcsicmp(((PWIN32_FIND_DATA)arg1)->cFileName, ((PWIN32_FIND_DATA)arg2)->cFileName);
    return lstrcmpi(((PWIN32_FIND_DATA)arg1)->cFileName,
                    ((PWIN32_FIND_DATA)arg2)->cFileName);
}



/*****************************************************************************\
 **            B A S H - S T Y L E   A U T O C O M P L E T I O N            **
\*****************************************************************************/

typedef BOOL (*COMPLETE_IN_FILELIST)(PCOMPLETION_CONTEXT, LPTSTR, LPTSTR);
typedef BOOL (*COMPLETE_IN_CMDLIST)(PCOMPLETION_CONTEXT, LPTSTR);

typedef struct _FILE_COMPLETION_BASH_CONTEXT
{
    FILE_COMPLETION_CONTEXT;
} FILE_COMPLETION_BASH_CONTEXT, *PFILE_COMPLETION_BASH_CONTEXT;

static BOOL
ShowFileList(
    IN PCOMPLETION_CONTEXT Context,
    IN OUT LPTSTR str,
    IN LPTSTR directory)
{
    PFILE_COMPLETION_BASH_CONTEXT FileContext =
        (PFILE_COMPLETION_BASH_CONTEXT)Context->CompleterContext;

    UINT i, count;
    SHORT screenwidth;
    UINT longestfname = 0;
    TCHAR fname[MAX_PATH];

    /* Get the size of longest filename first */
    for (i = 0; i < FileContext->NumberOfFiles; ++i)
    {
        if (_tcslen(FileContext->FileList[i].cFileName) > longestfname)
        {
            longestfname = _tcslen(FileContext->FileList[i].cFileName);
            /* Directories get extra brackets around them. */
            if (FileContext->FileList[i].dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                longestfname += 2;
        }
    }

    /* Count the highest number of columns */
    GetScreenSize(&screenwidth, NULL);

    /* For counting columns of output */
    count = 0;

    /* Increase by the number of spaces behind file name */
    longestfname += 3;

    ConOutPrintfPaging(TRUE, _T(""));
    ConOutPrintfPaging(FALSE, _T("\n"));

    /* Display the files */
    for (i = 0; i < FileContext->NumberOfFiles; ++i)
    {
        if (FileContext->FileList[i].dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            _stprintf(fname, _T("[%s]"), FileContext->FileList[i].cFileName);
        else
            _tcscpy(fname, FileContext->FileList[i].cFileName);

        // ConOutPrintfPaging(FALSE, _T("%*s"), -(int)longestfname, fname);
        ConOutPrintfPaging(FALSE, L"%s\n", fname); // FIXME!!!!!
        count++;
        // /* output as much columns as fits on the screen */
        // if (count >= (screenwidth / longestfname))
        // {
            // /* print the new line only if we aren't on the
             // * last column, in this case it wraps anyway */
            // if (count * longestfname != (UINT)screenwidth)
                // ConOutPrintfPaging(FALSE, _T("\n"));
            // count = 0;
        // }
    }

    if (count)
        ConOutPrintfPaging(FALSE, _T("\n"));

    return TRUE;
}

static BOOL
ShowCommandList(
    IN PCOMPLETION_CONTEXT Context,
    IN OUT LPTSTR str)
{
    LPCOMMAND cmdptr;
    INT count = 0;

    ConOutPrintfPaging(TRUE, _T(""));
    ConOutPrintfPaging(FALSE, _T("\n"));

    for (cmdptr = cmds; cmdptr->name; cmdptr++)
    {
        if (!_tcsnicmp(str, cmdptr->name, _tcslen(str)))
        {
            /* Return the match only if it is unique */
            // if (_tcsnicmp(str, (cmdptr+1)->name, _tcslen(str)))
                // _tcscpy(str, cmdptr->name);
            ConOutPrintfPaging(FALSE, _T("%s\n"), cmdptr->name);
            count++;
        }
    }

    return TRUE;
}

/* TRUE/FALSE: File is / is not found */
static BOOL
SearchInFileList(
    IN PCOMPLETION_CONTEXT Context,
    IN OUT LPTSTR str, // FIXME: Needs a 'charcount'
    IN LPTSTR directory)
{
    PFILE_COMPLETION_BASH_CONTEXT FileContext =
        (PFILE_COMPLETION_BASH_CONTEXT)Context->CompleterContext;

    UINT i, count;
    BOOL  perfectmatch = TRUE;
    TCHAR maxmatch[MAX_PATH] = _T("");
    TCHAR fname[MAX_PATH];

    for (i = 0; i < FileContext->NumberOfFiles; ++i)
    {
        _tcscpy(fname, FileContext->FileList[i].cFileName);

        if (FileContext->FileList[i].dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            _tcscat(fname, _T("\\"));

        if (!maxmatch[0] && perfectmatch)
        {
            /* Initialize maxmatch */
            _tcscpy(maxmatch, fname);
        }
        else
        {
            for (count = 0; maxmatch[count] && fname[count]; count++)
            {
                if (tolower(maxmatch[count]) != tolower(fname[count]))
                {
                    perfectmatch = FALSE;
                    maxmatch[count] = _T('\0');
                    break;
                }
            }

            if (maxmatch[count] == _T('\0') && fname[count] != _T('\0'))
                perfectmatch = FALSE;
        }
    }

    /* Only quote if the filename contains spaces */
    if (_tcschr(directory, _T(' ')) || _tcschr(maxmatch, _T(' ')))
    {
        *str = _T('\"');
        _tcscpy(str + 1, directory); // == _tcscat(str, directory);
        _tcscat(str, maxmatch);
        _tcscat(str, _T("\""));
    }
    else
    {
        _tcscpy(str, directory);
        _tcscat(str, maxmatch);
    }

    /* Return TRUE or FALSE if file is / is not found */
    return perfectmatch;
}

/* TRUE/FALSE: Command is / is not found */
static BOOL
SearchInCommandList(
    IN PCOMPLETION_CONTEXT Context,
    IN OUT LPTSTR str) // FIXME: Needs a 'charcount'
{
    LPCOMMAND cmdptr;

    for (cmdptr = cmds; cmdptr->name; cmdptr++)
    {
        if (!_tcsnicmp(str, cmdptr->name, _tcslen(str)))
        {
            /* Return the match only if it is unique */
            if (_tcsnicmp(str, (cmdptr+1)->name, _tcslen(str)))
                _tcscpy(str, cmdptr->name);
            // break;
            return TRUE;
        }
    }

    /* No match found */
    return FALSE;
}

/*
 * Returns TRUE if at least one match, otherwise returns FALSE
 */
static BOOL
CompleteFilenameBashEx(
    IN PCOMPLETION_CONTEXT Context,
    IN OUT LPTSTR str,
    IN UINT cursor,
    IN UINT charcount, // FIXME: Unused!!!!!
    IN BOOL OnlyDirs,
    IN BOOL RestartCompletion,
    IN COMPLETE_IN_FILELIST CompleteInFileList,
    IN COMPLETE_IN_CMDLIST  CompleteInCmdList)
{
    PFILE_COMPLETION_BASH_CONTEXT FileContext;

    WIN32_FIND_DATA file;
    HANDLE hFile;
    INT   curplace = 0;
    INT   start;
    INT   count;
    INT   step;
    INT   c = 0;
    BOOL  found_dot = FALSE;
    TCHAR path[MAX_PATH];
    TCHAR directory[MAX_PATH];

    if (RestartCompletion)
    {
        Context->CompleterCleanup = FreeFileCompletionContext;

        FileContext = cmd_alloc(sizeof(FILE_COMPLETION_BASH_CONTEXT));
        if (FileContext == NULL)
        {
            WARN("DEBUG: Cannot allocate memory for Context->CompleterContext!\n");
            // error_out_of_memory();
            // ConErrFormatMessage(GetLastError());
            return FALSE;
        }

        FileContext->OnlyDirs = OnlyDirs;

        FileContext->FileList = NULL;
        FileContext->TotalItems = 0;
        FileContext->NumberOfFiles = 0;

        Context->CompleterContext = FileContext;
    }
    else
    {
        FileContext = (PFILE_COMPLETION_BASH_CONTEXT)Context->CompleterContext;
    }

    /* Expand current file name */
    if (cursor == 0)
        count = 0; // Well it's already a problem that our buffer size cursor is == 0...
    else
        count = cursor - 1;

    /* find how many '"'s there is typed already */
    step = count;
    while (step > 0)
    {
        if (str[step] == _T('"'))
            c++;
        step--;
    }
    /* if c is odd, then user typed " before name, else not */

    /* find front of word */
    if (str[count] == _T('"') || (c % 2))
    {
        if (count > 0)
            count--;
        while (count > 0 && str[count] != _T('"'))
            count--;
    }
    else
    {
        while (count > 0 && str[count] != _T(' '))
            count--;
    }

    /* if not at beginning, go forward 1 */
    if (str[count] == _T(' '))
        count++;

    start = count;

    if (str[count] == _T('"'))
        count++;    /* don't increment start */

    /* Check whether the user hits TAB again, if so cut off the diff length */
    if (RestartCompletion)
    {
        /* extract directory from word */
        _tcscpy(directory, &str[count]);
        curplace = _tcslen(directory) - 1;

        if (curplace >= 0 && directory[curplace] == _T('"'))
            directory[curplace--] = _T('\0');

        _tcscpy(path, directory);

        while (curplace >= 0 &&
               directory[curplace] != _T('\\') &&
               directory[curplace] != _T('/') &&
               directory[curplace] != _T(':'))
        {
            directory[curplace--] = _T('\0');
        }

        /* look for a '.' in the filename */
        for (count = _tcslen(directory); path[count] != _T('\0'); count++)
        {
            if (path[count] == _T('.'))
            {
                found_dot = TRUE;
                break;
            }
        }

        if (found_dot)
            _tcscat(path, _T("*"));
        else
            _tcscat(path, _T("*.*"));

        // MessageBox(NULL, path, _T("path (UNIX)"), 0);

        /* Purge the old file list */
        // if (FileContext->FileList != NULL)
        FreeFileList((PFILE_COMPLETION_CONTEXT)FileContext);

        hFile = FindFirstFile(path, &file);
        FindClose(hFile);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            /* Build the file list for this path */
            if (!BuildFileList((PFILE_COMPLETION_CONTEXT)FileContext, path))
            {
                /* An unexpected error happened */
                // ConErrFormatMessage(GetLastError());
                return FALSE;
            }

            /* Check the size of the list to see if we found any matches */
            if (FileContext->NumberOfFiles == 0)
            {
                // if (FileContext->FileList != NULL)
                FreeFileList((PFILE_COMPLETION_CONTEXT)FileContext);
                // return FALSE;
            }
            else
            {
                /* Sort the files */
                qsort(FileContext->FileList, FileContext->NumberOfFiles,
                      sizeof(WIN32_FIND_DATA), compare);
            }
        }
    }
    else
    {
        /* Restore previous context */
    }

    /*
     * Check if the file list contains files or not, and perform the completion
     * accordingly (using the file list or falling back to the command list).
     */
    if (FileContext->NumberOfFiles != 0)
    {
        /* Attempt to search and complete the command, or show the file list */
        return CompleteInFileList(Context, &str[start], directory);
    }
    else
    {
        /*
         * No match found; search for internal command and attempt to search
         * and complete the command, or show the command list.
         */
        return CompleteInCmdList(Context, &str[start]);
    }
}

static BOOL
CompleteFilenameBash(
    IN PCOMPLETION_CONTEXT Context,
    IN OUT LPTSTR str,
    IN UINT cursor,
    IN UINT charcount,
    IN ULONG ControlKeyState, // Unused in CompleteFilenameBashEx...
    IN BOOL OnlyDirs,
    IN BOOL RestartCompletion)
{
    if (RestartCompletion)
    {
        // CompleteFilename
        return CompleteFilenameBashEx(Context, str, cursor, charcount,
                                      OnlyDirs, RestartCompletion,
                                      SearchInFileList, SearchInCommandList);
    }
    else
    {
        // ShowCompletionMatches
        return CompleteFilenameBashEx(Context, str, cursor, charcount,
                                      OnlyDirs, RestartCompletion,
                                      ShowFileList, ShowCommandList);
    }
}



/*****************************************************************************\
 **           N T C M D - S T Y L E   A U T O C O M P L E T I O N           **
\*****************************************************************************/

typedef struct _FILE_COMPLETION_CMD_CONTEXT
{
    FILE_COMPLETION_CONTEXT;

    /* Save the strings used last time, so if the user hits TAB again */
    TCHAR LastPrefix[MAX_PATH];
    /* Keeps track of what element was last selected */
    INT Sel;

} FILE_COMPLETION_CMD_CONTEXT, *PFILE_COMPLETION_CMD_CONTEXT;

static VOID
FindPrefixAndSuffix(
    IN LPTSTR strIN,
    OUT LPTSTR szPrefix,
    OUT LPTSTR szSuffix)
{
    /* String that is to be examined */
    TCHAR str[MAX_PATH];
    /* temp pointers to used to find needed parts */
    TCHAR* szSearch;
    TCHAR* szSearch1;
    TCHAR* szSearch2;
    TCHAR* szSearch3;
    /* Number of quotes in the string */
    INT nQuotes = 0;
    /* Used in for loops */
    UINT i;
    /* Char number to break the string at */
    INT PBreak = 0; // Path break
    INT SBreak = 0; // Space break
    /* Tells whether you are inside quotes ot not */
    BOOL InsideQuotes = FALSE;

    szPrefix[0] = _T('\0');
    szSuffix[0] = _T('\0');

    /* Copy over the string to later be edited */
    _tcscpy(str, strIN);

    /* Count number of " */
    // while (_tcschr(strIter, _T('\"'))) { nQuotes++; }
    for (i = 0; i < _tcslen(str); i++)
    {
        if (str[i] == _T('\"'))
            nQuotes++;
    }

    /* Find the prefix and suffix */
    if (nQuotes % 2 /* && nQuotes >= 1 */)
    {
        /*
         * Odd number of quotes. Just start from the last " .
         * This is the way MS does it, and is an easy way out.
         */
        szSearch = _tcsrchr(str, _T('\"'));
        /* Move to the next char past the " */
        szSearch++;
        // Or we know that "suffix" starts from 'szSearch' to NULL-termination
        _tcscpy(szSuffix, szSearch);

        /* Find the one closest to end */
        szSearch1 = _tcsrchr(str, _T('\"')); // Note: Must be equal to old szSearch by construction.
        szSearch2 = _tcsrchr(str, _T('\\'));
        szSearch3 = _tcsrchr(str, _T('.'));
        if ((szSearch2 != NULL) && (szSearch1 < szSearch2))
            szSearch = szSearch2;
        else if ((szSearch3 != NULL) && (szSearch1 < szSearch3))
            szSearch = szSearch3;
        else
            szSearch = szSearch1;
        /* Move one char past */
        szSearch++;
        szSearch[0] = _T('\0');

        // Or we know that "prefix" starts from 'str' to 'szSearch' included (NULL-terminated)
        _tcscpy(szPrefix, str);
        return;
    }

    /*
     * Even number of quotes.
     */

    if (!_tcschr(str, _T(' ')))
    {
        /* No spaces, everything goes to Suffix */
        // Or we know that "suffix" starts from 'str' to NULL-termination
        _tcscpy(szSuffix, str);

        /* Look for a slash just in case */
        szSearch = _tcsrchr(str, _T('\\'));
        if (szSearch)
        {
            /* Move one char past */
            szSearch++;
            szSearch[0] = _T('\0');

            // Or we know that "prefix" starts from 'str' to 'szSearch' included (NULL-terminated)
            _tcscpy(szPrefix, str);
        }
        else
        {
            // Or we know that "prefix" starts from 'str' to 'str' (i.e. empty)
            szPrefix[0] = _T('\0');
        }

        return;
    }

    if (!nQuotes)
    {
        /* No quotes, and there is a space. Take it after the last space. */
        szSearch = _tcsrchr(str, _T(' '));
        szSearch++;
        // Or we know that "suffix" starts from 'szSearch' to NULL-termination
        _tcscpy(szSuffix, szSearch);

        /* Find the closest to the end space or \ */
        szSearch1 = _tcsrchr(str, _T(' '));
        szSearch2 = _tcsrchr(str, _T('\\'));
        szSearch3 = _tcsrchr(str, _T('/'));
        if ((szSearch2 != NULL) && (szSearch1 < szSearch2))
            szSearch = szSearch2;
        else if ((szSearch3 != NULL) && (szSearch1 < szSearch3))
            szSearch = szSearch3;
        else
            szSearch = szSearch1;
        /* Move one char past */
        szSearch++;
        szSearch[0] = _T('\0');

        // Or we know that "prefix" starts from 'str' to 'szSearch' included (NULL-terminated)
        _tcscpy(szPrefix, str);
        return;
    }

    /* All else fails and there is a lot of quotes, spaces and |
       Then we search through and find the last space or \ that is
        not inside a quotes */
    for (i = 0; i < _tcslen(str); i++)
    {
        if (str[i] == _T('\"'))
            InsideQuotes = !InsideQuotes;
        if (str[i] == _T(' ') && !InsideQuotes)
            SBreak = i;
        if ((str[i] == _T(' ') || str[i] == _T('\\')) && !InsideQuotes)
            PBreak = i;
    }
    SBreak++;
    PBreak++;

    _tcscpy(szSuffix, &str[SBreak]);

    str[PBreak] = _T('\0');
    _tcscpy(szPrefix, str);
    i = _tcslen(szPrefix);
    if (i >= 2 && szPrefix[i - 2] == _T('\"') && szPrefix[i - 1] != _T(' '))
    {
        /* need to remove the " right before a \ at the end to
           allow the next stuff to stay inside one set of quotes
            otherwise you would have multiple sets of quotes*/
        _tcscpy(&szPrefix[i - 2], _T("\\"));
    }
}

// TODO: Transform into a macro, or put directly inside the code.
static BOOL
FileNameContainsSpecialCharacters(LPTSTR pszFileName)
{
    // pszFileName is NULL-terminated.
    // \xB4 is '´'
    // What about "\"&<>@^|" ?? (see cmd.c!GetCmdLineCommand)
    //
    // Original XP : " &()[]{}^=;!%'+,`~"
    return (_tcspbrk(pszFileName, _T(" !%&(){}[]=\'`,;^~+\xB4")) != NULL);
}

/*
 * strIN : "prefix" string that needs completion (unmodified here);
 *         everything that is after it is untouched.
 * bNext : direction flag;
 * strOut: resulting string.
 */
static BOOL
CompleteFilenameCmd(
    IN PCOMPLETION_CONTEXT Context,
    IN OUT LPTSTR str,
    IN UINT cursor,
    IN UINT charcount,
    IN ULONG ControlKeyState,
    IN BOOL OnlyDirs,
    IN BOOL RestartCompletion)
{
    PFILE_COMPLETION_CMD_CONTEXT FileContext;

    BOOL bNext = !(ControlKeyState & SHIFT_PRESSED);

    /* Length of string before we complete it */
    SIZE_T StartLength;

    /* Used to find and assemble the string that is returned */
    TCHAR szPrefix[MAX_PATH];
    TCHAR szBaseWord[MAX_PATH];

    /* Used to search for files */
    TCHAR szSearchPath[MAX_PATH];
    HANDLE hFile;
    WIN32_FIND_DATA file;

    /* Used for loops */
    UINT i;
    BOOL NeededQuote = FALSE;

    if (RestartCompletion)
    {
        Context->CompleterCleanup = FreeFileCompletionContext;

        FileContext = cmd_alloc(sizeof(FILE_COMPLETION_CMD_CONTEXT));
        if (FileContext == NULL)
        {
            WARN("DEBUG: Cannot allocate memory for Context->CompleterContext!\n");
            // error_out_of_memory();
            // ConErrFormatMessage(GetLastError());
            return FALSE;
        }

        FileContext->OnlyDirs = OnlyDirs;

        FileContext->FileList = NULL;
        FileContext->TotalItems = 0;
        FileContext->NumberOfFiles = 0;

        // memset(FileContext->LastPrefix, 0, FileContext->LastPrefix);
        FileContext->Sel = 0;

        Context->CompleterContext = FileContext;
    }
    else
    {
        FileContext = (PFILE_COMPLETION_CMD_CONTEXT)Context->CompleterContext;
    }

    /* Check whether the cursor is not at the end of the string */
    if ((cursor + 1) < _tcslen(str))
        str[cursor] = _T('\0');

    /* Check whether the user hits TAB again, if so cut off the diff length */
    if (RestartCompletion)
    {
        /* We need to know how many chars we added from the start */
        StartLength = _tcslen(str);

        /* No string, we need all files in that directory */
        if (!StartLength)
            _tcscat(str, _T("*")); // FIXME: Is it really needed????

        /* Zero it out first */
        szPrefix[0] = _T('\0');
        szBaseWord[0] = _T('\0');

        /* What comes out of this needs to be:
           szBaseWord =  path no quotes to the object
           szPrefix = what leads up to the filename
           no quote at the END of the full name */
        FindPrefixAndSuffix(str, szPrefix, szBaseWord);
        /* Strip quotes */
        for (i = 0; i < _tcslen(szBaseWord); )
        {
            if (szBaseWord[i] == _T('\"'))
                memmove(&szBaseWord[i], &szBaseWord[i + 1], _tcslen(&szBaseWord[i]) * sizeof(TCHAR));
            else
                i++;
        }

        /* Clear it out */
        memset(szSearchPath, 0, sizeof(szSearchPath));

        /* Start the search for all the files */
        GetFullPathName(szBaseWord, ARRAYSIZE(szSearchPath), szSearchPath, NULL);

        /* Got a device path? Fallback to the the current dir plus the short path */
        if (szSearchPath[0] == _T('\\') && szSearchPath[1] == _T('\\') &&
            szSearchPath[2] == _T('.')  && szSearchPath[3] == _T('\\'))
        {
            GetCurrentDirectory(ARRAYSIZE(szSearchPath), szSearchPath);
            StringCchCat(szSearchPath, ARRAYSIZE(szSearchPath), _T("\\"));
            StringCchCat(szSearchPath, ARRAYSIZE(szSearchPath), szBaseWord);
        }

        if (StartLength > 0)
            _tcscat(szSearchPath, _T("*"));

        /* Save for future usage */
        StringCchCopy(FileContext->LastPrefix, ARRAYSIZE(FileContext->LastPrefix), szPrefix);


        // MessageBox(NULL, szSearchPath, _T("path (NT)"), 0);

        /* Purge the old file list */
        // if (FileContext->FileList != NULL)
        FreeFileList((PFILE_COMPLETION_CONTEXT)FileContext);

        /* Search for the files it might be */
        hFile = FindFirstFile(szSearchPath, &file);
        FindClose(hFile);
        if (hFile == INVALID_HANDLE_VALUE)
            return FALSE;

        // /* Build a list of all files names */
        /* Build the file list for this path */
        /* Don't show files when the user uses directory-related commands */
        if (!BuildFileList((PFILE_COMPLETION_CONTEXT)FileContext, szSearchPath))
        {
            /* An unexpected error happened */
            // ConErrFormatMessage(GetLastError());
            return FALSE;
        }

        /* Check the size of the list to see if we found any matches */
        if (FileContext->NumberOfFiles == 0)
        {
            // if (FileContext->FileList != NULL)
            FreeFileList((PFILE_COMPLETION_CONTEXT)FileContext);
            return FALSE;
        }

        /* Sort the files */
        qsort(FileContext->FileList, FileContext->NumberOfFiles,
              sizeof(WIN32_FIND_DATA), compare);
    }
    else
    {
        /* Restore previous context */
        StringCchCopy(szPrefix, ARRAYSIZE(szPrefix), FileContext->LastPrefix);
        StartLength = 0;
    }

    /* Find the next/previous */
    if (!RestartCompletion)
    {
        if (bNext)
        {
            // FileContext->Sel = (++FileContext->Sel % FileContext->NumberOfFiles);
            if (FileContext->Sel >= FileContext->NumberOfFiles - 1)
                FileContext->Sel = 0;
            else
                FileContext->Sel++;
        }
        else
        {
            // FileContext->Sel = (--FileContext->Sel % FileContext->NumberOfFiles);
            if (FileContext->Sel == 0)
                FileContext->Sel = FileContext->NumberOfFiles - 1;
            else
                FileContext->Sel--;
        }
    }
    else
    {
        if (bNext)
            FileContext->Sel = 0;
        else
            FileContext->Sel = FileContext->NumberOfFiles - 1;
    }

    /* Special character in the name */
    if (FileNameContainsSpecialCharacters(FileContext->FileList[FileContext->Sel].cFileName))
    {
        INT LastSpace;
        BOOL InsideQuotes;
        /* It needs a " at the end */
        NeededQuote = TRUE;
        LastSpace = -1;
        InsideQuotes = FALSE;

        /* Find the place to put the " at the start */
        for (i = 0; i < _tcslen(szPrefix); i++)
        {
            if (szPrefix[i] == _T('\"'))
                InsideQuotes = !InsideQuotes;
            if (szPrefix[i] == _T(' ') && !InsideQuotes)
                LastSpace = i;
        }

        /* Insert the quotation and move things around */
        if (szPrefix[LastSpace + 1] != _T('\"') && LastSpace != -1)
        {
            // Shift the contents of prefix by 1 char to the right
            memmove(&szPrefix[LastSpace+1], &szPrefix[LastSpace],
                    (_tcslen(szPrefix)-LastSpace+1) * sizeof(TCHAR));

            if ((UINT)(LastSpace + 1) == _tcslen(szPrefix))
            {
                StringCchCat(szPrefix, ARRAYSIZE(szPrefix), _T("\""));
            }
            szPrefix[LastSpace + 1] = _T('\"');
        }
        else if (LastSpace == -1)
        {
            /* Add quotation only if none exists already */
            if (szPrefix[0] != _T('\"'))
            {
                // szBaseWord is used here as a temporary variable
                StringCchCopy(szBaseWord, ARRAYSIZE(szBaseWord), _T("\""));
                StringCchCat(szBaseWord, ARRAYSIZE(szBaseWord), szPrefix);
                StringCchCopy(szPrefix, ARRAYSIZE(szPrefix), szBaseWord);
            }
        }
    }

    StringCchCopy(str, charcount, szPrefix);
    StringCchCat(str, charcount, FileContext->FileList[FileContext->Sel].cFileName);

    /* Check for odd number of quotes means we need to close them */
    if (!NeededQuote)
    {
        for (i = 0; i < _tcslen(str); i++)
        {
            if (str[i] == _T('\"'))
                NeededQuote = !NeededQuote;
        }
    }

    if (NeededQuote || (_tcslen(szPrefix) && szPrefix[_tcslen(szPrefix) - 1] == _T('\"')))
        StringCchCat(str, charcount, _T("\""));

    return TRUE;
}


typedef BOOL (*COMPLETE_FILENAME)(
    IN PCOMPLETION_CONTEXT Context,
    IN OUT LPTSTR str,
    IN UINT cursor,
    IN UINT charcount,
    IN ULONG ControlKeyState,
    IN BOOL OnlyDirs,
    IN BOOL RestartCompletion);

/*
 * NOTE: Currently we only perform filename/directory autocompletion.
 * Maybe one day we will be able to perform per-command autocompletion.
 */
/*
 * FIXME: Per-command context ????
 *
 * TODO: When intelligent per-command auto-completion is implemented,
 * the 'CompleterContext' may become a generic list of objects (mappable to strings)
 * that is used by the per-command autocompleter to complete the command line.
 * Also the following member should then be added:
 *     LPCOMMAND cmd;
 * being the command being completed.
 */
/*
 * strIN : "prefix" string that needs completion (unmodified here);
 *         everything that is after it is untouched.
 * bNext : direction flag;
 * strOut: resulting string.
 */
BOOL
CompleteFilename(
    IN PCOMPLETION_CONTEXT Context,
    IN PVOID CompleterParameter OPTIONAL,
    IN OUT LPTSTR str, // == CmdLine
    IN UINT cursor,
    IN UINT charcount,
    IN TCHAR CompletionChar,
    IN ULONG ControlKeyState,
    IN OUT PBOOL RestartCompletion)
{
    BOOL OnlyDirs;

    /*
     * Set up the real filename completer the first time we run this function.
     * It will then remain the same during all this session (bUseBashCompletion
     * must not change either).
     */
    // static
    COMPLETE_FILENAME CompleteFilenameProc =
        (bUseBashCompletion ? CompleteFilenameBash : CompleteFilenameCmd);

    LPTSTR line = str;

    /*
     * Since 'AutoCompletionChar' has priority over 'PathCompletionChar'
     * (as they can be the same), we perform the checks in this order.
     */
    OnlyDirs = FALSE;
    if (CompletionChar == AutoCompletionChar)
        OnlyDirs = FALSE;
    else if (CompletionChar == PathCompletionChar)
        OnlyDirs = TRUE;

    //
    // FIXME BIG IMPROVEMENT:
    // Find the *real* command start within CmdLine !!
    // (useful in case CmdLine == "some command && cd <completion>" .
    // In that case the current code would check 'some' and think
    // we can complete files+dirs, whereas we really want only dirs
    // because the real command here starts at 'cd').
    // Valid for both UNIX and NT completion. CmdLine and charcount
    // must then be readjusted internally.
    //

    /* Trim leading whitespace */
    while (_istspace(*line))
        line++;

    /* We will only list directories for directory-related commands */
    if ( !_tcsnicmp(line, _T("cd "), 3) ||
         !_tcsnicmp(line, _T("md "), 3) ||
         !_tcsnicmp(line, _T("rd "), 3) ||
         !_tcsnicmp(line, _T("chdir "), 6) ||
         !_tcsnicmp(line, _T("mkdir "), 6) ||
         !_tcsnicmp(line, _T("rmdir "), 6) ||
         !_tcsnicmp(line, _T("pushd "), 6) )
    {
        OnlyDirs = TRUE;
    }

    if (Context->CompleterContext)
    {
        PFILE_COMPLETION_CONTEXT FileContext =
            (PFILE_COMPLETION_CONTEXT)Context->CompleterContext;

        *RestartCompletion =
            *RestartCompletion || (FileContext->OnlyDirs != OnlyDirs);
    }

    if (*RestartCompletion)
    {
        /* Call the completer cleaning routine */
        if (Context->CompleterCleanup)
            Context->CompleterCleanup(Context);
        Context->CompleterCleanup = NULL;

        if (Context->CompleterContext)
            cmd_free(Context->CompleterContext);
        Context->CompleterContext = NULL;
    }

    return CompleteFilenameProc(Context, str, cursor, charcount,
                                ControlKeyState, OnlyDirs, *RestartCompletion);
}
