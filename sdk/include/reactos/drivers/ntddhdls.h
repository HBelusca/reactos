/*
 * PROJECT:     NT / ReactOS Headless Terminal Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Headless Terminal Driver IOCTL Interface.
 * COPYRIGHT:   Copyright 2020 Hermes Belusca-Maito
 */

#ifndef _NTDDHDLS_H_
#define _NTDDHDLS_H_

#if _MSC_VER > 1000
#pragma once
#endif

#ifdef __cplusplus
extern "C" {
#endif

// #include <ntoskrnl/include/internal/hdl.h>

//
// Device Names
//
#define DD_HDLSTERM_DEVICE_NAME             "\\Device\\HdlsTerm"
#define DD_HDLSTERM_DEVICE_NAME_U          L"\\Device\\HdlsTerm"


//
// I/O Control Codes
//

/**
 * @name IOCTL_HDLSTERM_ENABLE_TERMINAL
 *     Enable or disable the Headless terminal.
 *
 * @param[in]   ULONG Enable, sizeof(ULONG)
 *     Boolean (in ULONG) to specify whether to enable (TRUE)
 *     or disable (FALSE) the terminal.
 *
 * @return
 *     Status in IO_STATUS_BLOCK IoStatus.Status.
 *
 * @note
 *     Output buffers are unused.
 **/
#define IOCTL_HDLSTERM_ENABLE_TERMINAL \
    CTL_CODE(FILE_DEVICE_CONSOLE, 0x101, METHOD_BUFFERED, FILE_WRITE_ACCESS)

/**
 * @name IOCTL_HDLSTERM_QUERY_INFO
 *     Query some information about the Headless terminal.
 *
 * @param[out]  PHEADLESS_RSP_QUERY_INFO HeadlessInfo, sizeof(HEADLESS_RSP_QUERY_INFO)
 *     Pointer to a HEADLESS_RSP_QUERY_INFO buffer that will receive
 *     the headless information.
 *
 * @return
 *     Status in IO_STATUS_BLOCK IoStatus.Status.
 *     IO_STATUS_BLOCK IoStatus.Information is set to sizeof(HEADLESS_RSP_QUERY_INFO).
 *
 * @note
 *     Input buffers are unused.
 **/
#define IOCTL_HDLSTERM_QUERY_INFO \
    CTL_CODE(FILE_DEVICE_CONSOLE, 0x102, METHOD_BUFFERED, FILE_READ_ACCESS)

/**
 * @name IOCTL_HDLSTERM_SET_COLOR
 *     Change the current text (foreground) and background colours.
 *
 * @param[in]   PHEADLESS_CMD_SET_COLOR SetColor, sizeof(HEADLESS_CMD_SET_COLOR)
 *     Pointer to a HEADLESS_CMD_SET_COLOR buffer (pair of two ULONGs)
 *     that specify the new text and background colors.
 *
 * @return
 *     Status in IO_STATUS_BLOCK IoStatus.Status.
 *
 * @note
 *     Output buffers are unused.
 **/
#define IOCTL_HDLSTERM_SET_COLOR \
    CTL_CODE(FILE_DEVICE_CONSOLE, 0x103, METHOD_BUFFERED, FILE_WRITE_ACCESS)

/**
 * @name IOCTL_HDLSTERM_CLEAR_DISPLAY
 *     Clear the whole terminal display.
 *
 * @return
 *     Status in IO_STATUS_BLOCK IoStatus.Status.
 *
 * @note
 *     Neither input or output buffers are used.
 **/
#define IOCTL_HDLSTERM_CLEAR_DISPLAY \
    CTL_CODE(FILE_DEVICE_CONSOLE, 0x104, METHOD_NEITHER, FILE_WRITE_ACCESS)

/**
 * @name IOCTL_HDLSTERM_CLEAR_TO_END_DISPLAY
 *     Clear the terminal display from the current cursor position downwards.
 *
 * @return
 *     Status in IO_STATUS_BLOCK IoStatus.Status.
 *
 * @note
 *     Neither input or output buffers are used.
 **/
#define IOCTL_HDLSTERM_CLEAR_TO_END_DISPLAY \
    CTL_CODE(FILE_DEVICE_CONSOLE, 0x105, METHOD_NEITHER, FILE_WRITE_ACCESS)

/**
 * @name IOCTL_HDLSTERM_CLEAR_TO_END_LINE
 *     Clear the current terminal line from the current cursor position.
 *
 * @return
 *     Status in IO_STATUS_BLOCK IoStatus.Status.
 *
 * @note
 *     Neither input or output buffers are used.
 **/
#define IOCTL_HDLSTERM_CLEAR_TO_END_LINE \
    CTL_CODE(FILE_DEVICE_CONSOLE, 0x106, METHOD_NEITHER, FILE_WRITE_ACCESS)

/**
 * @name IOCTL_HDLSTERM_ATTRIBUTES_OFF
 *     Reset the current terminal text attributes to their default values.
 *
 * @return
 *     Status in IO_STATUS_BLOCK IoStatus.Status.
 *
 * @note
 *     Neither input or output buffers are used.
 **/
#define IOCTL_HDLSTERM_ATTRIBUTES_OFF \
    CTL_CODE(FILE_DEVICE_CONSOLE, 0x107, METHOD_NEITHER, FILE_WRITE_ACCESS)

/**
 * @name IOCTL_HDLSTERM_INVERSE_VIDEO
 *     Turn on inverse video mode.
 *
 * @return
 *     Status in IO_STATUS_BLOCK IoStatus.Status.
 *
 * @note
 *     Neither input or output buffers are used.
 **/
#define IOCTL_HDLSTERM_INVERSE_VIDEO \
    CTL_CODE(FILE_DEVICE_CONSOLE, 0x108, METHOD_NEITHER, FILE_WRITE_ACCESS)

/**
 * @name IOCTL_HDLSTERM_POSITION_CURSOR
 *     Change the current position of the cursor.
 *
 * @param[in]   PHEADLESS_CMD_POSITION_CURSOR CursorPos, sizeof(HEADLESS_CMD_POSITION_CURSOR)
 *     Pointer to a HEADLESS_CMD_POSITION_CURSOR buffer (pair of two ULONGs)
 *     that specify the new cursor column and row positions (0-based).
 *
 * @return
 *     Status in IO_STATUS_BLOCK IoStatus.Status.
 *
 * @note
 *     Output buffers are unused.
 **/
#define IOCTL_HDLSTERM_POSITION_CURSOR \
    CTL_CODE(FILE_DEVICE_CONSOLE, 0x109, METHOD_BUFFERED, FILE_WRITE_ACCESS)

/**
 * @name IOCTL_HDLSTERM_TERMINAL_POLL
 *     Poll the terminal to see whether data is current present.
 *
 * @param[out]  PULONG DataPresent, sizeof(ULONG)
 *     Pointer to boolean (in ULONG) that retrieves whether data
 *     is present (TRUE) or not (FALSE) in the terminal.
 *
 * @return
 *     Status in IO_STATUS_BLOCK IoStatus.Status.
 *     IO_STATUS_BLOCK IoStatus.Information is set to sizeof(ULONG).
 *
 * @note
 *     Input buffers are unused.
 **/
#define IOCTL_HDLSTERM_TERMINAL_POLL \
    CTL_CODE(FILE_DEVICE_CONSOLE, 0x10a, METHOD_BUFFERED, FILE_READ_ACCESS)

// #define IOCTL_HDLSTERM_PUT_STRING /
    // CTL_CODE(FILE_DEVICE_CONSOLE, 0x10b, METHOD_IN_DIRECT, FILE_WRITE_ACCESS)

/**
 * @name IOCTL_HDLSTERM_SEND_COMMAND
 *     Send a command to the Headless terminal and receives a response.
 *
 * @param[in]   PHDLSTERM_COMMAND HeadlessCommand
 *     Pointer to a data buffer, whose first member is a HEADLESS_CMD command.
 *     For a complete list of commands, see ntoskrnl/include/internal/hdl.h .
 *
 * @param[out]  PVOID
 *     Pointer to a suitably-sized buffer that receives response data.
 *
 * @return
 *     Status in IO_STATUS_BLOCK IoStatus.Status.
 **/
#define IOCTL_HDLSTERM_SEND_COMMAND \
    CTL_CODE(FILE_DEVICE_CONSOLE, 0x110, METHOD_NEITHER, FILE_ANY_ACCESS)

// #define IOCTL_HDLSTERM_SEND_COMMAND(Cmd) /
    // CTL_CODE(FILE_DEVICE_CONSOLE, (0x110 + (Cmd)), METHOD_NEITHER, FILE_ANY_ACCESS)

// #define IOCTL_HDLSTERM_READ_OUTPUT_CHARACTER    CTL_CODE(FILE_DEVICE_CONSOLE, 0x111, METHOD_OUT_DIRECT, FILE_READ_ACCESS)
// #define IOCTL_HDLSTERM_WRITE_OUTPUT_CHARACTER   CTL_CODE(FILE_DEVICE_CONSOLE, 0x112, METHOD_IN_DIRECT, FILE_WRITE_ACCESS)


//
// Control Structures
//

#if 0
typedef struct _CONSOLE_MODE
{
    ULONG dwMode;
} CONSOLE_MODE, *PCONSOLE_MODE;
#endif

typedef struct _HDLSTERM_COMMAND
{
    HEADLESS_CMD Command;
#if 0
    union
    {
        HEADLESS_CMD_ENABLE_TERMINAL;
        HEADLESS_CMD_SET_COLOR;
        HEADLESS_CMD_POSITION_CURSOR;
        HEADLESS_CMD_SET_BLUE_SCREEN_DATA;
        // ...
    } Data;
#else
    UCHAR Data[1];
#endif
} HDLSTERM_COMMAND, *PHDLSTERM_COMMAND;

#ifdef __cplusplus
}
#endif

#endif /* _NTDDHDLS_H_ */
