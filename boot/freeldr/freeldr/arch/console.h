/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 *              or MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Emulated console implementation
 * COPYRIGHT:   Copyright 2025 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#pragma once

INT
printf(
    _In_ PCSTR Format, ...);

BOOLEAN
ConsInitialize(
    _In_ ULONG InitialCursorX,
    _In_ ULONG InitialCursorY,
    _In_ UCHAR InitialAttributes);

VOID
ConsClearScreen(
    _In_ UCHAR Attr);

VOID
ConsWriteChar(
    _In_ UCHAR Char);

VOID
ConsWriteString(
    _In_ _When_(Count == -1, _Pre_z_) PCCH String,
    _In_ ULONG Count);

VOID
ConsScrollUp(
    _In_ UCHAR Attr);

VOID
ConsSetCursorPosition(
    _In_ ULONG X,
    _In_ ULONG Y);


BOOLEAN
ConsKbHit(VOID);

UINT
ConsGetCh(VOID);
