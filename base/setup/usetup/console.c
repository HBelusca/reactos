/*
 *  ReactOS kernel
 *  Copyright (C) 2002 ReactOS Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS text-mode setup
 * FILE:            base/setup/usetup/console.c
 * PURPOSE:         Console support functions
 * PROGRAMMER:
 */

/* INCLUDES ******************************************************************/

#include <usetup.h>
/* Blue Driver Header */
#include <blue/ntddblue.h>
#include <../../ntoskrnl/include/internal/hdl.h>
#include <ntddhdls.h>
#include "keytrans.h"

#define NDEBUG
#include <debug.h>

/* DATA **********************************************************************/

static HANDLE InputHandle    = NULL;
static HANDLE OutputHandle   = NULL; // Handle to the Video device
static HANDLE HdlsTermHandle = NULL; // Handle to the Headless Terminal device

/* Cached screen buffer for headless terminal */
#define HEADLESS_SCREEN_WIDTH   80
#define HEADLESS_SCREEN_HEIGHT  25
static PCHAR_INFO ScreenBuffer = NULL;
static COORD ScreenBufferSize  = {0};

static BOOLEAN InputQueueEmpty;
static BOOLEAN WaitForInput;
static KEYBOARD_INPUT_DATA InputDataQueue; // Only one element!
static IO_STATUS_BLOCK InputIosb;
static UINT LastLoadedCodepage = 0;


#define FOREGROUND_BLACK    0
#define BACKGROUND_BLACK    0

/*
 * Headless terminal text colors -- Taken from ntoskrnl/inbv/inbv.c
 */

// Conversion table CGA to ANSI color index
const UCHAR CGA_TO_ANSI_COLOR_TABLE[16] =
{
    0,  // Black
    4,  // Blue
    2,  // Green
    6,  // Cyan
    1,  // Red
    5,  // Magenta
    3,  // Brown/Yellow
    7,  // Grey/White

    60, // Bright Black
    64, // Bright Blue
    62, // Bright Green
    66, // Bright Cyan
    61, // Bright Red
    65, // Bright Magenta
    63, // Bright Yellow
    67  // Bright Grey (White)
};

#define CGA_TO_ANSI_COLOR(CgaColor) \
    CGA_TO_ANSI_COLOR_TABLE[CgaColor & 0x0F]


/* FUNCTIONS *****************************************************************/

typedef struct _CONSOLE_CABINET_CONTEXT
{
    CABINET_CONTEXT CabinetContext;
    PVOID Data;
    ULONG Size;
} CONSOLE_CABINET_CONTEXT, *PCONSOLE_CABINET_CONTEXT;

static PVOID
ConsoleCreateFileHandler(
    IN PCABINET_CONTEXT CabinetContext,
    IN ULONG FileSize)
{
    PCONSOLE_CABINET_CONTEXT ConsoleCabinetContext;

    ConsoleCabinetContext = (PCONSOLE_CABINET_CONTEXT)CabinetContext;
    ConsoleCabinetContext->Data = RtlAllocateHeap(ProcessHeap, 0, FileSize);
    if (!ConsoleCabinetContext->Data)
    {
        DPRINT("Failed to allocate %d bytes\n", FileSize);
        return NULL;
    }
    ConsoleCabinetContext->Size = FileSize;
    return ConsoleCabinetContext->Data;
}


