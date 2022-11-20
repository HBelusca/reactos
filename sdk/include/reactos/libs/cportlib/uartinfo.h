/*
 * PROJECT:     ReactOS ComPort Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Default UART Settings
 * COPYRIGHT:   Copyright 2022 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#pragma once

/* Serial debug connection */
#if defined(SARCH_PC98)
// #define DEFAULT_DEBUG_PORT      2 /* COM2 */
// #define DEFAULT_DEBUG_COM1_IRQ  4
// #define DEFAULT_DEBUG_COM2_IRQ  5
// #define DEFAULT_DEBUG_BAUD_RATE 9600
#define DEFAULT_BAUD_RATE       9600
#else
// #define DEFAULT_DEBUG_PORT      2 /* COM2 */
// #define DEFAULT_DEBUG_COM1_IRQ  4
// #define DEFAULT_DEBUG_COM2_IRQ  3
// #define DEFAULT_DEBUG_BAUD_RATE 115200
#define DEFAULT_BAUD_RATE       19200
#endif


#if defined(_M_IX86) || defined(_M_AMD64)
#if defined(SARCH_PC98)
const ULONG BaseArray[] = {0, 0x30, 0x238};
#else
const ULONG BaseArray[] = {0, 0x3F8, 0x2F8, 0x3E8, 0x2E8};
#endif
#elif defined(_M_PPC)
const ULONG BaseArray[] = {0, 0x800003F8};
#elif defined(_M_MIPS)
const ULONG BaseArray[] = {0, 0x80006000, 0x80007000};
#elif defined(_M_ARM)
const ULONG BaseArray[] = {0, 0xF1012000};
#else
#error Unknown architecture
#endif

#define MAX_COM_PORTS   (sizeof(BaseArray) / sizeof(BaseArray[0]) - 1)

/* EOF */
