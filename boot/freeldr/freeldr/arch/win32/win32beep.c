/*
 * PROJECT:     FreeLoader Win32 Emulation
 * LICENSE:     Dual-licensed:
 *              GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 *              MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Beep routine
 * COPYRIGHT:   Copyright 2024-2026 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#include <freeldr.h>

VOID
Win32Beep(VOID)
{
    Beep(700, 100);
}