/******************************************************************************\
|** Driver management                                                        **|
\**/
/* Code taken and adapted from base/system/services/driver.c */
static NTSTATUS
ScmLoadUnloadDriver(
    IN PCWSTR ServiceName,
    IN BOOLEAN Loading) // TRUE: Loading; FALSE: Unloading.
{
#define REG_SERVICES_PATH   L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\"

    NTSTATUS Status;
    BOOLEAN WasPrivilegeEnabled = FALSE;
    UNICODE_STRING DriverPath;

    /* Build the driver path */
    DriverPath.MaximumLength = (USHORT)(sizeof(REG_SERVICES_PATH) + (wcslen(ServiceName) + 1) * sizeof(WCHAR));
    DriverPath.Buffer = RtlAllocateHeap(ProcessHeap,
                                        HEAP_ZERO_MEMORY,
                                        DriverPath.MaximumLength);
    if (DriverPath.Buffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlStringCbCopyW(DriverPath.Buffer, DriverPath.MaximumLength, REG_SERVICES_PATH);
    RtlStringCbCatW(DriverPath.Buffer, DriverPath.MaximumLength, ServiceName);

    DriverPath.Length = wcslen(DriverPath.Buffer) * sizeof(WCHAR);

    /* Acquire driver-(un)loading privilege */
    Status = RtlAdjustPrivilege(SE_LOAD_DRIVER_PRIVILEGE,
                                TRUE,
                                FALSE,
                                &WasPrivilegeEnabled);
    if (!NT_SUCCESS(Status))
    {
        /* We encountered a failure, exit properly */
        DPRINT1("Cannot acquire driver-(un)loading privilege, Status 0x%08lx\n", Status);
        goto Quit;
    }

    if (Loading)
    {
        Status = NtLoadDriver(&DriverPath);
        /* If we failed because the driver was already loaded, claim success */
        if (Status == STATUS_IMAGE_ALREADY_LOADED)
            Status = STATUS_SUCCESS;
    }
    else
    {
        Status = NtUnloadDriver(&DriverPath);
    }

    /* Release driver-(un)loading privilege */
    RtlAdjustPrivilege(SE_LOAD_DRIVER_PRIVILEGE,
                       WasPrivilegeEnabled,
                       FALSE,
                       &WasPrivilegeEnabled);

Quit:
    RtlFreeHeap(ProcessHeap, 0, DriverPath.Buffer);
    return Status;

#undef REG_SERVICES_PATH
}
/**\
\******************************************************************************/


/* HEADLESS STUFF ************************************************************/

static ULONG
ReadBytesAsync(
    IN HANDLE hInput,
    OUT LPVOID pBuffer,
    IN ULONG  nNumberOfBytesToRead,
    OUT PULONG lpNumberOfBytesRead OPTIONAL /*,
    IN ULONG dwTimeout OPTIONAL, // In milliseconds
    IN OUT LPOVERLAPPED lpOverlapped OPTIONAL */)
{
    NTSTATUS Status;
    LARGE_INTEGER Offset;
    IO_STATUS_BLOCK IoStatusBlock;

    ULONG dwTotalRead = 0;

    if (lpNumberOfBytesRead)
        *lpNumberOfBytesRead = dwTotalRead;

#if 0
    /* Read the data and write it into the buffer */
    if (!ReadFile(hInput, pBuffer, nNumberOfBytesToRead,
                  /*lpNumberOfBytesRead*/ &dwTotalRead, lpOverlapped))
    {
        ULONG dwLastError = GetLastError();
        if (dwLastError != ERROR_IO_PENDING)
            return dwLastError;

        if (lpOverlapped->hEvent)
        {
            ULONG dwWaitState;

            dwWaitState = WaitForSingleObject(lpOverlapped->hEvent, dwTimeout);
            if (dwWaitState == WAIT_TIMEOUT)
            {
                /*
                 * Properly cancel the I/O operation and wait for the operation
                 * to finish, otherwise the overlapped structure may become
                 * out-of-order while I/O operations are being completed...
                 * See https://blogs.msdn.microsoft.com/oldnewthing/20110202-00/?p=11613
                 * for more details.
                 * NOTE: CancelIoEx does not exist on Windows <= 2003.
                 */
                CancelIo(hInput);
                // CancelIoEx(hInput, &lpOverlapped);
                GetOverlappedResult(hInput, lpOverlapped, &dwTotalRead, TRUE);
                // WaitForSingleObject(lpOverlapped->hEvent, INFINITE);
                return dwWaitState;    // A timeout occurred
            }
            if (dwWaitState != WAIT_OBJECT_0)
                return GetLastError(); // An unknown error happened
        }

        if (!GetOverlappedResult(hInput, lpOverlapped, &dwTotalRead, !lpOverlapped->hEvent))
            return GetLastError();
    }
#else
    Offset.QuadPart = 0;
    Status = NtReadFile(hInput,
                        NULL,
                        NULL,
                        NULL,
                        &IoStatusBlock,
                        pBuffer,
                        nNumberOfBytesToRead,
                        &Offset,
                        NULL);
    if (!NT_SUCCESS(Status))
        return (ULONG)Status;

    dwTotalRead = IoStatusBlock.Information;
#endif

    if (lpNumberOfBytesRead)
        *lpNumberOfBytesRead = dwTotalRead;

    return ERROR_SUCCESS;
}

#define __T(x)  L ## x
#define _T(x)   __T(x)
#define PTCHAR  PWCHAR

/* Does an active UTF-8/UTF-16 conversion */
static ULONG
ReadTTYChar(
    IN HANDLE hInput,
    OUT PTCHAR pChar /*,
    IN ULONG dwTimeout OPTIONAL, // In milliseconds
    IN OUT LPOVERLAPPED lpOverlapped OPTIONAL */)
{
    ULONG dwLastError;
    ULONG dwRead;
    ULONG dwTotalRead;
    CHAR Buffer[6]; // Real maximum number of bytes for a UTF-8 encoded character

    RtlZeroMemory(Buffer, sizeof(Buffer));
    dwTotalRead = 0;

    /* Read the leading byte */
    dwLastError = ReadBytesAsync(hInput, Buffer, 1, &dwRead /*, dwTimeout, lpOverlapped*/);
    if (dwLastError != ERROR_SUCCESS)
        return dwLastError;
    ++dwTotalRead;

    /* Is it an escape sequence? */
    if (Buffer[0] == '\x1B')
    {
        /* Yes it is, let the caller interpret it instead */
        *pChar = _T('\x1B');
        return ERROR_SUCCESS;
    }

#if 0 /* Extensions to the UTF-8 encoding */
    if ((Buffer[0] & 0xFE) == 0xFC) /* Check for 1111110x: 1+5-byte encoded character */
    {
        dwLastError = ReadBytesAsync(hInput, &Buffer[1], 5, &dwRead /*, dwTimeout, lpOverlapped*/);
        if (dwLastError != ERROR_SUCCESS)
            return dwLastError;
        dwTotalRead += 5;

        /* The other bytes should all start with 10xxxxxx */
        if ((Buffer[1] & 0xC0) != 0x80 ||
            (Buffer[2] & 0xC0) != 0x80 ||
            (Buffer[3] & 0xC0) != 0x80 ||
            (Buffer[4] & 0xC0) != 0x80 ||
            (Buffer[5] & 0xC0) != 0x80)
        {
            return ERROR_INVALID_DATA;
        }
    }
    else
    if ((Buffer[0] & 0xFC) == 0xF8) /* Check for 111110xx: 1+4-byte encoded character */
    {
        dwLastError = ReadBytesAsync(hInput, &Buffer[1], 4, &dwRead /*, dwTimeout, lpOverlapped*/);
        if (dwLastError != ERROR_SUCCESS)
            return dwLastError;
        dwTotalRead += 4;

        /* The other bytes should all start with 10xxxxxx */
        if ((Buffer[1] & 0xC0) != 0x80 ||
            (Buffer[2] & 0xC0) != 0x80 ||
            (Buffer[3] & 0xC0) != 0x80 ||
            (Buffer[4] & 0xC0) != 0x80)
        {
            return ERROR_INVALID_DATA;
        }
    }
    else
#endif
    if ((Buffer[0] & 0xF8) == 0xF0) /* Check for 11110xxx: 1+3-byte encoded character */
    {
        dwLastError = ReadBytesAsync(hInput, &Buffer[1], 3, &dwRead /*, dwTimeout, lpOverlapped*/);
        if (dwLastError != ERROR_SUCCESS)
            return dwLastError;
        dwTotalRead += 3;

        /* The other bytes should all start with 10xxxxxx */
        if ((Buffer[1] & 0xC0) != 0x80 ||
            (Buffer[2] & 0xC0) != 0x80 ||
            (Buffer[3] & 0xC0) != 0x80)
        {
            return ERROR_INVALID_DATA;
        }
    }
    else
    if ((Buffer[0] & 0xF0) == 0xE0) /* Check for 1110xxxx: 1+2-byte encoded character */
    {
        dwLastError = ReadBytesAsync(hInput, &Buffer[1], 2, &dwRead /*, dwTimeout, lpOverlapped*/);
        if (dwLastError != ERROR_SUCCESS)
            return dwLastError;
        dwTotalRead += 2;

        /* The other bytes should all start with 10xxxxxx */
        if ((Buffer[1] & 0xC0) != 0x80 ||
            (Buffer[2] & 0xC0) != 0x80)
        {
            return ERROR_INVALID_DATA;
        }
    }
    else
    if ((Buffer[0] & 0xE0) == 0xC0) /* Check for 110xxxxx: 1+1-byte encoded character */
    {
        dwLastError = ReadBytesAsync(hInput, &Buffer[1], 1, &dwRead /*, dwTimeout, lpOverlapped*/);
        if (dwLastError != ERROR_SUCCESS)
            return dwLastError;
        ++dwTotalRead;

        /* The other bytes should all start with 10xxxxxx */
        if ((Buffer[1] & 0xC0) != 0x80)
            return ERROR_INVALID_DATA;
    }
    /* else, this is a 1-byte character */

// #ifdef _UNICODE
    // /* Convert to UTF-16 */
    // return (MultiByteToWideChar(CP_UTF8, 0, Buffer, dwTotalRead, pChar, 1) == 1
                // ? ERROR_SUCCESS : ERROR_INVALID_DATA);
// #else
    // #error Not implemented yet!
// #endif
    *pChar = *Buffer;
    return ERROR_SUCCESS;
}

static ULONG
ReadTTYEscapes(
    IN HANDLE hInput,
    OUT PCHAR pEscapeType,
    OUT PCHAR pFunctionChar,
    OUT PSTR pszParams OPTIONAL,
    IN ULONG dwParamsLength,
    OUT PSTR pszInterm OPTIONAL,
    IN ULONG dwIntermLength /*,
    IN ULONG dwTimeout OPTIONAL, // In milliseconds
    IN OUT LPOVERLAPPED lpOverlapped OPTIONAL */)
{
    ULONG dwLastError;
    ULONG dwRead, dwLength;
    PCHAR p;
    CHAR bChar;

    *pEscapeType = 0;
    *pFunctionChar = 0;
    if (pszParams && dwParamsLength > 0)
        *pszParams = 0;
    if (pszInterm && dwIntermLength > 0)
        *pszInterm = 0;

    /*
     * Possibly an escape character, check the second character.
     * Note that we only try to interpret CSI sequences.
     */
    dwLastError = ReadBytesAsync(hInput, &bChar, 1, NULL /*, dwTimeout, lpOverlapped*/);
    if (dwLastError != ERROR_SUCCESS)
        return dwLastError;

    if (bChar == 'O')
    {
        /* Single Shift Select of G3 Character Set (SS3) */
        dwLastError = ReadBytesAsync(hInput, &bChar, 1, NULL /*, dwTimeout, lpOverlapped*/);
        if (dwLastError != ERROR_SUCCESS)
            return dwLastError;

        *pEscapeType = 'O';
        *pFunctionChar = bChar;
        return ERROR_SUCCESS;
    }
    else
    if (bChar == '[')
    {
        /* Control Sequence Introducer (CSI) */

        /* Read any number of parameters */
        dwLength = dwParamsLength;
        p = pszParams;
        dwRead = 0;

        while (dwRead < dwLength - 1)
        {
            dwLastError = ReadBytesAsync(hInput, &bChar, 1, NULL /*, dwTimeout, lpOverlapped*/);
            if (dwLastError != ERROR_SUCCESS)
                return dwLastError; // ERROR_INVALID_DATA;

            /* Is it a paramater? */
            if (0x30 <= bChar && bChar <= 0x3F)
            {
                ++dwRead;
                if (pszParams && dwParamsLength > 0)
                    *p++ = bChar;
            }
            else
            {
                if (pszParams && dwParamsLength > 0)
                    *p = 0;
                break;
            }
        }

        /* Read any number of intermediate bytes */
        dwLength = dwIntermLength;
        p = pszInterm;
        dwRead = 0;

        do
        {
            /* Is it an intermediate byte? */
            if (0x20 <= bChar && bChar <= 0x2F)
            {
                ++dwRead;
                if (pszInterm && dwIntermLength > 0)
                    *p++ = bChar;
            }
            else
            {
                if (pszInterm && dwIntermLength > 0)
                    *p = 0;
                break;
            }

            dwLastError = ReadBytesAsync(hInput, &bChar, 1, NULL /*, dwTimeout, lpOverlapped*/);
            if (dwLastError != ERROR_SUCCESS)
                return dwLastError; // ERROR_INVALID_DATA;
        } while (dwLastError == ERROR_SUCCESS);

        /* Check the terminating byte */
        if (0x40 <= bChar && bChar <= 0x7E)
        {
            *pEscapeType = '[';
            *pFunctionChar = bChar;
            return ERROR_SUCCESS;
        }
        else
        {
            /* Malformed CSI escape sequence, ignore it */
            *pEscapeType = 0;
            *pFunctionChar = 0;
            if (pszParams && dwParamsLength > 0)
                *pszParams = 0;
            if (pszInterm && dwIntermLength > 0)
                *pszInterm = 0;
            return ERROR_INVALID_DATA;
        }
    }
    else
    {
        /* Unsupported escape sequence */
        return ERROR_INVALID_DATA;
    }
}

static ULONG
HdlsTermGetKeyTimeout(
    IN HANDLE hInput,
    IN OUT PKEY_EVENT_RECORD KeyEvent /*,
    IN ULONG dwTimeout */) // In milliseconds
{
    ULONG dwLastError;
    // OVERLAPPED ovl;
    WCHAR wChar;
    WORD  VkKey; // MAKEWORD(low = vkey_code, high = shift_state);
    KEY_EVENT_RECORD KeyEvt;

    // RtlZeroMemory(&ovl, sizeof(ovl));
    // ovl.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    dwLastError = ReadTTYChar(hInput, &wChar /*, dwTimeout, &ovl */);
    if (dwLastError != ERROR_SUCCESS)
    {
        /* We failed, bail out */
        // CloseHandle(ovl.hEvent);
        return dwLastError;
    }

    if (wChar != _T('\x1B'))
    {
        /* Get the key code (+ shift state) corresponding to the character */
        if (wChar == _T('\0') || wChar >= 0x20 || wChar == _T('\t') /** HACK **/ ||
            wChar == _T('\n') || wChar == _T('\r'))
        {
// #ifdef _UNICODE
            // VkKey = VkKeyScanW(wChar);
// #else
            // VkKey = VkKeyScanA(wChar);
// #endif
            if (wChar <= 0x7F)
            {
                VkKey = wChar;
            }
            else
            // if (VkKey == 0xFFFF)
            {
                DPRINT("FIXME: TODO: VkKeyScanW failed - Should simulate the key!\n");
                /*
                 * We don't really need the scan/key code because we actually only
                 * use the UnicodeChar for output purposes. It may pose few problems
                 * later on but it's not of big importance. One trick would be to
                 * convert the character to OEM / multibyte and use MapVirtualKey
                 * on each byte (simulating an Alt-0xxx OEM keyboard press).
                 */
            }
        }
        else
        {
            wChar += 0x40;
            VkKey = wChar;
            VkKey |= 0x0200; // KBDMULTIVK; // KBDCTRL;
        }

// #ifdef _UNICODE
        // KeyEvt.uChar.UnicodeChar = wChar;
// #else
        KeyEvt.uChar.AsciiChar = wChar;
// #endif
    }
    else // if (wChar == _T('\x1B'))
    {
        /* We deal with an escape sequence */

        CHAR EscapeType, FunctionChar;
        CHAR szParams[128];
        CHAR szInterm[128];

        /*
         * Try to interpret the escape sequence, and check its type.
         * Note that we only try to interpret a subset of CSI and SS3 sequences.
         */
        dwLastError = ReadTTYEscapes(hInput, &EscapeType, &FunctionChar,
                                     szParams, sizeof(szParams),
                                     szInterm, sizeof(szInterm) /*,
                                     dwTimeout, &ovl */);
        if (dwLastError != ERROR_SUCCESS)
        {
            /* We failed, bail out */
            // CloseHandle(ovl.hEvent);
            return dwLastError;
        }

        VkKey = 0;

        if (EscapeType == 'O')
        {
            /* Single Shift Select of G3 Character Set (SS3) */

            switch (FunctionChar)
            {
                case 'A': // Cursor up
                    VkKey = VK_UP;
                    break;

                case 'B': // Cursor down
                    VkKey = VK_DOWN;
                    break;

                case 'C': // Cursor right
                    VkKey = VK_RIGHT;
                    break;

                case 'D': // Cursor left
                    VkKey = VK_LEFT;
                    break;

                case 'F': // End
                    VkKey = VK_END;
                    break;

                case 'H': // Home
                    VkKey = VK_HOME;
                    break;

                case 'P': // F1
                    VkKey = VK_F1;
                    break;

                case 'Q': // F2
                    VkKey = VK_F2;
                    break;

                case 'R': // F3
                    VkKey = VK_F3;
                    break;

                case 'S': // F4
                    VkKey = VK_F4;
                    break;

                default: // Unknown
                    // CloseHandle(ovl.hEvent);
                    return ERROR_INVALID_DATA;
            }
        }
        else
        if (EscapeType == '[')
        {
            /* Control Sequence Introducer (CSI) */

            switch (FunctionChar)
            {
                case 'A': // Cursor up
                    VkKey = VK_UP;
                    break;

                case 'B': // Cursor down
                    VkKey = VK_DOWN;
                    break;

                case 'C': // Cursor right
                    VkKey = VK_RIGHT;
                    break;

                case 'D': // Cursor left
                    VkKey = VK_LEFT;
                    break;

                case '~': // Some Navigation or Function key
                {
                    UINT uFnKey = atoi(szParams);

                    switch (uFnKey)
                    {
                        case 1: // Home
                            VkKey = VK_HOME;
                            break;

                        case 2: // Insert
                            VkKey = VK_INSERT;
                            break;

                        case 3: // Delete
                            VkKey = VK_DELETE;
                            break;

                        case 4: // End
                            VkKey = VK_END;
                            break;

                        case 5: // Page UP
                            VkKey = VK_PRIOR;
                            break;

                        case 6: // Page DOWN
                            VkKey = VK_NEXT;
                            break;

                        default:
                        {
                            if (uFnKey < 11)
                                return ERROR_INVALID_DATA;

                            uFnKey -= 11;
                            if (uFnKey >= 6)
                                uFnKey--;
                            if (uFnKey >= 10)
                                uFnKey--;

                            VkKey = VK_F1 + uFnKey;
                        }
                    }

                    break;
                }
            }
        }
        else
        {
            /* Unsupported escape sequence */
            // CloseHandle(ovl.hEvent);
            return ERROR_INVALID_DATA;
        }

        KeyEvt.uChar.UnicodeChar = 0;
    }

    // CloseHandle(ovl.hEvent);

    KeyEvt.bKeyDown = TRUE;
    KeyEvt.wRepeatCount = 1;
    KeyEvt.wVirtualKeyCode = LOBYTE(VkKey);
    KeyEvt.wVirtualScanCode = 0; // MapVirtualKeyW(LOBYTE(VkKey), MAPVK_VK_TO_VSC);
    KeyEvt.dwControlKeyState = 0;
    if (HIBYTE(VkKey) & 1) // KBDSHIFT
        KeyEvt.dwControlKeyState |= SHIFT_PRESSED;
    if (HIBYTE(VkKey) & 2) // KBDCTRL
        KeyEvt.dwControlKeyState |= LEFT_CTRL_PRESSED; // RIGHT_CTRL_PRESSED;
    if (HIBYTE(VkKey) & 4) // KBDALT
        KeyEvt.dwControlKeyState |= LEFT_ALT_PRESSED; // RIGHT_ALT_PRESSED;

// TODO: Emit a key up?

    /* Got our key, return to caller */
    *KeyEvent = KeyEvt;
    return ERROR_SUCCESS;
}


/* CONSOLE STUFF *************************************************************/

static NTSTATUS
OpenHeadless(
    OUT PHANDLE pHandleDevice)
{
    NTSTATUS Status;
    UNICODE_STRING HdlsTermDevice = RTL_CONSTANT_STRING(DD_HDLSTERM_DEVICE_NAME_U);
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE HandleDevice;
    ULONG Enable;

    /* Allocate a local screenbuffer */
    ScreenBuffer = RtlAllocateHeap(ProcessHeap,
                                   HEAP_ZERO_MEMORY,
                                   HEADLESS_SCREEN_WIDTH *
                                   HEADLESS_SCREEN_HEIGHT * sizeof(CHAR_INFO));
    if (ScreenBuffer == NULL)
    {
        DPRINT1("Could not allocate a local screenbuffer\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Try to load the Headless Terminal Driver */
    Status = ScmLoadUnloadDriver(L"HdlsTerm", TRUE);
    if (!NT_SUCCESS(Status))
    {
        /* We failed, e.g. because redirection is disabled. Just bail out. */
        DPRINT1("Could not start the HdlsTerm driver, Status 0x%08lx\n", Status);
        goto Failure;
    }

    /* The Headless Terminal Driver is present and loaded.
     * Find whether the SAC driver is loaded as well and if so,
     * unload it as we take over the headless terminal. */
    // "\\Device\\SAC"
//
// TODO: Do the shutdown in a separate thread, since the SAC
// has the tendency to wait undefinitely in its Unload procedure.
//
#if 0 // Disabled until we fix SAC to correctly unload,
      // or move this to a separate thread.
    Status = ScmLoadUnloadDriver(L"SacDrv", FALSE);
    if (!NT_SUCCESS(Status))
    {
        /* We failed, e.g. because SAC is not present. Ignore but warn nonetheless. */
        DPRINT1("SAC driver not present, or could not be stopped, Status 0x%08lx\n", Status);
    }
#endif

    /* Open the terminal */
    InitializeObjectAttributes(&ObjectAttributes,
                               &HdlsTermDevice,
                               0,
                               NULL,
                               NULL);
    Status = NtOpenFile(&HandleDevice,
                        FILE_ALL_ACCESS,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_OPEN,
                        FILE_SYNCHRONOUS_IO_ALERT);
    if (!NT_SUCCESS(Status))
        goto Failure;

    /* Enable it */
    Enable = TRUE;
    Status = NtDeviceIoControlFile(HandleDevice,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_HDLSTERM_ENABLE_TERMINAL,
                                   &Enable,
                                   sizeof(Enable),
                                   NULL,
                                   0);
    if (!NT_SUCCESS(Status))
    {
        NtClose(HandleDevice);
        goto Failure;
    }

//
// TODO: Input handling from the Headless Terminal.
//
#if 0
    /* Open the keyboard */
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyboardName,
                               0,
                               NULL,
                               NULL);
    Status = NtOpenFile(&InputHandle,
                        FILE_ALL_ACCESS,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_OPEN,
                        0);
    if (!NT_SUCCESS(Status))
    {
        NtClose(HandleDevice);
        goto Failure;
    }

    /* Reset the queue state */
    InputQueueEmpty = TRUE;
    WaitForInput = FALSE;
#endif

    *pHandleDevice = HandleDevice;
    return STATUS_SUCCESS;

Failure:
    RtlFreeHeap(ProcessHeap, 0, ScreenBuffer);
    ScreenBuffer = NULL;
    return Status;
}

static NTSTATUS
OpenVideo(
    OUT PHANDLE pHandleDevice)
{
    NTSTATUS Status;
    UNICODE_STRING ScreenName = RTL_CONSTANT_STRING(L"\\??\\BlueScreen");
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE HandleDevice;
    ULONG Enable;

    /* Try to load the BlueScreen Driver */
    Status = ScmLoadUnloadDriver(L"Blue", TRUE);
    if (!NT_SUCCESS(Status))
    {
        /* We failed, e.g. because redirection is disabled. Just bail out. */
        DPRINT1("Could not start the BlueScreen driver, Status 0x%08lx\n", Status);
        return Status;
    }

    /* Open the screen */
    InitializeObjectAttributes(&ObjectAttributes,
                               &ScreenName,
                               0,
                               NULL,
                               NULL);
    Status = NtOpenFile(&HandleDevice,
                        FILE_ALL_ACCESS,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_OPEN,
                        FILE_SYNCHRONOUS_IO_ALERT);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Enable it */
    Enable = TRUE;
    Status = NtDeviceIoControlFile(HandleDevice,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_CONSOLE_RESET_SCREEN,
                                   &Enable,
                                   sizeof(Enable),
                                   NULL,
                                   0);
    if (!NT_SUCCESS(Status))
    {
        NtClose(HandleDevice);
        return Status;
    }

    *pHandleDevice = HandleDevice;
    return STATUS_SUCCESS;
}


BOOL
WINAPI
AllocConsole(VOID)
{
    NTSTATUS Status;
    UNICODE_STRING KeyboardName = RTL_CONSTANT_STRING(L"\\Device\\KeyboardClass0");
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    CONSOLE_SCREEN_BUFFER_INFO ConsoleScreenBufferInfo = {0};

// DbgBreakPoint();

    /* First, try to open the Headless Terminal, if available */
    Status = OpenHeadless(&HdlsTermHandle);
    if (!NT_SUCCESS(Status))
        DPRINT("Could not open the HdlsTerm driver, Status 0x%08lx\n", Status);

    /* Then, try to open the video */
    Status = OpenVideo(&OutputHandle);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Could not open the Video driver, Status 0x%08lx\n", Status);
        /* Definitively fail if we don't have the Headless Terminal */
        if (HdlsTermHandle == NULL)
            return FALSE;
    }

    if (!GetConsoleScreenBufferInfo(OutputHandle, &ConsoleScreenBufferInfo))
        DPRINT1("GetConsoleScreenBufferInfo() failed\n");
    ScreenBufferSize = ConsoleScreenBufferInfo.dwSize;

    /* Open the keyboard */
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyboardName,
                               0,
                               NULL,
                               NULL);
    Status = NtOpenFile(&InputHandle,
                        FILE_ALL_ACCESS,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_OPEN,
                        0);

    /* If we failed, fail and bail out only if we don't have the Headless Terminal */
    if (!NT_SUCCESS(Status) && !HdlsTermHandle)
    {
        if (OutputHandle)
        {
            NtClose(OutputHandle);
            OutputHandle = NULL;
        }
#if 0
        if (HdlsTermHandle)
        {
            NtClose(HdlsTermHandle);
            HdlsTermHandle = NULL;
        }
#endif
        return FALSE;
    }

    /* Reset the queue state */
    InputQueueEmpty = TRUE;
    WaitForInput = FALSE;

    return TRUE;
}


