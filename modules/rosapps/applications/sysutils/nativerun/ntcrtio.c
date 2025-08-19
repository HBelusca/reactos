/*
 * PROJECT:     NativeRun Command
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NT Native Applications startup program.
 * COPYRIGHT:   Copyright 2025 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

/* INCLUDES ******************************************************************/

#include "ntcrtio.h" // <stdio.h>
#include <stdlib.h> // For _countof()

//_CRTIMP int __cdecl _vsnprintf(char *_Dest,size_t _Count,const char *_Format,va_list _Args);
//_CRTIMP int __cdecl _vsnwprintf(wchar_t *_Dest,size_t _Count,const wchar_t *_Format,va_list _Args);

/* PSDK/NDK Headers */
#define WIN32_NO_STATUS
// #include <windows.h>
#include <windef.h>
#include <winbase.h>

// #include <ntndk.h>
#define NTOS_MODE_USER
#include <ndk/exfuncs.h>
#include <ndk/rtlfuncs.h>
#include <ndk/umfuncs.h>

#include <ntstrsafe.h>


/* GLOBALS *******************************************************************/

extern PIMAGE_NT_HEADERS g_NtHeader;
extern PRTL_USER_PROCESS_PARAMETERS g_ProcessParameters;
extern BOOLEAN g_IsNative;


/* PRINTF-STYLE FUNCTIONS FOR BOTH NATIVE AND WIN32 SUPPORT ******************/

static HANDLE g_hKernel32 = NULL;    ///< Delay-loaded kernel32, only when in Win32 mode!
static BOOLEAN g_bK32Loaded = FALSE; ///< TRUE if we loaded kernel32; FALSE if we didn't.

BOOL
(WINAPI* pWriteFile)(
    _In_ HANDLE hFile,
    _In_ LPCVOID lpBuffer,
    _In_opt_ DWORD nNumberOfBytesToWrite,
    _Out_ LPDWORD lpNumberOfBytesWritten,
    _In_opt_ LPOVERLAPPED lpOverlapped) = NULL;


FILE _iob[_IOB_ENTRIES] = {NULL};
FILE* __cdecl __iob_func(void)
{
    return &_iob[0];
}

#define IO_STRING_MAX_LEN  512

#if 0
size_t fwrite(
    const void *buffer,
    size_t size,
    size_t count,
    FILE *stream
)
{
    int ret;
    ANSI_STRING StringA, StringADbg;
    UNICODE_STRING StringU;
    WCHAR UnicodeBuffer[IO_STRING_MAX_LEN];

    StringA.Buffer = (PCHAR)buffer;
    StringA.Length = StringA.MaximumLength = min(size * count, USHRT_MAX);

    RtlInitEmptyUnicodeString(&StringU, UnicodeBuffer, sizeof(UnicodeBuffer));
    (void)RtlAnsiStringToUnicodeString(&StringU, &StringA,
                                       (sizeof(UnicodeBuffer) < StringA.MaximumLength * sizeof(WCHAR)));

    /* Do the DbgPrint in 512 byte chunks.
     * Repeat until the string has been fully output */
    StringADbg = StringA;
    while (StringADbg.Length > 0)
    {
        /* Write a maximum of 510 bytes + NUL terminator */
        USHORT oldLen = StringADbg.Length;
        USHORT nRoundLen = min(StringADbg.Length, 510);
        StringADbg.Length = nRoundLen;
        (void)DbgPrint("%Z", &StringADbg);
        StringADbg.Length = oldLen - nRoundLen;
        StringADbg.MaximumLength -= nRoundLen;
        StringADbg.Buffer += (nRoundLen / sizeof(CHAR));
    }

    ret = EOF;
    if (g_IsNative)
    {
        /* We run as a Native application */
        NTSTATUS Status = NtDisplayString(&StringU);
        ret = (NT_SUCCESS(Status) ? StringU.Length / sizeof(WCHAR) : EOF);
    }
    else if (pWriteFile)
    {
        /* Use kernel32 WriteFile() so as to be able to either
         * output to a file, or to a Win32 console */
        ULONG dwBytesWritten;
        if (pWriteFile(**stream/*->handle*/, StringA.Buffer, StringA.Length, &dwBytesWritten, NULL))
            ret = (int)dwBytesWritten;
    }

    if (StringU.Buffer != UnicodeBuffer)
        RtlFreeUnicodeString(&StringU);

    return ret;
}
#endif

#if 0
int _write(
    int fd,
    const void *buffer,
    unsigned int count
);
#endif

