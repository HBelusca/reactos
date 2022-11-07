/*
 * PROJECT:     ReactOS Native NT Runtime Library
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Entry point for Native NT Programs
 * COPYRIGHT:   Copyright 2007 Alex Ionescu <alex@relsoft.net>
 *              Copyright 2022 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

/* INCLUDES ******************************************************************/

// #include <tchar.h>

/* PSDK/NDK Headers */
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winnt.h>
#define NTOS_MODE_USER
// #include <ndk/psfuncs.h>
#include <ndk/rtlfuncs.h>

#define NDEBUG
#include <debug.h>


#ifdef UNICODE

#define PTCHAR PWCHAR
#define PTSTR  PWSTR

#define __T(x) L##x

#define _tmain wmain
#define _tenviron _wenviron

#define _tcslen wcslen

#else // UNICODE

#define PTCHAR PCHAR
#define PTSTR  PSTR

#define __T(x) x

#define _tmain main
#ifdef _POSIX_
#define _tenviron environ
#else
#define _tenviron _environ
#endif

#define _tcslen strlen

#endif // UNICODE

#define _T(x) __T(x)
#define _TEXT(x) __T(x)

/* See ntbasedef.h / winnt.h */
typedef _NullNull_terminated_ CHAR *PZZSTR;
typedef _NullNull_terminated_ CONST CHAR *PCZZSTR;

typedef _NullNull_terminated_ WCHAR *PZZWSTR;
typedef _NullNull_terminated_ CONST WCHAR *PCZZWSTR;

#ifdef UNICODE
typedef PZZWSTR PZZTSTR;
typedef PCZZWSTR PCZZTSTR;
#else // UNICODE
typedef PZZSTR PZZTSTR;
typedef PCZZSTR PCZZTSTR;
#endif // UNICODE

INT
__cdecl
_tmain(
    _In_ INT argc,
    _In_ PTCHAR argv[],
    _In_ PTCHAR envp[],
    _In_opt_ ULONG DebugFlags);

/* For POSIX compliance */
PTCHAR* _tenviron = NULL;


/* FUNCTIONS *****************************************************************/

// NOTE: Up to Windows 2003, the whitespace check excluded everything
// (including control characters) up to the space character itself.
// On Vista+, the whitespace check only checks for those characters
// the is(w)space() CRT function would return TRUE.
#ifndef _istspace
#define _istspace(c) (c <= _T(' '))
#endif

static __inline
PCTCH
NtGetNextArgument(
    _Inout_ PCTCH* CmdLine,
    _Inout_ PULONG CmdLineLength)
{
    PCTCH Source = *CmdLine;
    PCTCH End = NULL;
    ULONG Length = *CmdLineLength;

    /* Skip the whitespace */
    while (Length && *Source && _istspace(*Source))
    {
        Length -= sizeof(TCHAR);
        ++Source;
    }

    /* Find the end of this token, until the next whitespace */
    if (*Source)
    {
        End = Source;
        while (Length && *End && !_istspace(*End))
        {
            Length -= sizeof(TCHAR);
            ++End;
        }
    }

    *CmdLine = End;
    *CmdLineLength = Length;
    return Source;
}

static __inline
PCTSTR
NtGetNextEnvironmentString(
    _In_ PCZZTSTR Environment)
{
    /* Go to the next variable */
    // while (*Environment++);
    Environment += _tcslen(Environment) + 1;
    return Environment; // (*Environment ? Environment : NULL);
}


#ifndef UNICODE
static
NTSTATUS
NtEnvironmentToUnicodeString(
    _Out_ PUNICODE_STRING usOut,
    _In_opt_ PCZZWSTR Environment,
    _In_opt_ SIZE_T EnvSize)
{
    UNREFERENCED_PARAMETER(EnvSize);

    if (Environment)
    {
        PCWCH CurrentChar = Environment;

        while (*CurrentChar)
        {
            // while (*CurrentChar++);
            CurrentChar += wcslen(CurrentChar) + 1;
        }
        /* Double NULL-termination at end */
        CurrentChar++;

        /* Verify we don't overflow the UNICODE_STRING capacity */
        if ((CurrentChar - Environment) * sizeof(WCHAR) > MAXUSHORT)
            return STATUS_BUFFER_OVERFLOW;

        usOut->Buffer = (PWCHAR)Environment;
        usOut->MaximumLength = usOut->Length =
            (USHORT)(CurrentChar - Environment) * sizeof(WCHAR);
    }
    else
    {
        RtlInitEmptyUnicodeString(usOut, NULL, 0);
    }

    return STATUS_SUCCESS;
}
#endif