BOOL
WINAPI
AttachConsole(
    IN DWORD dwProcessId)
{
    return FALSE;
}


BOOL
WINAPI
FreeConsole(VOID)
{
    /* Reset the queue state */
    InputQueueEmpty = TRUE;
    WaitForInput = FALSE;

    if (InputHandle)
    {
        NtClose(InputHandle);
        InputHandle = NULL;
    }
    if (OutputHandle)
    {
        NtClose(OutputHandle);
        OutputHandle = NULL;
    }
    if (HdlsTermHandle)
    {
        NtClose(HdlsTermHandle);
        HdlsTermHandle = NULL;
    }
    if (ScreenBuffer)
    {
        RtlFreeHeap(ProcessHeap, 0, ScreenBuffer);
        ScreenBuffer = NULL;
    }

    return TRUE;
}


BOOL
WINAPI
WriteConsole(
    IN HANDLE hConsoleOutput,
    IN const VOID *lpBuffer,
    IN DWORD nNumberOfCharsToWrite,
    OUT LPDWORD lpNumberOfCharsWritten,
    IN LPVOID lpReserved)
{
    NTSTATUS Status, Status2;
    IO_STATUS_BLOCK IoStatusBlock;
    DWORD dwNumberOfCharsWritten;

    /* Write to the console output (video) */
    Status = NtWriteFile(hConsoleOutput,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         (PVOID)lpBuffer,
                         nNumberOfCharsToWrite,
                         NULL,
                         NULL);
    if (NT_SUCCESS(Status))
        dwNumberOfCharsWritten = IoStatusBlock.Information;

    /* Write to the Headless Terminal if present */
    Status2 = STATUS_SUCCESS;
    if (HdlsTermHandle)
    {
        Status2 = NtWriteFile(HdlsTermHandle,
                              NULL,
                              NULL,
                              NULL,
                              &IoStatusBlock,
                              (PVOID)lpBuffer,
                              nNumberOfCharsToWrite,
                              NULL,
                              NULL);
        /* Retrieve the number of characters written only if video output
         * failed, but headless terminal output succeeded. */
        if (!NT_SUCCESS(Status) && NT_SUCCESS(Status2))
            dwNumberOfCharsWritten = IoStatusBlock.Information;
    }

    if (!NT_SUCCESS(Status) && !NT_SUCCESS(Status2))
        return FALSE;

    *lpNumberOfCharsWritten = dwNumberOfCharsWritten;
    return TRUE;
}


