/*
 * PROJECT:     NT / ReactOS Headless Terminal Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Main Header File.
 * COPYRIGHT:   Copyright 2020 Hermes Belusca-Maito
 */

#ifndef _HDLSTERM_PCH_
#define _HDLSTERM_PCH_

#include <ntifs.h>

#define TAG_BLUE    'EULB'

#define TAB_WIDTH   8
#define MAX_PATH    260

typedef struct _SECURITY_ATTRIBUTES SECURITY_ATTRIBUTES, *PSECURITY_ATTRIBUTES;

// Define material that normally comes from PSDK
// This is mandatory to prevent any inclusion of
// user-mode stuff.
typedef struct _COORD
{
    SHORT X;
    SHORT Y;
} COORD, *PCOORD;

typedef struct _SMALL_RECT
{
    SHORT Left;
    SHORT Top;
    SHORT Right;
    SHORT Bottom;
} SMALL_RECT;

// #include <ntcondd/ntddblue.h>
// #include <ntoskrnl/include/internal/hdl.h>

#endif /* _HDLSTERM_PCH_ */