int fputs(
    _In_z_ const char *str,
    FILE *stream
)
{
    int ret;
    ANSI_STRING StringA, StringADbg;
    UNICODE_STRING StringU;
    WCHAR UnicodeBuffer[IO_STRING_MAX_LEN];

    RtlInitAnsiString(&StringA, str);
    RtlInitEmptyUnicodeString(&StringU, UnicodeBuffer, sizeof(UnicodeBuffer));
    (void)RtlAnsiStringToUnicodeString(&StringU, &StringA,
                                       (sizeof(UnicodeBuffer) < StringA.MaximumLength * sizeof(WCHAR)));

    /* Do the DbgPrint in 512 byte chunks.
     * Repeat until the string has been fully output */
    StringADbg = StringA;
    while (StringADbg.Length > 0)
    {
        /* Write a maximum of 510 bytes + NUL terminator */
        USHORT oldLen = StringADbg.Length;
        USHORT nRoundLen = min(StringADbg.Length, 510);
        StringADbg.Length = nRoundLen;
        (void)DbgPrint("%Z", &StringADbg);
        StringADbg.Length = oldLen - nRoundLen;
        StringADbg.MaximumLength -= nRoundLen;
        StringADbg.Buffer += (nRoundLen / sizeof(CHAR));
    }

    ret = EOF;
    if (g_IsNative)
    {
        /* We run as a Native application */
        NTSTATUS Status = NtDisplayString(&StringU);
        ret = (NT_SUCCESS(Status) ? StringU.Length / sizeof(WCHAR) : EOF);
    }
    else if (pWriteFile)
    {
        /* Use kernel32 WriteFile() so as to be able to either
         * output to a file, or to a Win32 console */
        ULONG dwBytesWritten;
        if (pWriteFile(**stream/*->handle*/, StringA.Buffer, StringA.Length, &dwBytesWritten, NULL))
            ret = (int)dwBytesWritten;
    }

    if (StringU.Buffer != UnicodeBuffer)
        RtlFreeUnicodeString(&StringU);

    return ret;
}

int fputws(
    _In_z_ const wchar_t *str,
    FILE *stream
)
{
    int ret;
    UNICODE_STRING StringU, StringUDbg;
    ANSI_STRING StringA;
    CHAR AnsiBuffer[IO_STRING_MAX_LEN];

    RtlInitUnicodeString(&StringU, str);
    RtlInitEmptyAnsiString(&StringA, AnsiBuffer, sizeof(AnsiBuffer));
    (void)RtlUnicodeStringToAnsiString(&StringA, &StringU,
                                       (sizeof(AnsiBuffer) < StringU.MaximumLength / sizeof(WCHAR)));

    /* Do the DbgPrint in 512 byte chunks.
     * Repeat until the string has been fully output */
    StringUDbg = StringU;
    while (StringUDbg.MaximumLength > 0)
    {
        /* Write a maximum of 510 bytes + NUL terminator */
        USHORT oldLen = StringUDbg.Length;
        USHORT nRoundLen = min(StringUDbg.Length, 510);
        StringUDbg.Length = nRoundLen;
        (void)DbgPrint("%wZ", &StringUDbg);
        // (void)DbgPrint("%Z", &StringADbg);
        StringUDbg.Length = oldLen - nRoundLen;
        StringUDbg.MaximumLength -= nRoundLen;
        StringUDbg.Buffer += (nRoundLen / sizeof(WCHAR));
    }

    ret = EOF;
    if (g_IsNative)
    {
        /* We run as a Native application */
        NTSTATUS Status = NtDisplayString(&StringU);
        ret = (NT_SUCCESS(Status) ? StringU.Length / sizeof(WCHAR) : EOF);
    }
    else if (pWriteFile)
    {
        /* Use kernel32 WriteFile() so as to be able to either
         * output to a file, or to a Win32 console */
        ULONG dwBytesWritten;
        if (pWriteFile(**stream/*->handle*/, StringA.Buffer, StringA.Length, &dwBytesWritten, NULL))
            ret = (int)dwBytesWritten;
    }

    if (StringA.Buffer != AnsiBuffer)
        RtlFreeAnsiString(&StringA);

    return ret;
}

int puts(
    _In_z_ const char *str
)
{
    return (fputs(str, stdout) + fputs("\n", stdout));
}

int _putws(
    _In_z_ const wchar_t *str
)
{
    return (fputws(str, stdout) + fputws(L"\n", stdout));
}


int vfprintf(
    FILE *stream,
    _In_z_ _Printf_format_string_ const char *format,
    va_list argptr
)
{
    int len;
    CHAR bufSrc[IO_STRING_MAX_LEN];

#if 1
    (void)RtlStringCchVPrintfA(bufSrc, _countof(bufSrc), format, argptr);
#else
    /*
     * Re-use format as the pointer to end-of-string, so as
     * to compute the string length instead of calling wcslen().
     */
    // len = strlen(bufSrc);
    (void)RtlStringCchVPrintfExA(bufSrc, _countof(bufSrc), (PSTR*)&format, NULL, 0, format, argptr);
    len = format - bufSrc;
#endif
    // CON_STREAM_WRITE2(stream, bufSrc, len, len);
    len = fputs(bufSrc, stream);
#if 0
    /* Fixup returned length in case of errors */
    if (len < 0)
        len = 0;
#endif
    return len;
}

