
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <windows.h>

// #define INT_TYPES
#define ALT_NAMES
#include "../parser.h"


struct _PARSE_STRING_CTX
{
    IN OUT PCWSTR Str; // Iterator
    OUT SIZE_T Length;
    OUT PCWSTR TokBuf;
};

static PCWSTR
TokStr(
    IN OUT PCWSTR* Str,
    OUT PSIZE_T Length)
{
    PCWSTR Src, Start;

    Src = (Str ? *Str : NULL);

    if (!Str || !Src || !*Src)
    {
        *Length = 0;
        if (Str) *Str = NULL;
        return NULL;
    }

    /* Strip leading whitespace */
    while (iswspace(*Src))
        ++Src;
    if (!*Src)
    {
        *Length = 0;
        *Str = NULL;
        return NULL;
    }

    /* Token start now */
    Start = Src;

    /* Get next separator */
    Src = wcschr(Src, L' ');
    if (!Src)
    {
        Src = Start + wcslen(Start);
    }

    *Length = (Src - Start);
    *Str = Src;
    return Start;
}

PCWSTR
ParseTokenStr(
    IN OUT struct _PARSE_STRING_CTX* Context)
{
    PCWSTR tok;
    PWSTR ptr;

    tok = TokStr(&Context->Str, &Context->Length);
    if (!tok)
        goto Failure;
    ptr = realloc((void*)Context->TokBuf, (Context->Length + 1) * sizeof(WCHAR));
    if (!ptr)
        goto Failure;

    wcsncpy(ptr, tok, Context->Length);
    ptr[Context->Length] = 0;
    Context->TokBuf = ptr;
    return ptr;

Failure:
    if (Context->TokBuf)
        free((void*)Context->TokBuf);
    Context->TokBuf = NULL;
    Context->Length = 0;
    return NULL;
}



/** Support for the Parser API **/
static VOID
__cdecl
PrintParserError(PARSER_ERROR Error, ...)
{
    /* WARNING: Please keep this lookup table in sync with the resources! */
    static PCWSTR ErrorIDs[] =
    {
        L"Success\n",                                    /* Success */
        L"Invalid syntax\n",                             /* InvalidSyntax */
        L"Invalid option '%s'\n",                        /* InvalidOption */
        L"Value required for option '%s'\n",             /* ValueRequired */
        L"Value for option '%s' is empty\n",             /* ValueIsEmpty */
        L"Invalid value '%s' for option '%s'\n",         /* InvalidValue */
        L"Value for option '%s' is not allowed\n",       /* ValueNotAllowed */
        L"Too many same option '%s', max number %d\n",   /* TooManySameOption */
        L"Mandatory option '%s' absent\n",               /* MandatoryOptionAbsent */
    };

    va_list args;

    if (Error < ARRAYSIZE(ErrorIDs))
    {
        va_start(args, Error);
        vfwprintf(stderr, ErrorIDs[Error], args);
        va_end(args);

        if (Error != Success)
            fprintf(stderr, "Usage: None\n");
    }
    else
    {
        fprintf(stderr, "PARSER: Unknown error %d\n", Error);
    }
}


// /t error /d "toto" /id 1000 /f
int wmain(int argc, WCHAR* argv[])
{
    BOOL bSuccess;
    LPCWSTR lpCmdLine;

    /* Default option values */
    BOOL  bDisplayHelp  = FALSE;
    PWSTR szSystem      = NULL;
    PWSTR szPassword    = NULL;
    PWSTR szEventType   = NULL;
    PWSTR szDescription = NULL;
    ULONG ulEventType   = EVENTLOG_INFORMATION_TYPE;
    ULONG ulEventCategory   = 0;
    ULONG ulEventIdentifier = 0;
    ULONG ulFlag = 0;

    OPTION Options[] =
    {
        /* Help */
        NEW_OPT(L"?|h", TYPE_None, // ~= TYPE_Bool,
                OPTION_EXCLUSIVE,
                1,
                sizeof(bDisplayHelp), &bDisplayHelp),

        /* System */
        NEW_OPT(L"S", TYPE_Str,
                OPTION_NOT_EMPTY | OPTION_TRIM_SPACE,
                1,
                sizeof(szSystem), &szSystem),

        /* Password */
        NEW_OPT(L"P", TYPE_Str,
                0,
                1,
                sizeof(szPassword), &szPassword),

        /* Event type */
        NEW_OPT_EX(L"T", TYPE_Str,
                   OPTION_MANDATORY | OPTION_NOT_EMPTY | OPTION_TRIM_SPACE | OPTION_ALLOWED_LIST,
                   L"SUCCESS|ERROR|WARNING|INFORMATION",
                   1,
                   sizeof(szEventType), &szEventType),

        /* Event description */
        NEW_OPT(L"D", TYPE_Str,
                OPTION_MANDATORY,
                1,
                sizeof(szDescription), &szDescription),

        /* Event category (ReactOS additional option) */
        NEW_OPT(L"C", TYPE_ULong,
                0,
                1,
                sizeof(ulEventCategory), &ulEventCategory),

        /* Event ID */
        NEW_OPT(L"ID", TYPE_ULong,
                OPTION_MANDATORY,
                1,
                sizeof(ulEventIdentifier), &ulEventIdentifier),

        /* Some flag */
        NEW_OPT_EX(L"F", TYPE_Flag,
                   0,
                   123,
                   1,
                   sizeof(ulFlag), &ulFlag),
    };

    /* Parse command line for options */
    lpCmdLine = GetCommandLineW();
    wprintf(L"Original command-line:\n    '%s'\n", lpCmdLine);

    printf("Calling DoParseArgv()\n");
    bSuccess = DoParseArgv(argc, argv, Options, ARRAYSIZE(Options), PrintParserError);
    if (!bSuccess)
        printf("DoParseArgv() failed\n");

    if (bDisplayHelp)
        printf("Help(1)!\n");

    bDisplayHelp = FALSE;
    szSystem = NULL;
    szPassword = NULL;
    szEventType = NULL;
    szDescription = NULL;
    ulEventType = EVENTLOG_INFORMATION_TYPE;
    ulEventCategory = 0;
    ulEventIdentifier = 0;
    ulFlag = 0;
    for (size_t i = 0; i < ARRAYSIZE(Options); ++i)
    {
        Options[i].Instances = 0;
    }

    printf("Calling DoParseWorker()\n");
    {
    struct _PARSE_STRING_CTX Context = {lpCmdLine, 0, NULL};
    /* Zap the first argument (program name) */
    ParseTokenStr(&Context);
    /* Do the parsing */
    bSuccess = DoParseWorker(&Context, ParseTokenStr,
                             Options, ARRAYSIZE(Options), PrintParserError);
    if (Context.TokBuf)
        free((void*)Context.TokBuf);
    }
    if (!bSuccess)
        printf("DoParseWorker() failed\n");

    if (bDisplayHelp)
        printf("Help(2)!\n");

    return 0;
}
