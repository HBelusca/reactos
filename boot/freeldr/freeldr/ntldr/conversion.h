/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Windows-compatible NT OS Loader.
 * COPYRIGHT:   Copyright 2006-2019 Aleksey Bragin <aleksey@reactos.org>
 */

#pragma once

PVOID VaToPa(PVOID Va);
PVOID PaToVa(PVOID Pa);

VOID List_PaToVa(_In_ PLIST_ENTRY ListHeadPa);
VOID ConvertConfigToVA(PCONFIGURATION_COMPONENT_DATA Start);

/* EOF */
