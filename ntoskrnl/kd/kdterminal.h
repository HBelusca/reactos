/*
 * PROJECT:     ReactOS KDBG Kernel Debugger Terminal Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     KD Terminal Driver Interface public header
 * COPYRIGHT:   Copyright 2023 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#pragma once

#define KEY_BS          8
#define KEY_ESC         27
#define KEY_DEL         127

#define KEY_SCAN_UP     72
#define KEY_SCAN_DOWN   80

/* Scan codes of keyboard keys */
#define KEYSC_END       0x004f
#define KEYSC_PAGEUP    0x0049
#define KEYSC_PAGEDOWN  0x0051
#define KEYSC_HOME      0x0047
#define KEYSC_ARROWUP   0x0048  // == KEY_SCAN_UP


typedef struct _SIZE
{
    LONG cx;
    LONG cy;
} SIZE, *PSIZE;

/* KD Controlling Terminal */
typedef struct _KD_TERMINAL
{
    /*COORD*/ SIZE Size;
    BOOLEAN Connected;
    union
    {
        UCHAR Flags;
        struct
        {
            UCHAR Serial : 1;
            UCHAR ReportsSize : 1;
            UCHAR NoEcho : 1;       // KDNOECHO
            UCHAR SerialInput : 1;  // KDSERIAL
        };
    };

    VOID
    (NTAPI *SetState)(
        _In_ BOOLEAN Enable);

    BOOLEAN
    (NTAPI *UpdateSize)(VOID);

    CHAR
    (NTAPI *ReadKey)(
        _Out_ PULONG ScanCode);

} KD_TERMINAL, *PKD_TERMINAL;

// #ifdef _KDTERMINAL_
extern KD_TERMINAL KdTerminal;
#define KD_TERM KdTerminal
// #else
// __CREATE_NTOS_DATA_IMPORT_ALIAS(KdTerminal)
// extern PKD_TERMINAL KdTerminal;
// #define KD_TERM (*KdTerminal)
// #endif

/* EOF */
