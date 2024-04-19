/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARC path dissector - For BIOS drives only
 * COPYRIGHT:   Copyright 2001 Eric Kohl <eric.kohl@reactos.org>
 *              Copyright 2010 Hervé Poussineau  <hpoussin@reactos.org>
 */

#pragma once

BOOLEAN
DissectArcPath(
    _In_ PCSTR ArcPath,
    _Out_opt_ PCSTR* Path,
    _Out_ PUCHAR DriveNumber,
    _Out_ PULONG PartitionNumber);

BOOLEAN
DissectArcPath2(
    _In_ PCSTR ArcPath,
    _Out_ PULONG x,
    _Out_ PULONG y,
    _Out_ PULONG z,
    _Out_ PULONG Partition,
    _Out_ PULONG PathSyntax);


/* pathmap.c -- FIXME: Move to its own header */

BOOLEAN
DriveStrToNumber(
    _In_ PCSTR DriveString,
    _Out_opt_ PCSTR* Trailing,
    _Out_ PUCHAR DriveType,
    _Out_ PULONG DriveNumber,
    _Out_opt_ PULONG PartitionNumber,
    _Out_opt_ PULONG HwDriveNumber);

BOOLEAN
ExpandPath(
    _Inout_ PSTR PathBuffer,
    _In_ ULONG Size);