HANDLE
WINAPI
GetStdHandle(
    IN DWORD nStdHandle)
{
    switch (nStdHandle)
    {
        case STD_INPUT_HANDLE:
            return InputHandle;
        case STD_OUTPUT_HANDLE:
            return OutputHandle;
        default:
            return NULL; // INVALID_HANDLE_VALUE;
    }
}


BOOL
WINAPI
FlushConsoleInputBuffer(
    IN HANDLE hConsoleInput)
{
    NTSTATUS Status, Status2;
    LARGE_INTEGER Offset, Timeout;
    IO_STATUS_BLOCK IoStatusBlock;
    KEYBOARD_INPUT_DATA InputData;

    /* Cancel any pending read */
    if (WaitForInput)
        NtCancelIoFile(hConsoleInput, &InputIosb /*&IoStatusBlock*/);

    /* Reset the queue state */
    InputQueueEmpty = TRUE;
    WaitForInput = FALSE;

    /* Flush the keyboard buffer */
    do
    {
        Offset.QuadPart = 0;
        Status = NtReadFile(hConsoleInput,
                            NULL,
                            NULL,
                            NULL,
                            &InputIosb, // &IoStatusBlock,
                            &InputData,
                            sizeof(InputData),
                            &Offset,
                            NULL);
        if (Status == STATUS_PENDING)
        {
            Timeout.QuadPart = -100LL; // Wait just a little bit.
            Status = NtWaitForSingleObject(hConsoleInput, FALSE, &Timeout);
            if (Status == STATUS_TIMEOUT)
            {
                NtCancelIoFile(hConsoleInput, &InputIosb /*&IoStatusBlock*/);
                goto Continue;
            }
        }
    } while (NT_SUCCESS(Status));

Continue:
    /* Flush the Headless Terminal if present */
    Status2 = STATUS_SUCCESS;
    if (HdlsTermHandle)
    {
        Status2 = NtFlushBuffersFile(HdlsTermHandle, &IoStatusBlock);
    }

    return (NT_SUCCESS(Status) || NT_SUCCESS(Status2));
}