int vfwprintf(
    FILE *stream,
    _In_z_ _Printf_format_string_ const wchar_t *format,
    va_list argptr
)
{
    int len;
    WCHAR bufSrc[IO_STRING_MAX_LEN];

#if 1
    (void)RtlStringCchVPrintfW(bufSrc, _countof(bufSrc), format, argptr);
#else
    /*
     * Re-use format as the pointer to end-of-string, so as
     * to compute the string length instead of calling wcslen().
     */
    // len = wcslen(bufSrc);
    (void)RtlStringCchVPrintfExW(bufSrc, _countof(bufSrc), (PWSTR*)&format, NULL, 0, format, argptr);
    len = format - bufSrc;
#endif
    // CON_STREAM_WRITE2(stream, bufSrc, len, len);
    len = fputws(bufSrc, stream);
#if 0
    /* Fixup returned length in case of errors */
    if (len < 0)
        len = 0;
#endif
    return len;
}

int __cdecl fprintf(
    FILE *stream,
    _In_z_ _Printf_format_string_ const char *format,
    ...
)
{
    int len;
    va_list args;

    va_start(args, format);
    len = vfprintf(stream, format, args);
    va_end(args);

    return len;
}

int __cdecl fwprintf(
    FILE *stream,
    _In_z_ _Printf_format_string_ const wchar_t *format,
    ...
)
{
    int len;
    va_list args;

    va_start(args, format);
    len = vfwprintf(stream, format, args);
    va_end(args);

    return len;
}

int vprintf(
    _In_z_ _Printf_format_string_ const char *format,
    va_list argptr
)
{
    return vfprintf(stdout, format, argptr);
}

int vwprintf(
    _In_z_ _Printf_format_string_ const wchar_t *format,
    va_list argptr
)
{
    return vfwprintf(stdout, format, argptr);
}

int __cdecl printf(
    _In_z_ _Printf_format_string_ const char *format,
    ...
)
{
    int len;
    va_list args;

    va_start(args, format);
    len = vprintf(format, args);
    va_end(args);

    return len;
}

int __cdecl wprintf(
    _In_z_ _Printf_format_string_ const wchar_t *format,
    ...
)
{
    int len;
    va_list args;

    va_start(args, format);
    len = vwprintf(format, args);
    va_end(args);

    return len;
}


NTSTATUS
InitNtCrtIO(VOID)
{
    NTSTATUS Status = STATUS_SUCCESS;

    /* Initialize the standard IO handles */
    _iob[STDIN_FILENO]  = (FILE)&g_ProcessParameters->StandardInput;
    _iob[STDOUT_FILENO] = (FILE)&g_ProcessParameters->StandardOutput;
    _iob[STDERR_FILENO] = (FILE)&g_ProcessParameters->StandardError;

    /* If we are running as a Win32 image, load some functions from kernel32 */
    if (g_NtHeader->OptionalHeader.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI ||
        g_NtHeader->OptionalHeader.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_CUI)
    {
        PVOID ProcAddress;
        ANSI_STRING ProcName;
        // UNICODE_STRING K32DllName = RTL_CONSTANT_STRING(L"kernel32.dll");
        UNICODE_STRING K32DllPath;
        WCHAR Buffer[MAX_PATH*2];

        /* Build a full path to kernel32.dll.
         * Use the SystemRoot from the shared user data string. */
        RtlInitEmptyUnicodeString(&K32DllPath, Buffer, sizeof(Buffer));
        if (!NT_SUCCESS(Status = RtlAppendUnicodeToString(&K32DllPath, SharedUserData->NtSystemRoot)) ||
            !NT_SUCCESS(Status = RtlAppendUnicodeToString(&K32DllPath, L"\\system32\\kernel32.dll")))
        {
            return Status;
        }

        /* Retrieve or load kernel32; use the default DLL path we were started with */
        Status = LdrGetDllHandle(NULL, NULL, &K32DllPath, (PVOID*)&g_hKernel32);
        if (!NT_SUCCESS(Status))
        {
            Status = LdrLoadDll(NULL, NULL, &K32DllPath, (PVOID*)&g_hKernel32);
            if (!NT_SUCCESS(Status))
            {
                DbgPrint("Unable to load %wZ (Status: 0x%08lx)\n", &K32DllPath, Status);
                FiniNtCrtIO();
                return Status;
            }
            g_bK32Loaded = TRUE;
        }

        RtlInitAnsiString(&ProcName, "WriteFile");
        Status = LdrGetProcedureAddress(g_hKernel32,
                                        &ProcName,
                                        0, // Not using ordinal.
                                        (PVOID*)&ProcAddress);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint("Unable to find kernel32!WriteFile (Status: 0x%08lx)\n", Status);
            FiniNtCrtIO();
            return Status;
        }
        pWriteFile = ProcAddress;
    }

    return Status;
}

VOID
FiniNtCrtIO(VOID)
{
    if (g_bK32Loaded)
        (void)LdrUnloadDll((PVOID)g_hKernel32);
    pWriteFile = NULL;
    g_hKernel32 = NULL;
    g_bK32Loaded = FALSE;
}
