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

/* Similar to memchr, but searches from the end backwards */
#if defined (_UNICODE) || defined (UNICODE)
const void* wmemrchr(
#else
const void* memrchr(
#endif
    const void* buf,
    int c,
    size_t count)
{
    LPCTSTR szBuf = (LPCTSTR)buf;
    szBuf += count;
    while (count--)
    {
        --szBuf;
        if (*szBuf == (TCHAR)c)
            return szBuf;
    }
    return NULL;
}

#if defined (_UNICODE) || defined (UNICODE)
#define _tmemchr    wmemchr
#define _tmemrchr   wmemrchr
#else
#define _tmemchr    memchr
#define _tmemrchr   memrchr
#endif


static int
__cdecl
compare(const void *arg1, const void *arg2)
{
    // return _tcsicmp(((PWIN32_FIND_DATA)arg1)->cFileName, ((PWIN32_FIND_DATA)arg2)->cFileName);
    return lstrcmpi(((PWIN32_FIND_DATA)arg1)->cFileName,
                    ((PWIN32_FIND_DATA)arg2)->cFileName);
}

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
    HANDLE hFile;
    PWIN32_FIND_DATA OldFileList;
    WIN32_FIND_DATA file;

    ASSERT(FileContext->FileList == NULL);
    // FileContext->FileList = NULL;
    FileContext->TotalItems = 0;
    FileContext->NumberOfFiles = 0;

    hFile = FindFirstFile(szSearchPathSpec, &file);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        /*
         * We cannot find any file with this path specification,
         * but this is not a "fatal" error. The caller will know
         * that because NumberOfFiles == 0.
         */
        return TRUE;
    }

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
            FileContext->NumberOfFiles = 0;

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
        else if (FileContext->NumberOfFiles >= FileContext->TotalItems)
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

    /* Now sort the list if it's not empty */
    if (FileContext->NumberOfFiles != 0)
    {
        qsort(FileContext->FileList, FileContext->NumberOfFiles,
              sizeof(WIN32_FIND_DATA), compare);
    }

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

    /* Free the allocated private completer context */
    if (Context->CompleterContext)
        cmd_free(Context->CompleterContext);
    Context->CompleterContext = NULL;
}


// TODO: Transform into a macro, or put directly inside the code.
static BOOL
FileNameContainsSpecialCharacters(IN LPCTSTR pszFileName)
{
    // pszFileName is NULL-terminated.
    // \xB4 is '´'
    // What about "\"&<>@^|" ?? (see cmd.c!GetCmdLineCommand)
    //
    // Original XP : " &()[]{}^=;!%'+,`~"
    return (_tcspbrk(pszFileName, _T(" !%&(){}[]=\'`,;^~+\xB4")) != NULL);
}


/*****************************************************************************\
 **            B A S H - S T Y L E   A U T O C O M P L E T I O N            **
\*****************************************************************************/

typedef struct _FILE_COMPLETION_BASH_CONTEXT
{
    FILE_COMPLETION_CONTEXT;

    UINT start;
    TCHAR directory[MAX_PATH];

} FILE_COMPLETION_BASH_CONTEXT, *PFILE_COMPLETION_BASH_CONTEXT;

static BOOL
ShowFileList(
    IN PCOMPLETION_CONTEXT Context)
{
    PFILE_COMPLETION_BASH_CONTEXT FileContext =
        (PFILE_COMPLETION_BASH_CONTEXT)Context->CompleterContext;

    UINT i, count;
    // SHORT screenwidth;
    UINT longestfname = 0;
    TCHAR fname[MAX_PATH];

    /* Get the size of longest filename first */
    for (i = 0; i < FileContext->NumberOfFiles; ++i)
    {
        count = _tcslen(FileContext->FileList[i].cFileName);
        if (count > longestfname)
        {
            longestfname = count;
            /* Directories get extra brackets around them. */
            if (FileContext->FileList[i].dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                longestfname += 2;
        }
    }

    // /* Count the highest number of columns */
    // GetScreenSize(&screenwidth, NULL);

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
    IN LPCTSTR str)
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

    if (count)
        ConOutPrintfPaging(FALSE, _T("\n"));

    return TRUE;
}

