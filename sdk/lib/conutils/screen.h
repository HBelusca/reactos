/*
 * PROJECT:     ReactOS Console Utilities Library
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Console/terminal screen management.
 * COPYRIGHT:   Copyright 2017-2018 ReactOS Team
 *              Copyright 2017-2018 Hermes Belusca-Maito
 */

/**
 * @file    screen.h
 * @ingroup ConUtils
 *
 * @brief   Console/terminal screen management.
 **/

#ifndef __SCREEN_H__
#define __SCREEN_H__

#pragma once

#ifndef _UNICODE
#error The ConUtils library at the moment only supports compilation with _UNICODE defined!
#endif

#ifdef __cplusplus
extern "C" {
#endif


#if 0

VOID
ConClearLine(IN PCON_STREAM Stream);

#endif



#include <wincon.h>

typedef struct _CON_SCREEN
{
    PCON_STREAM Stream; // Out
    // PCON_STREAM In;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    CONSOLE_CURSOR_INFO cci;
} CON_SCREEN, *PCON_SCREEN;

#define INIT_CON_SCREEN(pStream)    {(pStream)} /* {(pStream), {{}}, {{}}} */

#define InitializeConScreen(pScreen, pStream)   \
do { \
    (pScreen)->Stream = (pStream);  \
    RtlZeroMemory(&(pScreen)->csbi, sizeof((pScreen)->csbi));   \
    RtlZeroMemory(&(pScreen)->cci , sizeof((pScreen)->cci ));   \
} while (0)


#define CON_SCREEN_SBSIZE       1
#define CON_SCREEN_CURSORPOS    2
#define CON_SCREEN_WINDOWSIZE   4

BOOL
ConGetScreenInfo(
    IN PCON_SCREEN Screen,
    OUT PCONSOLE_SCREEN_BUFFER_INFO pcsbi,
    IN DWORD dwFlags);

VOID
ConClearScreen(IN PCON_SCREEN Screen);

extern const UCHAR CGA_TO_ANSI_COLOR_TABLE[16];

#define CGA_TO_ANSI_COLOR(CgaColor) \
    CGA_TO_ANSI_COLOR_TABLE[CgaColor & 0x0F]

BOOL
ConSetScreenColor(
    IN PCON_SCREEN Screen,
    IN WORD wColor,
    IN BOOL bFill);

#ifdef __cplusplus
}
#endif

#endif  /* __SCREEN_H__ */

/* EOF */