BOOL
WINAPI
PeekConsoleInput(
    IN HANDLE hConsoleInput,
    OUT PINPUT_RECORD lpBuffer,
    IN DWORD nLength,
    OUT LPDWORD lpNumberOfEventsRead)
{
    NTSTATUS Status;
    LARGE_INTEGER Offset, Timeout;
    KEYBOARD_INPUT_DATA InputData;

    if (InputQueueEmpty)
    {
        /* Read the keyboard for an event, without waiting */
        if (!WaitForInput)
        {
            Offset.QuadPart = 0;
            Status = NtReadFile(hConsoleInput,
                                NULL,
                                NULL,
                                NULL,
                                &InputIosb,
                                &InputDataQueue,
                                sizeof(InputDataQueue),
                                &Offset,
                                NULL);
            if (!NT_SUCCESS(Status))
            {
                /* Check the Headless Terminal */
                if (HdlsTermHandle)
                {
                    KEY_EVENT_RECORD KeyEvent;
                    Status = (NTSTATUS)HdlsTermGetKeyTimeout(HdlsTermHandle,
                                                             &KeyEvent /*,
                                                             dwTimeout */);
                    if (NT_SUCCESS(Status) && Status != STATUS_PENDING)
                    {
                        lpBuffer->EventType = KEY_EVENT;
                        lpBuffer->Event.KeyEvent = KeyEvent;
                        *lpNumberOfEventsRead = 1;
                        return TRUE;
                    }
                }

                return FALSE;
            }
            if (Status == STATUS_PENDING)
            {
                /* Check the Headless Terminal */
                if (HdlsTermHandle)
                {
                    KEY_EVENT_RECORD KeyEvent;
                    Status = (NTSTATUS)HdlsTermGetKeyTimeout(HdlsTermHandle,
                                                             &KeyEvent /*,
                                                             dwTimeout */);
                    if (NT_SUCCESS(Status) && Status != STATUS_PENDING)
                    {
                        NtCancelIoFile(hConsoleInput, &InputIosb);

                        lpBuffer->EventType = KEY_EVENT;
                        lpBuffer->Event.KeyEvent = KeyEvent;
                        *lpNumberOfEventsRead = 1;
                        return TRUE;
                    }
                }

                /* No input yet, we will have to wait next time */
                *lpNumberOfEventsRead = 0;
                WaitForInput = TRUE;
                return TRUE;
            }
        }
        else
        {
            /*
             * We already tried to read from the keyboard and are
             * waiting for data, check whether something showed up.
             */
            Timeout.QuadPart = -100LL; // Wait just a little bit.
            Status = NtWaitForSingleObject(hConsoleInput, FALSE, &Timeout);
            if (Status == STATUS_TIMEOUT)
            {
                /* Check the Headless Terminal */
                if (HdlsTermHandle)
                {
                    KEY_EVENT_RECORD KeyEvent;
                    Status = (NTSTATUS)HdlsTermGetKeyTimeout(HdlsTermHandle,
                                                             &KeyEvent /*,
                                                             dwTimeout */);
                    if (NT_SUCCESS(Status) && Status != STATUS_PENDING)
                    {
                        NtCancelIoFile(hConsoleInput, &InputIosb);

                        lpBuffer->EventType = KEY_EVENT;
                        lpBuffer->Event.KeyEvent = KeyEvent;
                        *lpNumberOfEventsRead = 1;
                        return TRUE;
                    }
                }

                /* Nothing yet, continue waiting next time */
                *lpNumberOfEventsRead = 0;
                WaitForInput = TRUE;
                return TRUE;
            }
            WaitForInput = FALSE;
            if (!NT_SUCCESS(Status))
            {
                /* Check the Headless Terminal */
                if (HdlsTermHandle)
                {
                    KEY_EVENT_RECORD KeyEvent;
                    Status = (NTSTATUS)HdlsTermGetKeyTimeout(HdlsTermHandle,
                                                             &KeyEvent /*,
                                                             dwTimeout */);
                    if (NT_SUCCESS(Status) && Status != STATUS_PENDING)
                    {
                        lpBuffer->EventType = KEY_EVENT;
                        lpBuffer->Event.KeyEvent = KeyEvent;
                        *lpNumberOfEventsRead = 1;
                        return TRUE;
                    }
                }

                return FALSE;
            }
        }

        /* We got something in the queue */
        InputQueueEmpty = FALSE;
        WaitForInput = FALSE;
    }

    /* Fetch from the queue but keep it inside */
    InputData = InputDataQueue;

    lpBuffer->EventType = KEY_EVENT;
    Status = IntTranslateKey(hConsoleInput, &InputData, &lpBuffer->Event.KeyEvent);
    if (!NT_SUCCESS(Status))
        return FALSE;

    *lpNumberOfEventsRead = 1;
    return TRUE;
}


BOOL
WINAPI
ReadConsoleInput(
    IN HANDLE hConsoleInput,
    OUT PINPUT_RECORD lpBuffer,
    IN DWORD nLength,
    OUT LPDWORD lpNumberOfEventsRead)
{
    NTSTATUS Status;
    LARGE_INTEGER Offset;
    KEYBOARD_INPUT_DATA InputData;

    if (InputQueueEmpty)
    {
        /* Read the keyboard and wait for an event, skipping the queue */
        if (!WaitForInput)
        {
            Offset.QuadPart = 0;
            Status = NtReadFile(hConsoleInput,
                                NULL,
                                NULL,
                                NULL,
                                &InputIosb,
                                &InputDataQueue,
                                sizeof(InputDataQueue),
                                &Offset,
                                NULL);
            if (Status == STATUS_PENDING)
            {
                /* Check the Headless Terminal */
                if (HdlsTermHandle)
                {
                    KEY_EVENT_RECORD KeyEvent;
                    Status = (NTSTATUS)HdlsTermGetKeyTimeout(HdlsTermHandle,
                                                             &KeyEvent /*,
                                                             dwTimeout */);
                    if (NT_SUCCESS(Status) && Status != STATUS_PENDING)
                    {
                        NtCancelIoFile(hConsoleInput, &InputIosb);

                        lpBuffer->EventType = KEY_EVENT;
                        lpBuffer->Event.KeyEvent = KeyEvent;
                        *lpNumberOfEventsRead = 1;
                        return TRUE;
                    }
                }

                /* Block and wait for input */
                WaitForInput = TRUE;
                Status = NtWaitForSingleObject(hConsoleInput, FALSE, NULL);
                WaitForInput = FALSE;
                Status = InputIosb.Status;
            }
            if (!NT_SUCCESS(Status))
            {
                /* Check the Headless Terminal */
                if (HdlsTermHandle)
                {
                    KEY_EVENT_RECORD KeyEvent;
                    Status = (NTSTATUS)HdlsTermGetKeyTimeout(HdlsTermHandle,
                                                             &KeyEvent /*,
                                                             dwTimeout */);
                    if (NT_SUCCESS(Status) && Status != STATUS_PENDING)
                    {
                        lpBuffer->EventType = KEY_EVENT;
                        lpBuffer->Event.KeyEvent = KeyEvent;
                        *lpNumberOfEventsRead = 1;
                        return TRUE;
                    }
                }

                return FALSE;
            }
        }
        else
        {
            /* Check the Headless Terminal */
            if (HdlsTermHandle)
            {
                KEY_EVENT_RECORD KeyEvent;
                Status = (NTSTATUS)HdlsTermGetKeyTimeout(HdlsTermHandle,
                                                         &KeyEvent /*,
                                                         dwTimeout */);
                if (NT_SUCCESS(Status) && Status != STATUS_PENDING)
                {
                    NtCancelIoFile(hConsoleInput, &InputIosb);

                    lpBuffer->EventType = KEY_EVENT;
                    lpBuffer->Event.KeyEvent = KeyEvent;
                    *lpNumberOfEventsRead = 1;
                    return TRUE;
                }
            }

            /*
             * We already tried to read from the keyboard and are
             * waiting for data, block and wait for input.
             */
            Status = NtWaitForSingleObject(hConsoleInput, FALSE, NULL);
            WaitForInput = FALSE;
            Status = InputIosb.Status;
            if (!NT_SUCCESS(Status))
            {
                /* Check the Headless Terminal */
                if (HdlsTermHandle)
                {
                    KEY_EVENT_RECORD KeyEvent;
                    Status = (NTSTATUS)HdlsTermGetKeyTimeout(HdlsTermHandle,
                                                             &KeyEvent /*,
                                                             dwTimeout */);
                    if (NT_SUCCESS(Status) && Status != STATUS_PENDING)
                    {
                        lpBuffer->EventType = KEY_EVENT;
                        lpBuffer->Event.KeyEvent = KeyEvent;
                        *lpNumberOfEventsRead = 1;
                        return TRUE;
                    }
                }

                return FALSE;
            }
        }
    }

    /* Fetch from the queue and empty it */
    InputData = InputDataQueue;
    InputQueueEmpty = TRUE;

    lpBuffer->EventType = KEY_EVENT;
    Status = IntTranslateKey(hConsoleInput, &InputData, &lpBuffer->Event.KeyEvent);
    if (!NT_SUCCESS(Status))
        return FALSE;

    *lpNumberOfEventsRead = 1;
    return TRUE;
}