/* TRUE/FALSE: File is / is not found */
static BOOL
CompleteFromFileList(
    IN PCOMPLETION_CONTEXT Context,
    OUT LPTSTR str,
    IN UINT charcount,
    IN LPCTSTR directory)
{
    PFILE_COMPLETION_BASH_CONTEXT FileContext =
        (PFILE_COMPLETION_BASH_CONTEXT)Context->CompleterContext;

    UINT i, count;
    BOOL  perfectmatch = TRUE;
    TCHAR maxmatch[MAX_PATH] = _T("");
    TCHAR fname[MAX_PATH];

    for (i = 0; i < FileContext->NumberOfFiles; ++i)
    {
        StringCchCopy(fname, ARRAYSIZE(fname), FileContext->FileList[i].cFileName);

        if (FileContext->FileList[i].dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            StringCchCat(fname, ARRAYSIZE(fname), _T("\\"));

        if (!maxmatch[0] && perfectmatch)
        {
            /* Initialize maxmatch */
            StringCchCopy(maxmatch, ARRAYSIZE(maxmatch), fname);
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

    /*
     * We are now overwriting the old string line, and everything after
     * the completed part will be erased. If we are in insertion mode,
     * this erased part will be restored by our caller.
     */

    /* Only quote if the filename contains special characters */
    if (FileNameContainsSpecialCharacters(directory) ||
        FileNameContainsSpecialCharacters(maxmatch))
    {
        /*
         * Check whether we have enough space for completion before doing anything.
         * Take into account for the NULL-termination.
         */
        if (1 + _tcslen(directory) + _tcslen(maxmatch) + 1 + 1 > charcount)
            return FALSE;

        Context->Touched = TRUE;

        StringCchCopy(str, charcount, _T("\""));
        StringCchCat(str, charcount, directory);
        StringCchCat(str, charcount, maxmatch);
        StringCchCat(str, charcount, _T("\""));
    }
    else
    {
        /*
         * Check whether we have enough space for completion before doing anything.
         * Take into account for the NULL-termination.
         */
        if (_tcslen(directory) + _tcslen(maxmatch) + 1 > charcount)
            return FALSE;

        Context->Touched = TRUE;

        StringCchCopy(str, charcount, directory);
        StringCchCat(str, charcount, maxmatch);
    }

    /* Return TRUE or FALSE if file is / is not found */
    // return perfectmatch;
    return TRUE;
}

/* TRUE/FALSE: Command is / is not found */
static BOOL
CompleteFromCommandList(
    IN PCOMPLETION_CONTEXT Context,
    OUT LPTSTR str,
    IN UINT charcount)
{
    HRESULT hr;
    LPCOMMAND cmdptr;

    for (cmdptr = cmds; cmdptr->name; ++cmdptr)
    {
        if (!_tcsnicmp(str, cmdptr->name, _tcslen(str)))
        {
            /* Return the match only if it is unique */
            if (_tcsnicmp(str, (cmdptr+1)->name, _tcslen(str)))
            {
                /*
                 * We are now overwriting the old string line, and everything after
                 * the completed part will be erased. If we are in insertion mode,
                 * this erased part will be restored by our caller.
                 */
                hr = StringCchCopy(str, charcount, cmdptr->name);
                if (FAILED(hr))
                    return FALSE;
                Context->Touched = TRUE;
            }
            // break;
            return TRUE;
        }
    }

    /* No match found */
    return FALSE;
}


static BOOL
PrepareCompletionContext_Bash(
    IN OUT PCOMPLETION_CONTEXT Context,
    IN BOOL RestartCompletion,
    IN ULONG ControlKeyState,
    IN BOOL OnlyDirs)
{
    PFILE_COMPLETION_BASH_CONTEXT FileContext;

    if (!RestartCompletion)
    {
        ASSERT(Context->CompleterContext);
        return TRUE;
    }

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

    return TRUE;
}

#if 0
static VOID
FindPrefixAndSuffix_Bash(
    IN LPCTSTR str,
    IN UINT charcount,
/*
    OUT PUINT pnPrefixLength,
    OUT LPCTSTR* pszSuffix, // szBaseWord
    OUT PUINT pnSuffixLength
*/
    OUT PUINT pnSuffix)
{
    UINT count;
    UINT step;
    UINT c = 0;

    /* Expand current file name */
    if (charcount == 0)
        count = 0; // Well it's already a problem that our buffer size charcount is == 0...
    else
        count = charcount - 1;

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

    *pnSuffix = count;
}
#endif

/*
 * Returns TRUE if at least one match, otherwise returns FALSE
 */
static BOOL
CompleteFilename_Bash(
    IN PCOMPLETION_CONTEXT Context,
    IN BOOL RestartCompletion,
    OUT LPTSTR str,
    IN UINT charcount)
{
    PFILE_COMPLETION_BASH_CONTEXT FileContext =
        (PFILE_COMPLETION_BASH_CONTEXT)Context->CompleterContext;

    ASSERT(FileContext);

    str += FileContext->start;
    charcount -= FileContext->start;

    /*
     * Check if the file list contains files or not, and perform the completion
     * accordingly (using the file list or falling back to the command list).
     */
    if (FileContext->NumberOfFiles != 0)
    {
        /* Attempt to search and complete the command, or show the file list */
        if (RestartCompletion)  // CompleteFilename
            return CompleteFromFileList(Context, str, charcount, FileContext->directory);
        else                    // ShowCompletionMatches
            return ShowFileList(Context);
    }
    else
    {
        /*
         * No match found; search for internal command and attempt to search
         * and complete the command, or show the command list.
         */
        if (RestartCompletion)  // CompleteFilename
            return CompleteFromCommandList(Context, str, charcount);
        else                    // ShowCompletionMatches
            return ShowCommandList(Context, str);
    }
}

static VOID
PrepareCompletion_Bash(
    IN PFILE_COMPLETION_CONTEXT FileContext,
    IN LPCTSTR str,
    IN UINT charcount,
    IN UINT nPrefixLength,
    IN LPCTSTR pszSuffix,
    IN UINT nSuffixLength,
    IN UINT cursor,
    OUT LPTSTR pszSearchPath,
    IN UINT nSearchPathLength)
{
    PFILE_COMPLETION_BASH_CONTEXT pFileContext =
        (PFILE_COMPLETION_BASH_CONTEXT)FileContext;

    UINT count = pszSuffix - str;
    INT curplace = 0;
    BOOL found_dot = FALSE;

#if 0
    /*
     * Check whether we are inside quotes or not...
     * See also FindPrefixAndSuffix().
     */
    FindPrefixAndSuffix_Bash(str, cursor, &count);
#endif

    pFileContext->start = count;

    if (str[count] == _T('"'))
    {
        count++;    /* don't increment start */
        nSuffixLength--;
    }

    /* extract directory from word */
    StringCchCopyN(pFileContext->directory,
                   ARRAYSIZE(pFileContext->directory),
                   str + count, nSuffixLength);
    curplace = _tcslen(pFileContext->directory) - 1;

    /* remove one possible trailing quote */
    if (curplace >= 0 && pFileContext->directory[curplace] == _T('"'))
        pFileContext->directory[curplace--] = _T('\0');

    StringCchCopy(pszSearchPath, nSearchPathLength, pFileContext->directory);

    while (curplace >= 0 &&
           pFileContext->directory[curplace] != _T('\\') &&
           pFileContext->directory[curplace] != _T('/') &&
           pFileContext->directory[curplace] != _T(':'))
    {
        pFileContext->directory[curplace--] = _T('\0');
    }

    /* look for a '.' in the filename */
    for (count = _tcslen(pFileContext->directory);
         pszSearchPath[count] != _T('\0'); count++)
    {
        if (pszSearchPath[count] == _T('.'))
        {
            found_dot = TRUE;
            break;
        }
    }

    if (found_dot)
        StringCchCat(pszSearchPath, nSearchPathLength, _T("*"));
    else
        StringCchCat(pszSearchPath, nSearchPathLength, _T("*.*"));
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
    /* Direction flag */
    BOOL bNext;

} FILE_COMPLETION_CMD_CONTEXT, *PFILE_COMPLETION_CMD_CONTEXT;

static BOOL
PrepareCompletionContext_Cmd(
    IN PCOMPLETION_CONTEXT Context,
    IN BOOL RestartCompletion,
    IN ULONG ControlKeyState,
    IN BOOL OnlyDirs)
{
    PFILE_COMPLETION_CMD_CONTEXT FileContext;

    if (!RestartCompletion)
    {
        ASSERT(Context->CompleterContext);

        FileContext = (PFILE_COMPLETION_CMD_CONTEXT)Context->CompleterContext;
        FileContext->bNext = !(ControlKeyState & SHIFT_PRESSED);
        return TRUE;
    }

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

    FileContext->LastPrefix[0] = _T('\0');
    FileContext->Sel = 0;

    FileContext->bNext = !(ControlKeyState & SHIFT_PRESSED);

    Context->CompleterContext = FileContext;

    return TRUE;
}

/*
 * str - Input string to analyse.
 * charcount - Length of the buffer pointed by str.
 * pnPrefixLength - Length of "prefix" string, from the beginning of 'str'
 *                  up to the point of interest.
 * ??
 */
static VOID
FindPrefixAndSuffix(
    IN LPCTSTR str,
    IN UINT charcount,
    OUT PUINT pnPrefixLength,
    OUT LPCTSTR* pszSuffix, // szBaseWord // pnSuffixIndex,
    OUT PUINT pnSuffixLength)
{
    /* Temp pointers used to find needed parts */
    LPCTSTR szSearch, szSearch1, szSearch2;
    /* Number of quotes in the string */
    UINT nQuotes = 0;
    /* Used in for loops */
    UINT i;
    /* Char number to break the string at */
    INT PBreak = 0; // Path break
    INT SBreak = 0; // Space break

    /* Tells whether you are inside quotes ot not */
    BOOL InsideQuotes = FALSE;
    INT LastQuote = -1;

    /* Zero the strings out first */
    *pnPrefixLength = 0;
    *pszSuffix = NULL;
    *pnSuffixLength = 0;

    /**/charcount = min(charcount, _tcslen(str));/**/

    /* Count number of quotes */
    for (i = 0; i < charcount; i++)
    {
        if (str[i] == _T('\"'))
        {
            InsideQuotes = !InsideQuotes;
            LastQuote = i;
            nQuotes++;
        }
    }

    /* Find the prefix and suffix */

    /* Odd number of quotes */
    if (nQuotes % 2)
    {
        /*
         * Odd number of quotes. Just start from the last quote.
         * This is the way MS does it, and is an easy way out.
         */
        ASSERT(InsideQuotes);

        szSearch = str + LastQuote; // _tcsrchr(str, _T('\"'));

        /* Move to the next char past the quote */
        // We know that "suffix" starts from 'szSearch' to NULL-termination
        charcount = charcount - (szSearch + 1 - str);
        *pszSuffix = szSearch + 1;
        *pnSuffixLength = charcount;

        /* Find the one closest to end */
        szSearch1 = _tmemrchr(szSearch + 1, _T('\\'), charcount);
        szSearch2 = _tmemrchr(szSearch + 1, _T('/'), charcount);
        if (szSearch1 != NULL) // '\\' has precedence over '/'
            szSearch = szSearch1;
        else if (szSearch2 != NULL)
            szSearch = szSearch2;

        /* Move one char past */
        szSearch++;

        /* "prefix" starts from 'str' to 'szSearch' included */
        *pnPrefixLength = szSearch - str;
        return;
    }

    /* Even number of quotes */

    /* No spaces */
    // szSearch = _tmemrchr(str, _T(' '), charcount);
    // if (!szSearch)
    if (!_tmemchr(str, _T(' '), charcount))
    {
        /* No spaces, everything goes to Suffix */
        // We know that "suffix" starts from 'str' to NULL-termination
        *pszSuffix = str;
        *pnSuffixLength = charcount;

        /* Look for a slash just in case */
        szSearch  = NULL;
        szSearch1 = _tmemrchr(str, _T('\\'), charcount);
        szSearch2 = _tmemrchr(str, _T('/'), charcount);
        if (szSearch1 != NULL) // '\\' has precedence over '/'
            szSearch = szSearch1;
        else if (szSearch2 != NULL)
            szSearch = szSearch2;

        if (szSearch)
        {
            /* Move one char past */
            szSearch++;

            /* "prefix" starts from 'str' to 'szSearch' included */
            *pnPrefixLength = szSearch - str;
        }
        else
        {
            /* "prefix" is empty */
            *pnPrefixLength = 0;
        }

        return;
    }

    /* Zero quotes */
    if (!nQuotes)
    {
        /* No quotes, and there is a space. Take it after the last space. */
        szSearch = _tmemrchr(str, _T(' '), charcount);

        // We know that "suffix" starts from 'szSearch' to NULL-termination
        charcount = charcount - (szSearch + 1 - str);
        *pszSuffix = szSearch + 1;
        *pnSuffixLength = charcount;

        /* Find the closest to the end space or \ */
        szSearch1 = _tmemrchr(szSearch + 1, _T('\\'), charcount);
        szSearch2 = _tmemrchr(szSearch + 1, _T('/'), charcount);
        if (szSearch1 != NULL) // '\\' has precedence over '/'
            szSearch = szSearch1;
        else if (szSearch2 != NULL)
            szSearch = szSearch2;

        /* Move one char past */
        szSearch++;

        /* "prefix" starts from 'str' to 'szSearch' included */
        *pnPrefixLength = szSearch - str;
        return;
    }

    /*
     * All else fails: we have an even and non-zero number of quotes
     * and spaces. Then we search through and find the last space or \
     * that is not inside quotes.
     */
    InsideQuotes = FALSE;
    for (i = 0; i < charcount; i++)
    {
        if (str[i] == _T('\"'))
            InsideQuotes = !InsideQuotes;
        if (!InsideQuotes)
        {
            if (str[i] == _T(' '))
                SBreak = i;
            if (str[i] == _T(' ') || str[i] == _T('\\') || str[i] == _T('/'))
                PBreak = i;
        }
    }
    SBreak++;
    PBreak++;

    *pszSuffix = str + SBreak;
    *pnSuffixLength = charcount - SBreak;

    *pnPrefixLength = PBreak;

#if 0
    if (PBreak >= 2 && szPrefix[PBreak - 2] == _T('\"') && szPrefix[PBreak - 1] != _T(' '))
    {
        /*
         * Need to remove the " right before a \ at the end to allow
         * the next stuff to stay inside one set of quotes, otherwise
         * you would have multiple sets of quotes.
         */
        _tcscpy(&szPrefix[PBreak - 2], _T("\\"));
    }
#endif
}

static BOOL
CompleteFilename_Cmd(
    IN PCOMPLETION_CONTEXT Context,
    IN BOOL RestartCompletion,
    OUT LPTSTR str,
    IN UINT charcount)
{
    PFILE_COMPLETION_CMD_CONTEXT FileContext =
        (PFILE_COMPLETION_CMD_CONTEXT)Context->CompleterContext;

    /* Used to find and assemble the string that is returned */
    TCHAR szPrefix[MAX_PATH];

    /* Used for loops */
    UINT count;
    UINT i;
    BOOL NeededQuote = FALSE;

    ASSERT(FileContext);

    /* Check the size of the list to see if we have found any matches */
    if (FileContext->NumberOfFiles == 0)
        return FALSE;

    /* Find the next/previous */
    if (!RestartCompletion)
    {
        if (FileContext->bNext)
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
        if (FileContext->bNext)
            FileContext->Sel = 0;
        else
            FileContext->Sel = FileContext->NumberOfFiles - 1;
    }

    /* Restore previous context */
    StringCchCopy(szPrefix, ARRAYSIZE(szPrefix), FileContext->LastPrefix);

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
        count = _tcslen(szPrefix);
        for (i = 0; i < count; i++)
        {
            if (szPrefix[i] == _T('\"'))
                InsideQuotes = !InsideQuotes;
            if (szPrefix[i] == _T(' ') && !InsideQuotes)
                LastSpace = i;
        }

        /* Insert the quotation only if none exists already, and move things around */
        if (LastSpace != -1 && szPrefix[LastSpace + 1] != _T('\"'))
        {
            // Shift the contents of prefix by 1 char to the right
            memmove(&szPrefix[LastSpace+1], &szPrefix[LastSpace],
                    (count-LastSpace+1) * sizeof(TCHAR));

            szPrefix[LastSpace + 1] = _T('\"');
        }
        // Almost this: /* Note, that when LastSpace precisely == -1, this case corresponds to the previous one! */
        else if (LastSpace == -1 && szPrefix[0] != _T('\"'))
        {
            // Shift the contents of prefix by 1 char to the right
            memmove(&szPrefix[1], &szPrefix[0],
                    (count-0+1) * sizeof(TCHAR));

            szPrefix[0] = _T('\"');
        }
    }

    /*
     * We are now overwriting the old string line, and everything after
     * the completed part will be erased. If we are in insertion mode,
     * this erased part will be restored by our caller.
     */
    Context->Touched = TRUE;

    StringCchCopy(str, charcount, szPrefix);
    StringCchCat(str, charcount, FileContext->FileList[FileContext->Sel].cFileName);

    /* Check for odd number of quotes means we need to close them */
    if (!NeededQuote)
    {
        count = _tcslen(str);
        for (i = 0; i < count; i++)
        {
            if (str[i] == _T('\"'))
                NeededQuote = !NeededQuote;
        }
    }

    count = _tcslen(szPrefix);
    if (NeededQuote || (count && szPrefix[count - 1] == _T('\"')))
        StringCchCat(str, charcount, _T("\""));

    return TRUE;
}

static VOID
PrepareCompletion_Cmd(
    IN PFILE_COMPLETION_CONTEXT FileContext,
    IN LPCTSTR str,
    IN UINT charcount,
    IN UINT nPrefixLength,
    IN LPCTSTR pszSuffix,
    IN UINT nSuffixLength,
    IN UINT cursor,
    OUT LPTSTR pszSearchPath,
    IN UINT nSearchPathLength)
{
    PFILE_COMPLETION_CMD_CONTEXT pFileContext =
        (PFILE_COMPLETION_CMD_CONTEXT)FileContext;

    /* Used for loops */
    UINT i;
    UINT count;

    /* Used to search for files */
    TCHAR szBaseWord[MAX_PATH];

#if 0
    /* Length of string before we complete it */
    /* We need to know how many chars we added from the start */
    SIZE_T StartLength;
    // StartLength = _tcslen(str);
    StartLength = cursor;
#endif

    /* No string, we need all files in that directory */
    // if (!StartLength)
        // _tcscat(str, _T("*")); // FIXME: Is it really needed????

    StringCchCopyN(pFileContext->LastPrefix,
                   ARRAYSIZE(pFileContext->LastPrefix),
                   str, nPrefixLength);
    if (nPrefixLength >= 2 &&
        pFileContext->LastPrefix[nPrefixLength - 2] == _T('\"') &&
        // pFileContext->LastPrefix[nPrefixLength - 1] != _T(' ')
        (pFileContext->LastPrefix[nPrefixLength - 1]  == _T('\\') ||
         pFileContext->LastPrefix[nPrefixLength - 1] == _T('/')))
    {
        /*
         * Need to remove the " right before a \ at the end to allow
         * the next stuff to stay inside one set of quotes, otherwise
         * you would have multiple sets of quotes.
         */
        _tcscpy(&pFileContext->LastPrefix[nPrefixLength - 2], _T("\\"));
    }

    StringCchCopyN(szBaseWord, ARRAYSIZE(szBaseWord), pszSuffix, nSuffixLength);

    /* Strip quotes */
    for (i = 0, count = _tcslen(szBaseWord); i < count; )
    {
        if (szBaseWord[i] == _T('\"'))
        {
            memmove(&szBaseWord[i], &szBaseWord[i + 1],
                    _tcslen(&szBaseWord[i]) * sizeof(TCHAR));
            count = _tcslen(szBaseWord);
        }
        else
        {
            i++;
        }
    }

    /* Clear it out */
    memset(pszSearchPath, 0, nSearchPathLength * sizeof(TCHAR));

#if 0
    /* Start the search for all the files */
    GetFullPathName(szBaseWord, nSearchPathLength, pszSearchPath, NULL);
    //
    // Known pitfall: when running on e.g. "..\\cmd\\a ", with a space,
    // the space is not taken into account!
    //

    /*
     * Corner case: Have we got a device path?
     * If so, fall back to the the current dir plus the short path.
     */
    if (!_tcsnicmp(pszSearchPath, _T("\\\\.\\"), 4))
    {
        GetCurrentDirectory(nSearchPathLength, pszSearchPath);
        StringCchCat(pszSearchPath, nSearchPathLength, _T("\\"));
        StringCchCat(pszSearchPath, nSearchPathLength, szBaseWord);
    }
#else
    StringCchCopy(pszSearchPath, nSearchPathLength, szBaseWord);
#endif

    // if (StartLength > 0)
        StringCchCat(pszSearchPath, nSearchPathLength, _T("*"));
}


typedef BOOL (*PREPARE_COMPLETION_CONTEXT)(
    IN PCOMPLETION_CONTEXT Context,
    IN BOOL RestartCompletion,
    IN ULONG ControlKeyState,
    IN BOOL OnlyDirs);

typedef VOID (*PREPARE_COMPLETION)(
    IN PFILE_COMPLETION_CONTEXT FileContext,
    IN LPCTSTR str,
    IN UINT charcount,
    IN UINT nPrefixLength,
    IN LPCTSTR pszSuffix,
    IN UINT nSuffixLength,
    IN UINT cursor,
    OUT LPTSTR pszSearchPath,
    IN UINT nSearchPathLength);

typedef BOOL (*COMPLETE_FILENAME)(
    IN PCOMPLETION_CONTEXT Context,
    IN BOOL RestartCompletion,
    IN OUT LPTSTR str,
    IN UINT charcount);

/*
 * NOTE: Currently we only perform filename/directory autocompletion.
 * Maybe one day we will be able to perform per-command autocompletion.
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
    IN UINT charcount,
    IN UINT cursor,
    IN TCHAR CompletionChar,
    IN ULONG ControlKeyState,
    // OUT LPTSTR* CompletingWord,
    IN OUT PBOOL RestartCompletion)
{
    /*
     * Set up the real filename completer the first time we run this function.
     * It will then remain the same during all this session (bUseBashCompletion
     * must not change either).
     */
    PREPARE_COMPLETION_CONTEXT PrepareCompletionContext =
        (bUseBashCompletion ? PrepareCompletionContext_Bash : PrepareCompletionContext_Cmd);
    PREPARE_COMPLETION PrepareCompletion =
        (bUseBashCompletion ? PrepareCompletion_Bash : PrepareCompletion_Cmd);
    COMPLETE_FILENAME CompleteFilenameProc =
        (bUseBashCompletion ? CompleteFilename_Bash : CompleteFilename_Cmd);

    PFILE_COMPLETION_CONTEXT FileContext;


    BOOL Success;
    BOOL OnlyDirs;

    UINT nPrefixLength = 0;
    LPCTSTR pszSuffix  = NULL;
    UINT nSuffixLength = 0;

    /* Sanity checks */
    ASSERT(Context);
    ASSERT(str);
    ASSERT(cursor < charcount);

    /*
     * Since 'AutoCompletionChar' has priority over 'PathCompletionChar'
     * (as they can be the same), we perform the checks in this order.
     */
    OnlyDirs = FALSE;
    if (Context->CompleterContext)
    {
        PFILE_COMPLETION_CONTEXT FileContext =
            (PFILE_COMPLETION_CONTEXT)Context->CompleterContext;

        OnlyDirs = FileContext->OnlyDirs;
    }
    else
    {
        if (CompletionChar == AutoCompletionChar)
            OnlyDirs = FALSE;
        else if (CompletionChar == PathCompletionChar)
            OnlyDirs = TRUE;
    }

    /*
     * Do the splitting only if the text line has changed
     * before the completion (insertion) point.
     */
    if (*RestartCompletion)
    {
        LPCTSTR line;

        /* What comes out of this needs to be:
           szBaseWord = path no quotes to the object;
           szPrefix   = what leads up to the filename;
           no quote at the END of the full name */
        FindPrefixAndSuffix(str, cursor,
                            &nPrefixLength,  // szPrefix
                            &pszSuffix,      // szBaseWord
                            &nSuffixLength);

        /*
         * Attempt to find the start of the command being completed within CmdLine.
         * For example, when: CmdLine == "some command && cd <completion>",
         * the start of the command would be "cd".
         * Based on this information we can perform completion depending on the
         * context: in this example, the "cd" command would prefer to get only
         * a directory specification. Therefore we will only complete these.
         */
        //
        // NOTE / FIXME? :
        // CmdLine and charcount must then be readjusted internally?
        //
        line = str;
        if (nPrefixLength > 0)
            line += nPrefixLength - 1;
        if (str < pszSuffix && nSuffixLength > 0)
            line = min(line, pszSuffix - 1);

        /* Skip any whitespace / separator */
        while (str < line && _istspace(*line) /* or another separator?? */)
            --line;

        /* Go back at the beginning of the alphabetic word */
        while (str < line && _istalpha(*line))
            --line;
        if (!_istalpha(*line))
            ++line;

        /*
         * Check whether the user is completing a path for a directory-related
         * command. In this case, do not show files but only list directories.
         */
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

#if 0
        /*
         * Reverse the behaviour if PathCompletionChar is pressed
         * while we want to complete only directories.
         */
        if (CompletionChar == PathCompletionChar)
            OnlyDirs = !OnlyDirs;
#endif
    }

    /* Cleanup the existing file completion context if we restart a new completion */
    if (*RestartCompletion)
    {
#if 1
        /* Call the completer cleaning routine */
        if (Context->CompleterCleanup)
            Context->CompleterCleanup(Context);
        Context->CompleterCleanup = NULL;
#else
        if (Context->CompleterContext)
            FreeFileList((PFILE_COMPLETION_CONTEXT)Context->CompleterContext);
#endif
    }



    /* Prepare the completion */
    if (!PrepareCompletionContext(Context, *RestartCompletion,
                                  ControlKeyState, OnlyDirs))
    {
        Success = FALSE;
        goto Quit;
    }
    ASSERT(Context->CompleterContext);
    FileContext = (PFILE_COMPLETION_CONTEXT)Context->CompleterContext;

    /* Check whether the user hits TAB again, if so cut off the diff length */
    if (*RestartCompletion)
    {
        /* Used to search for files */
        TCHAR szSearchPath[MAX_PATH];

        PrepareCompletion(FileContext,
                          str, charcount,
                          nPrefixLength,
                          pszSuffix, nSuffixLength,
                          cursor,
                          szSearchPath, ARRAYSIZE(szSearchPath));

        /* Purge the old file list */
        FreeFileList(FileContext);

        /* Build the file list for this path */
        if (!BuildFileList(FileContext, szSearchPath))
        {
            /* An unexpected error happened */
            // ConErrFormatMessage(GetLastError());
            Success = FALSE;
            goto Quit;
        }
    }

    /* Perform the completion */
    Success = CompleteFilenameProc(Context, *RestartCompletion,
                                   str, charcount);

Quit:
    if (!Success)
        MessageBeep(-1);

    return Success;
}
