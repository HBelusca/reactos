/*
 * PROJECT:     ReactOS Console Text-Mode Device Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Main Header File.
 * COPYRIGHT:   Copyright 1999 Boudewijn Dekker
 *              Copyright 1999-2019 Eric Kohl
 */

#ifndef _BLUE_PCH_
#define _BLUE_PCH_

#pragma once

#include <ntifs.h>

#define TAG_BLUE    'EULB'

#define TAB_WIDTH   8
#define MAX_PATH    260

typedef struct _SECURITY_ATTRIBUTES SECURITY_ATTRIBUTES, *PSECURITY_ATTRIBUTES;

// Define material that normally comes from PSDK
// This is mandatory to prevent any inclusion of
// user-mode stuff.
typedef struct tagCOORD
{
    SHORT X;
    SHORT Y;
} COORD, *PCOORD;

typedef struct tagSMALL_RECT
{
    SHORT Left;
    SHORT Top;
    SHORT Right;
    SHORT Bottom;
} SMALL_RECT;

typedef struct tagCONSOLE_SCREEN_BUFFER_INFO
{
    COORD      dwSize;
    COORD      dwCursorPosition;
    USHORT     wAttributes;
    SMALL_RECT srWindow;
    COORD      dwMaximumWindowSize;
} CONSOLE_SCREEN_BUFFER_INFO, *PCONSOLE_SCREEN_BUFFER_INFO;

typedef struct tagCONSOLE_CURSOR_INFO
{
    ULONG dwSize;
    LOGICAL bVisible; // BOOL
} CONSOLE_CURSOR_INFO, *PCONSOLE_CURSOR_INFO;

#define ENABLE_PROCESSED_OUTPUT                 0x0001
#define ENABLE_WRAP_AT_EOL_OUTPUT               0x0002

#include <blue/ntddblue.h>

/** From include/psdk/wingdi.h and bootvid/precomp.h **/
typedef struct tagRGBQUAD
{
    UCHAR rgbBlue;
    UCHAR rgbGreen;
    UCHAR rgbRed;
    UCHAR rgbReserved;
} RGBQUAD, *PRGBQUAD;

#define RGB(r, g, b)    ((RGBQUAD)(((UCHAR)(b) | ((USHORT)((UCHAR)(g))<<8)) | (((ULONG)(UCHAR)(r))<<16)))

/*
 * Color attributes for text and screen background
 */
#define FOREGROUND_BLUE                 0x0001
#define FOREGROUND_GREEN                0x0002
#define FOREGROUND_RED                  0x0004
#define FOREGROUND_INTENSITY            0x0008
#define BACKGROUND_BLUE                 0x0010
#define BACKGROUND_GREEN                0x0020
#define BACKGROUND_RED                  0x0040
#define BACKGROUND_INTENSITY            0x0080

VOID ScrSetFont(_In_ PUCHAR FontBitfield);

#endif /* _BLUE_PCH_ */