BOOL
WINAPI
WriteConsoleOutputCharacterA(
    HANDLE hConsoleOutput,
    IN LPCSTR lpCharacter,
    IN DWORD nLength,
    IN COORD dwWriteCoord,
    OUT LPDWORD lpNumberOfCharsWritten)
{
    NTSTATUS Status, Status2;
    IO_STATUS_BLOCK IoStatusBlock;
    PCHAR Buffer;
    COORD *pCoord;
    PCHAR pText;
    DWORD dwNumberOfCharsWritten;

    Buffer = (CHAR*)RtlAllocateHeap(ProcessHeap,
                                    0,
                                    nLength + sizeof(COORD));
    if (Buffer == NULL)
        return FALSE;

    pCoord = (COORD *)Buffer;
    pText = (PCHAR)(pCoord + 1);

    *pCoord = dwWriteCoord;
    memcpy(pText, lpCharacter, nLength);

    /* Write to the console output (video) */
    Status = NtDeviceIoControlFile(hConsoleOutput,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_CONSOLE_WRITE_OUTPUT_CHARACTER,
                                   NULL,
                                   0,
                                   Buffer,
                                   nLength + sizeof(COORD));
    if (NT_SUCCESS(Status))
        dwNumberOfCharsWritten = IoStatusBlock.Information;

    /* Write to the Headless Terminal if present */
    Status2 = STATUS_SUCCESS;
    if (HdlsTermHandle)
    {
        HEADLESS_CMD_POSITION_CURSOR CursorPos;

        /*
         * IMPORTANT NOTICE! Terminal will write the text using
         * the **CURRENT** text attributes, and NOT the attributes
         * that are existing on the output text buffer.
         * This problem can only be fixed once we cache a local
         * output text buffer.
         */

        if ( dwWriteCoord.X < 0 || dwWriteCoord.X >= ScreenBufferSize.X  ||
             dwWriteCoord.Y < 0 || dwWriteCoord.Y >= ScreenBufferSize.Y ||
             nLength == 0 )
        {
            Status2 = STATUS_SUCCESS;
            goto Done;
        }

        /*
         * Determine the actual maximum number of characters we can
         * write from the wanted position down to the bottom of the
         * screen, without the need to scroll.
         */
        nLength = min(nLength,
                      (ScreenBufferSize.Y - dwWriteCoord.Y)
                         * ScreenBufferSize.X - dwWriteCoord.X);
        if (nLength == 0)
        {
            Status2 = STATUS_SUCCESS;
            goto Done;
        }

        /* Save cursor position */
        Status2 = NtWriteFile(HdlsTermHandle,
                              NULL,
                              NULL,
                              NULL,
                              &IoStatusBlock,
                              (PVOID)"\x1B[s",
                              3,
                              NULL,
                              NULL);

        /* Position cursor */
        CursorPos.CursorCol = dwWriteCoord.X;
        CursorPos.CursorRow = dwWriteCoord.Y;
        Status2 = NtDeviceIoControlFile(HdlsTermHandle,
                                        NULL,
                                        NULL,
                                        NULL,
                                        &IoStatusBlock,
                                        IOCTL_HDLSTERM_POSITION_CURSOR,
                                        &CursorPos,
                                        sizeof(CursorPos),
                                        NULL,
                                        0);

        /* Write the string */
        Status2 = NtWriteFile(HdlsTermHandle,
                              NULL,
                              NULL,
                              NULL,
                              &IoStatusBlock,
                              (PVOID)lpCharacter,
                              nLength,
                              NULL,
                              NULL);
        /* Retrieve the number of characters written only if video output
         * failed, but headless terminal output succeeded. */
        if (!NT_SUCCESS(Status) && NT_SUCCESS(Status2))
            dwNumberOfCharsWritten = IoStatusBlock.Information;

        /* Restore the previous cursor position */
        Status2 = NtWriteFile(HdlsTermHandle,
                              NULL,
                              NULL,
                              NULL,
                              &IoStatusBlock,
                              (PVOID)"\x1B[u",
                              3,
                              NULL,
                              NULL);
        Done: ;
    }

    RtlFreeHeap(ProcessHeap, 0, Buffer);

    if (!NT_SUCCESS(Status) && !NT_SUCCESS(Status2))
        return FALSE;

    *lpNumberOfCharsWritten = dwNumberOfCharsWritten;
    return TRUE;
}


BOOL
WINAPI
WriteConsoleOutputCharacterW(
    HANDLE hConsoleOutput,
    IN LPCWSTR lpCharacter,
    IN DWORD nLength,
    IN COORD dwWriteCoord,
    OUT LPDWORD lpNumberOfCharsWritten)
{
    NTSTATUS Status;
    BOOL Success;
    UNICODE_STRING UnicodeString;
    OEM_STRING OemString;
    // ULONG OemLength;

    UnicodeString.Length = nLength * sizeof(WCHAR);
    UnicodeString.MaximumLength = nLength * sizeof(WCHAR);
    UnicodeString.Buffer = (PWSTR)lpCharacter;

    // OemLength = RtlUnicodeStringToOemSize(&UnicodeString);
    RtlInitEmptyAnsiString(&OemString, NULL, 0);
    Status = RtlUnicodeStringToOemString(&OemString,
                                         &UnicodeString,
                                         TRUE);
    if (!NT_SUCCESS(Status))
        return FALSE;

    Success = WriteConsoleOutputCharacterA(hConsoleOutput,
                                           OemString.Buffer,
                                           OemString.Length,
                                           dwWriteCoord,
                                           lpNumberOfCharsWritten);

    RtlFreeOemString(&OemString);

    return Success;
}


BOOL
WINAPI
FillConsoleOutputAttribute(
    IN HANDLE hConsoleOutput,
    IN WORD wAttribute,
    IN DWORD nLength,
    IN COORD dwWriteCoord,
    OUT LPDWORD lpNumberOfAttrsWritten)
{
    IO_STATUS_BLOCK IoStatusBlock;
    OUTPUT_ATTRIBUTE Buffer;
    NTSTATUS Status;

    Buffer.wAttribute = wAttribute;
    Buffer.nLength    = nLength;
    Buffer.dwCoord    = dwWriteCoord;

    Status = NtDeviceIoControlFile(hConsoleOutput,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_CONSOLE_FILL_OUTPUT_ATTRIBUTE,
                                   &Buffer,
                                   sizeof(OUTPUT_ATTRIBUTE),
                                   &Buffer,
                                   sizeof(OUTPUT_ATTRIBUTE));
    if (!NT_SUCCESS(Status))
        return FALSE;

    *lpNumberOfAttrsWritten = Buffer.dwTransfered;
    return TRUE;
}


