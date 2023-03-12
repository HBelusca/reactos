/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Public subset of Kernel Debugger functions (from kdb.h)
 * COPYRIGHT:   Copyright 2023 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#pragma once

/* FUNCTIONS *****************************************************************/

/* from kdb_cli.c */

BOOLEAN
NTAPI
KdbRegisterCliCallback(
    PVOID Callback,
    BOOLEAN Deregister);

VOID
KdbpPrint(
    _In_ PSTR Format,
    _In_ ...);

VOID
KdbpPrintUnicodeString(
    _In_ PCUNICODE_STRING String);

BOOLEAN
NTAPI
KdbpGetHexNumber(
    IN PCHAR pszNum,
    OUT ULONG_PTR *pulValue);

/* from kdb_symbols.c */

BOOLEAN
KdbSymPrintAddress(
    IN PVOID Address,
    IN PCONTEXT Context);

/* EOF */
