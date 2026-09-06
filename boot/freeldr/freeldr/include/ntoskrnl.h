/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     NTOS glue routines for the MINIHAL library
 * COPYRIGHT:   Copyright 2010 Hervé Poussineau <hpoussin@reactos.org>
 */

#include <ntdef.h>
#undef _NTHAL_
#undef NTSYSAPI
#define NTSYSAPI

/* Windows Device Driver Kit */
#include <ntddk.h>
#include <ndk/haltypes.h>

/* Disk stuff */
#include <arc/arc.h>
#include <ntdddisk.h>
#include <../../ntoskrnl/include/internal/hal.h>