BOOL
WINAPI
FillConsoleOutputCharacterA(
    IN HANDLE hConsoleOutput,
    IN CHAR cCharacter,
    IN DWORD nLength,
    IN COORD dwWriteCoord,
    OUT LPDWORD lpNumberOfCharsWritten)
{
    NTSTATUS Status, Status2;
    IO_STATUS_BLOCK IoStatusBlock;
    OUTPUT_CHARACTER Buffer;
    DWORD dwNumberOfCharsWritten;

    Buffer.cCharacter = cCharacter;
    Buffer.nLength    = nLength;
    Buffer.dwCoord    = dwWriteCoord;

    /* Write to the console output (video) */
    Status = NtDeviceIoControlFile(hConsoleOutput,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_CONSOLE_FILL_OUTPUT_CHARACTER,
                                   &Buffer,
                                   sizeof(OUTPUT_CHARACTER),
                                   &Buffer,
                                   sizeof(OUTPUT_CHARACTER));
    if (NT_SUCCESS(Status))
        dwNumberOfCharsWritten = Buffer.dwTransfered; // IoStatusBlock.Information;

    /* Write to the Headless Terminal if present */
    Status2 = STATUS_SUCCESS;
    if (HdlsTermHandle)
    {
        HEADLESS_CMD_POSITION_CURSOR CursorPos;

        /*
         * IMPORTANT NOTICE! Terminal will write the text using
         * the **CURRENT** text attributes, and NOT the attributes
         * that are existing on the output text buffer.
         * This problem can only be fixed once we cache a local
         * output text buffer.
         */

        if ( dwWriteCoord.X < 0 || dwWriteCoord.X >= ScreenBufferSize.X  ||
             dwWriteCoord.Y < 0 || dwWriteCoord.Y >= ScreenBufferSize.Y ||
             nLength == 0 )
        {
            Status2 = STATUS_SUCCESS;
            goto Done;
        }

        /*
         * Determine the actual maximum number of characters we can
         * write from the wanted position down to the bottom of the
         * screen, without the need to scroll.
         */
        nLength = min(nLength,
                      (ScreenBufferSize.Y - dwWriteCoord.Y)
                         * ScreenBufferSize.X - dwWriteCoord.X);
        if (nLength == 0)
        {
            Status2 = STATUS_SUCCESS;
            goto Done;
        }

        /* Save cursor position */
        Status2 = NtWriteFile(HdlsTermHandle,
                              NULL,
                              NULL,
                              NULL,
                              &IoStatusBlock,
                              (PVOID)"\x1B[s",
                              3,
                              NULL,
                              NULL);

        /* Position cursor */
        CursorPos.CursorCol = dwWriteCoord.X;
        CursorPos.CursorRow = dwWriteCoord.Y;
        Status2 = NtDeviceIoControlFile(HdlsTermHandle,
                                        NULL,
                                        NULL,
                                        NULL,
                                        &IoStatusBlock,
                                        IOCTL_HDLSTERM_POSITION_CURSOR,
                                        &CursorPos,
                                        sizeof(CursorPos),
                                        NULL,
                                        0);

        /* Repeatedly write the character */
        while (nLength-- > 0)
        {
            Status2 = NtWriteFile(HdlsTermHandle,
                                  NULL,
                                  NULL,
                                  NULL,
                                  &IoStatusBlock,
                                  (PVOID)&cCharacter,
                                  sizeof(CHAR),
                                  NULL,
                                  NULL);
            /* Retrieve the number of characters written only if video output
             * failed, but headless terminal output succeeded. */
            if (!NT_SUCCESS(Status) && NT_SUCCESS(Status2))
                dwNumberOfCharsWritten += IoStatusBlock.Information;
        }

        /* Restore the previous cursor position */
        Status2 = NtWriteFile(HdlsTermHandle,
                              NULL,
                              NULL,
                              NULL,
                              &IoStatusBlock,
                              (PVOID)"\x1B[u",
                              3,
                              NULL,
                              NULL);
        Done: ;
    }

    if (!NT_SUCCESS(Status) && !NT_SUCCESS(Status2))
        return FALSE;

    *lpNumberOfCharsWritten = dwNumberOfCharsWritten;
    return TRUE;
}


BOOL
WINAPI
GetConsoleScreenBufferInfo(
    IN HANDLE hConsoleOutput,
    OUT PCONSOLE_SCREEN_BUFFER_INFO lpConsoleScreenBufferInfo)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatusBlock;

    /* If the Headless Terminal is not connected, return the actual
     * video buffer size, otherwise return hardcoded terminal size. */
#if 0
    if (!HdlsTermHandle)
#else
    if (hConsoleOutput) // OutputHandle
#endif
    {
        Status = NtDeviceIoControlFile(hConsoleOutput,
                                       NULL,
                                       NULL,
                                       NULL,
                                       &IoStatusBlock,
                                       IOCTL_CONSOLE_GET_SCREEN_BUFFER_INFO,
                                       NULL,
                                       0,
                                       lpConsoleScreenBufferInfo,
                                       sizeof(CONSOLE_SCREEN_BUFFER_INFO));
    }
    else
    {
        lpConsoleScreenBufferInfo->dwSize.X = HEADLESS_SCREEN_WIDTH;
        lpConsoleScreenBufferInfo->dwSize.Y = HEADLESS_SCREEN_HEIGHT;
        // lpConsoleScreenBufferInfo->dwCursorPosition;
        // lpConsoleScreenBufferInfo->wAttributes;
        // lpConsoleScreenBufferInfo->srWindow;
        // lpConsoleScreenBufferInfo->dwMaximumWindowSize;
        Status = STATUS_SUCCESS;
    }

    return NT_SUCCESS(Status);
}


BOOL
WINAPI
SetConsoleCursorInfo(
    IN HANDLE hConsoleOutput,
    IN const CONSOLE_CURSOR_INFO *lpConsoleCursorInfo)
{
    NTSTATUS Status, Status2;
    IO_STATUS_BLOCK IoStatusBlock;

    /* Write to the console output (video) */
    Status = NtDeviceIoControlFile(hConsoleOutput,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_CONSOLE_SET_CURSOR_INFO,
                                   (PCONSOLE_CURSOR_INFO)lpConsoleCursorInfo,
                                   sizeof(CONSOLE_CURSOR_INFO),
                                   NULL,
                                   0);

    /* Write to the Headless Terminal if present */
    Status2 = STATUS_SUCCESS;
    if (HdlsTermHandle)
    {
        BOOLEAN bInsert, bVisible;
        SIZE_T DataBufferSize;
        CHAR DataBuffer[80];

        bInsert  = (lpConsoleCursorInfo->dwSize < 50);
        bVisible = lpConsoleCursorInfo->bVisible;
        Status2 = RtlStringCbPrintfA(/*(PCHAR)*/DataBuffer,
                                     sizeof(DataBuffer),
                                     "\x1B[%hu q"  // Mode style
                                     "\x1B[?25%c", // Visible (h) or hidden (l)
                                     bInsert  ?  3  :  1, // Blinking underline (3) or blinking block (1)
                                     bVisible ? 'h' : 'l');
        // if (!NT_SUCCESS(Status2)) break;

        /* Write the command string */
        DataBufferSize = strlen(DataBuffer) * sizeof(CHAR);
        Status2 = NtWriteFile(HdlsTermHandle,
                              NULL,
                              NULL,
                              NULL,
                              &IoStatusBlock,
                              (PVOID)DataBuffer,
                              DataBufferSize,
                              NULL,
                              NULL);
    }

    return (NT_SUCCESS(Status) || NT_SUCCESS(Status2));
}


BOOL
WINAPI
SetConsoleCursorPosition(
    IN HANDLE hConsoleOutput,
    IN COORD dwCursorPosition)
{
    NTSTATUS Status, Status2;
    IO_STATUS_BLOCK IoStatusBlock;
    CONSOLE_SCREEN_BUFFER_INFO ConsoleScreenBufferInfo;

    if (!GetConsoleScreenBufferInfo(hConsoleOutput, &ConsoleScreenBufferInfo))
        return FALSE;

    ConsoleScreenBufferInfo.dwCursorPosition.X = dwCursorPosition.X;
    ConsoleScreenBufferInfo.dwCursorPosition.Y = dwCursorPosition.Y;

    /* Write to the console output (video) */
    Status = NtDeviceIoControlFile(hConsoleOutput,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_CONSOLE_SET_SCREEN_BUFFER_INFO,
                                   &ConsoleScreenBufferInfo,
                                   sizeof(CONSOLE_SCREEN_BUFFER_INFO),
                                   NULL,
                                   0);

    /* Write to the Headless Terminal if present */
    Status2 = STATUS_SUCCESS;
    if (HdlsTermHandle)
    {
        HEADLESS_CMD_POSITION_CURSOR CursorPos;

        if ( dwCursorPosition.X < 0 || dwCursorPosition.X >= ScreenBufferSize.X ||
             dwCursorPosition.Y < 0 || dwCursorPosition.Y >= ScreenBufferSize.Y )
        {
            Status2 = STATUS_INVALID_PARAMETER;
            goto Done;
        }

        CursorPos.CursorCol = dwCursorPosition.X;
        CursorPos.CursorRow = dwCursorPosition.Y;
        Status2 = NtDeviceIoControlFile(HdlsTermHandle,
                                        NULL,
                                        NULL,
                                        NULL,
                                        &IoStatusBlock,
                                        IOCTL_HDLSTERM_POSITION_CURSOR,
                                        &CursorPos,
                                        sizeof(CursorPos),
                                        NULL,
                                        0);
        Done: ;
    }

    return (NT_SUCCESS(Status) || NT_SUCCESS(Status2));
}