VOID
NTAPI
#ifdef UNICODE
NtProcessStartupW
#else
NtProcessStartup
#endif
    (_In_ PPEB Peb)
{
    NTSTATUS Status;
    PRTL_USER_PROCESS_PARAMETERS ProcessParameters;
    PUNICODE_STRING CmdLineString;
#ifndef UNICODE
    ANSI_STRING AnsiCmdLine;
    ANSI_STRING AnsiEnvironment;
#endif
    PTCHAR NullPointer = NULL;
    INT argc = 0;
    INT envc = 0;
    PTCHAR* argv;
    PTCHAR* envp;
    PTCHAR* ArgumentList;
    PCTCH  Source;
    PTCHAR Destination;
    ULONG Length;

    DPRINT("%s(0x%p) called\n", __FUNCTION__, Peb);
    ASSERT(Peb);

    /* Normalize and get the Process Parameters */
    ProcessParameters = RtlNormalizeProcessParams(Peb->ProcessParameters);
    ASSERT(ProcessParameters); // FIXME: Otherwise directly jump to _main()?

    /* Use a null pointer as default */
    argv = &NullPointer;
    envp = &NullPointer;

    /* Get the pointer to the Command Line. If we
     * don't have one, use the Image Path instead. */
    CmdLineString = &ProcessParameters->CommandLine;
    if (!CmdLineString->Buffer || !CmdLineString->Length)
        CmdLineString = &ProcessParameters->ImagePathName;

    /* If we don't have anything still, bail out */
    if (!CmdLineString->Buffer || !CmdLineString->Length)
        goto Fail; // FIXME: Otherwise directly jump to _main()?

#ifndef UNICODE
    /* Convert it to an ANSI string */
    Status = RtlUnicodeStringToAnsiString(&AnsiCmdLine, CmdLineString, TRUE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ERR: no mem(guess)\n");
        goto Fail;
    }
    Source = AnsiCmdLine.Buffer;
    Length = AnsiCmdLine.Length;
#else
    Source = CmdLineString->Buffer;
    Length = CmdLineString->Length;
#endif
    ASSERT(Source);

    /* Count the number of arguments in it */
    while (Length && *Source)
    {
        NtGetNextArgument(&Source, &Length);
        if (Source && *Source)
            ++argc;
    }

    /* Now handle the environment */
    if (ProcessParameters->Environment)
    {
#ifndef UNICODE
        UNICODE_STRING UnicodeEnvironment;
        NtEnvironmentToUnicodeString(&UnicodeEnvironment,
                                     ProcessParameters->Environment,
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
                                     ProcessParameters->EnvironmentSize
#else
                                     0
#endif
                                    );

        /* Convert it to an ANSI string */
        Status = RtlUnicodeStringToAnsiString(&AnsiEnvironment, &UnicodeEnvironment, TRUE);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ERR: no mem(guess)\n");
            goto Fail;
        }
        Source = AnsiEnvironment.Buffer;
#else
        Source = ProcessParameters->Environment;
#endif // UNICODE
        ASSERT(Source);

        /* Count the number of environment strings in it */
        // NtStartupCountEnvironmentStrings(Source);
        while (*Source)
        {
            ++envc;
            Source = NtGetNextEnvironmentString(Source);
        }
    }

    /* Allocate memory for the full argument list. Each sub-list
     * (arguments and environment) are terminated with NULL pointers. */
    Length = (argc + 1 + envc + 1) * sizeof(PTSTR);
    ArgumentList = RtlAllocateHeap(RtlGetProcessHeap(), 0, Length);
    if (!ArgumentList)
    {
        DPRINT1("ERR: no mem!");
        Status = STATUS_NO_MEMORY;
        goto Fail;
    }

    /* Set the first pointer to NULL, and set the argument array to the buffer */
    *ArgumentList = NULL;
    argv = ArgumentList;

    /* Save parameters for parsing */
#ifndef UNICODE
    Source = AnsiCmdLine.Buffer;
    Length = AnsiCmdLine.Length;
#else
    Source = CmdLineString->Buffer;
    Length = CmdLineString->Length;
#endif
    ASSERT(Source);

    /* Ensure it's valid */
    if (Source)
    {
        /* Allocate a buffer for the sanitized version of the arguments */
        // FIXME: We could have obtained a more accurate length while counting the arguments!
        Destination = RtlAllocateHeap(RtlGetProcessHeap(), 0, Length + sizeof(TCHAR));
        if (!Destination)
        {
            DPRINT1("ERR: no mem!");
            Status = STATUS_NO_MEMORY;
            goto Fail;
        }

        /* Start parsing */
        while (*Source)
        {
            PCTCH Argument = NtGetNextArgument(&Source, &Length);
            size_t ArgLen;
            if (Source)
            {
                /* Save one token pointer */
                *ArgumentList++ = Destination;

                /* Copy token and NULL-terminate it */
                ArgLen = Source - Argument;
                memmove(Destination, Argument, ArgLen * sizeof(TCHAR));
                Destination += ArgLen;
                *Destination++ = _T('\0');
            }
        }
    }

    /* NULL-terminate the token pointer list */
    *ArgumentList++ = NULL;

    /* Now handle the environment, point the envp at our current list location */
    envp = ArgumentList;

    if (ProcessParameters->Environment)
    {
#ifndef UNICODE
        Source = AnsiEnvironment.Buffer;
#else
        Source = ProcessParameters->Environment;
#endif
        /* Save a pointer to each environment string */
        // NtRecordEnvironmentPointers(Source, ArgumentList);
        while (*Source)
        {
            *ArgumentList++ = (PTCHAR)Source;
            Source = NtGetNextEnvironmentString(Source);
        }

        /* NULL-terminate the list again */
        *ArgumentList++ = NULL;
    }

SkipInit:
    /* Breakpoint if we were requested to do so */
    if (ProcessParameters->DebugFlags)
        DbgBreakPoint();

    /* Call the Main Function */
    Status = (NTSTATUS)_tmain(argc, argv, envp, ProcessParameters->DebugFlags);

Fail:
    /* We're done here */
    NtTerminateProcess(NtCurrentProcess(), Status);
}

/* EOF */
