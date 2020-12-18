/*
 * PROJECT:     NT / ReactOS Console Display Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Console Display Driver IOCTL Interface.
 * COPYRIGHT:   Copyright 2020 Hermes Belusca-Maito
 */

#ifndef _NTDDBLUE_H_INCLUDED_
#define _NTDDBLUE_H_INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif

#ifdef __cplusplus
extern "C" {
#endif

//
// Device Names
//
#define DD_NTCONDD_DEVICE_NAME              "\\Device\\NtConDd"
#define DD_NTCONDD_DEVICE_NAME_U           L"\\Device\\NtConDd"
#define DD_NTCONDD_SYMLNK_NAME              "\\DosDevices\\Global\\NtConDd"
#define DD_NTCONDD_SYMLNK_NAME_U           L"\\DosDevices\\Global\\NtConDd"


//
// I/O Control Codes
//
#define IOCTL_CONSOLE_RESET_SCREEN              CTL_CODE(FILE_DEVICE_SCREEN, 0x800, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_CONSOLE_GET_SCREEN_BUFFER_INFO    CTL_CODE(FILE_DEVICE_SCREEN, 0x801, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_CONSOLE_SET_SCREEN_BUFFER_INFO    CTL_CODE(FILE_DEVICE_SCREEN, 0x802, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_CONSOLE_GET_CURSOR_INFO           CTL_CODE(FILE_DEVICE_SCREEN, 0x803, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_CONSOLE_SET_CURSOR_INFO           CTL_CODE(FILE_DEVICE_SCREEN, 0x804, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_CONSOLE_GET_MODE                  CTL_CODE(FILE_DEVICE_SCREEN, 0x805, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_CONSOLE_SET_MODE                  CTL_CODE(FILE_DEVICE_SCREEN, 0x806, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_CONSOLE_FILL_OUTPUT_ATTRIBUTE     CTL_CODE(FILE_DEVICE_SCREEN, 0x810, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_CONSOLE_READ_OUTPUT_ATTRIBUTE     CTL_CODE(FILE_DEVICE_SCREEN, 0x811, METHOD_OUT_DIRECT, FILE_READ_ACCESS)
#define IOCTL_CONSOLE_WRITE_OUTPUT_ATTRIBUTE    CTL_CODE(FILE_DEVICE_SCREEN, 0x812, METHOD_IN_DIRECT, FILE_WRITE_ACCESS)
#define IOCTL_CONSOLE_SET_TEXT_ATTRIBUTE        CTL_CODE(FILE_DEVICE_SCREEN, 0x813, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_CONSOLE_FILL_OUTPUT_CHARACTER     CTL_CODE(FILE_DEVICE_SCREEN, 0x820, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_CONSOLE_READ_OUTPUT_CHARACTER     CTL_CODE(FILE_DEVICE_SCREEN, 0x821, METHOD_OUT_DIRECT, FILE_READ_ACCESS)
#define IOCTL_CONSOLE_WRITE_OUTPUT_CHARACTER    CTL_CODE(FILE_DEVICE_SCREEN, 0x822, METHOD_IN_DIRECT, FILE_WRITE_ACCESS)

#define IOCTL_CONSOLE_DRAW                      CTL_CODE(FILE_DEVICE_SCREEN, 0x830, METHOD_IN_DIRECT, FILE_WRITE_ACCESS)

#define IOCTL_CONSOLE_LOADFONT                  CTL_CODE(FILE_DEVICE_SCREEN, 0x840, METHOD_IN_DIRECT, FILE_WRITE_ACCESS)


//
// Control Structures
//

typedef enum _CONSOLE_TYPE
{
    ConsoleText,        // Uses text-mode VGA video if possible.
                        // Otherwise emulates text-mode over graphics framebuffer.

    ConsoleFramebuffer, // Uses \Device\Video0 video driver.
    ConsoleInbv,        // Uses NTOS Inbv / Bootvid driver.
    ConsoleHeadless     // Uses NTOS Headless.
};

typedef struct _CONSOLE_MODE
{
    ULONG dwMode;
} CONSOLE_MODE, *PCONSOLE_MODE;

typedef struct _OUTPUT_ATTRIBUTE
{
    USHORT wAttribute;
    ULONG  nLength;
    COORD  dwCoord;
    ULONG  dwTransfered;
} OUTPUT_ATTRIBUTE, *POUTPUT_ATTRIBUTE;

typedef struct _OUTPUT_CHARACTER
{
    CHAR  cCharacter;
    ULONG nLength;
    COORD dwCoord;
    ULONG dwTransfered;
} OUTPUT_CHARACTER, *POUTPUT_CHARACTER;

typedef struct _CONSOLE_DRAW
{
    USHORT X;       /* Origin */
    USHORT Y;
    USHORT SizeX;   /* Size of the screen buffer (chars) */
    USHORT SizeY;
    USHORT CursorX; /* New cursor position (screen-relative) */
    USHORT CursorY;
    /* Followed by screen buffer in char/attrib format */
} CONSOLE_DRAW, *PCONSOLE_DRAW;

#ifdef __cplusplus
}
#endif

#endif /* _NTDDBLUE_H_INCLUDED_ */