BOOL
WINAPI
SetConsoleTextAttribute(
    IN HANDLE hConsoleOutput,
    IN WORD wAttributes)
{
    NTSTATUS Status, Status2;
    IO_STATUS_BLOCK IoStatusBlock;

    /* Write to the console output (video) */
    Status = NtDeviceIoControlFile(hConsoleOutput,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_CONSOLE_SET_TEXT_ATTRIBUTE,
                                   &wAttributes,
                                   sizeof(USHORT),
                                   NULL,
                                   0);

    /* Write to the Headless Terminal if present */
    Status2 = STATUS_SUCCESS;
    if (HdlsTermHandle)
    {
        HEADLESS_CMD_SET_COLOR SetColor;
        SIZE_T DataBufferSize;
        CHAR DataBuffer[80];

        /*
         * As we are going to set many attributes, do it at once
         * using an ANSI control sequence string. Otherwise we
         * could call separately IOCTL_HDLSTERM_ATTRIBUTES_OFF,
         * IOCTL_HDLSTERM_SET_COLOR and IOCTL_HDLSTERM_INVERSE_VIDEO.
         */

        /* Reset default attributes (will turn off inverse video etc.) */
#if 0
        Status2 = NtDeviceIoControlFile(HdlsTermHandle,
                                        NULL,
                                        NULL,
                                        NULL,
                                        &IoStatusBlock,
                                        IOCTL_HDLSTERM_ATTRIBUTES_OFF,
                                        NULL,
                                        0,
                                        NULL,
                                        0);
#else
        Status2 = RtlStringCbCopyA(/*(PCHAR)*/DataBuffer,
                                   sizeof(DataBuffer),
                                   "\x1B[0m");
        // if (!NT_SUCCESS(Status2)) break;
        DataBufferSize = strlen(DataBuffer) * sizeof(CHAR);
#endif

        /* Set the current color */
        SetColor.TextColor = 30 + CGA_TO_ANSI_COLOR(wAttributes & 0x0F);
        SetColor.BkgdColor = 40 + CGA_TO_ANSI_COLOR(((wAttributes & 0xF0) >> 4));
#if 0
        Status2 = NtDeviceIoControlFile(HdlsTermHandle,
                                        NULL,
                                        NULL,
                                        NULL,
                                        &IoStatusBlock,
                                        IOCTL_HDLSTERM_SET_COLOR,
                                        &SetColor,
                                        sizeof(SetColor),
                                        NULL,
                                        0);
#else
        Status2 = RtlStringCbPrintfA((PCHAR)((ULONG_PTR)DataBuffer + DataBufferSize),
                                     sizeof(DataBuffer) - DataBufferSize,
                                     "\x1B[%d;%dm",
                                     SetColor.BkgdColor,
                                     SetColor.TextColor);
        // if (!NT_SUCCESS(Status2)) break;
#endif

        /* Turn on/off the inverse video */
        if (wAttributes & COMMON_LVB_REVERSE_VIDEO)
        {
#if 0
            Status2 = NtDeviceIoControlFile(HdlsTermHandle,
                                            NULL,
                                            NULL,
                                            NULL,
                                            &IoStatusBlock,
                                            IOCTL_HDLSTERM_INVERSE_VIDEO,
                                            NULL,
                                            0,
                                            NULL,
                                            0);
#else
            Status2 = RtlStringCbCatA(/*(PCHAR)*/DataBuffer,
                                      sizeof(DataBuffer),
                                      "\x1B[7m");
            // if (!NT_SUCCESS(Status2)) break;
#endif
        }

        /* Turn on/off underscore */
        if (wAttributes & COMMON_LVB_UNDERSCORE)
        {
            Status2 = RtlStringCbCatA(/*(PCHAR)*/DataBuffer,
                                      sizeof(DataBuffer),
                                      "\x1B[4m");
            // if (!NT_SUCCESS(Status2)) break;
        }

        /* Write the command string */
        DataBufferSize = strlen(DataBuffer) * sizeof(CHAR);
        Status2 = NtWriteFile(HdlsTermHandle,
                              NULL,
                              NULL,
                              NULL,
                              &IoStatusBlock,
                              (PVOID)DataBuffer,
                              DataBufferSize,
                              NULL,
                              NULL);
    }

    return (NT_SUCCESS(Status) || NT_SUCCESS(Status2));
}


BOOL
WINAPI
SetConsoleOutputCP(
    IN UINT wCodepage)
{
    WCHAR FontName[100];
    WCHAR FontFile[] = L"\\SystemRoot\\vgafonts.cab";
    CONSOLE_CABINET_CONTEXT ConsoleCabinetContext;
    PCABINET_CONTEXT CabinetContext = &ConsoleCabinetContext.CabinetContext;
    CAB_SEARCH Search;
    ULONG CabStatus;
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;

    if (wCodepage == LastLoadedCodepage)
        return TRUE;

    /*
     * If we have a terminal attached, print on the top right a line
     * indicating which codepage the user should set manually.
     */
    if (HdlsTermHandle)
    {
        NTSTATUS Status2;
        HEADLESS_CMD_POSITION_CURSOR CursorPos;
        HEADLESS_CMD_SET_COLOR SetColor;
        USHORT wAttributes;
        CHAR CodePageName[2+10+1];

        /* Save cursor position */
        Status2 = NtWriteFile(HdlsTermHandle,
                              NULL,
                              NULL,
                              NULL,
                              &IoStatusBlock,
                              (PVOID)"\x1B[s",
                              3,
                              NULL,
                              NULL);

        /* Format the codepage number string */
        RtlStringCbPrintfA(CodePageName, sizeof(CodePageName),
                           "CP%u", wCodepage);

        /* Position cursor on the first line and print on the right */
        CursorPos.CursorCol = ScreenBufferSize.X - strlen(CodePageName);
        CursorPos.CursorRow = 0;
        Status2 = NtDeviceIoControlFile(HdlsTermHandle,
                                        NULL,
                                        NULL,
                                        NULL,
                                        &IoStatusBlock,
                                        IOCTL_HDLSTERM_POSITION_CURSOR,
                                        &CursorPos,
                                        sizeof(CursorPos),
                                        NULL,
                                        0);

        /* Show the codepage number to the user, in red on black */
        wAttributes = FOREGROUND_RED | FOREGROUND_INTENSITY | BACKGROUND_BLACK;
        SetColor.TextColor = 30 + CGA_TO_ANSI_COLOR(wAttributes & 0x0F);
        SetColor.BkgdColor = 40 + CGA_TO_ANSI_COLOR(((wAttributes & 0xF0) >> 4));
        Status2 = NtDeviceIoControlFile(HdlsTermHandle,
                                        NULL,
                                        NULL,
                                        NULL,
                                        &IoStatusBlock,
                                        IOCTL_HDLSTERM_SET_COLOR,
                                        &SetColor,
                                        sizeof(SetColor),
                                        NULL,
                                        0);

        Status2 = NtWriteFile(HdlsTermHandle,
                              NULL,
                              NULL,
                              NULL,
                              &IoStatusBlock,
                              (PVOID)CodePageName,
                              strlen(CodePageName) * sizeof(CHAR),
                              NULL,
                              NULL);

        /* Restore the previous cursor position */
        Status2 = NtWriteFile(HdlsTermHandle,
                              NULL,
                              NULL,
                              NULL,
                              &IoStatusBlock,
                              (PVOID)"\x1B[u",
                              3,
                              NULL,
                              NULL);
    }

    if (!OutputHandle)
    {
        /* Fail if neither video or terminal is available,
         * otherwise if only headless terminal is available,
         * bail out now and claim success. */
        return (HdlsTermHandle != NULL);
    }

    CabinetInitialize(CabinetContext);
    CabinetSetEventHandlers(CabinetContext,
                            NULL, NULL, NULL, ConsoleCreateFileHandler);
    CabinetSetCabinetName(CabinetContext, FontFile);

    CabStatus = CabinetOpen(CabinetContext);
    if (CabStatus != CAB_STATUS_SUCCESS)
    {
        DPRINT("CabinetOpen('%S') returned 0x%08x\n", FontFile, CabStatus);
        return FALSE;
    }

    RtlStringCbPrintfW(FontName, sizeof(FontName),
                       L"%u-8x8.bin", wCodepage);
    CabStatus = CabinetFindFirst(CabinetContext, FontName, &Search);
    if (CabStatus != CAB_STATUS_SUCCESS)
    {
        DPRINT("CabinetFindFirst('%S', '%S') returned 0x%08x\n", FontFile, FontName, CabStatus);
        CabinetClose(CabinetContext);
        return FALSE;
    }

    CabStatus = CabinetExtractFile(CabinetContext, &Search);
    CabinetClose(CabinetContext);
    if (CabStatus != CAB_STATUS_SUCCESS)
    {
        DPRINT("CabinetLoadFile('%S', '%S') returned 0x%08x\n", FontFile, FontName, CabStatus);
        if (ConsoleCabinetContext.Data)
            RtlFreeHeap(ProcessHeap, 0, ConsoleCabinetContext.Data);
        return FALSE;
    }
    ASSERT(ConsoleCabinetContext.Data);

    Status = NtDeviceIoControlFile(OutputHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_CONSOLE_SETFONT,
                                   ConsoleCabinetContext.Data,
                                   ConsoleCabinetContext.Size,
                                   NULL,
                                   0);

    RtlFreeHeap(ProcessHeap, 0, ConsoleCabinetContext.Data);

    if (!NT_SUCCESS(Status))
          return FALSE;

    LastLoadedCodepage = wCodepage;
    return TRUE;
}


/* EOF */
