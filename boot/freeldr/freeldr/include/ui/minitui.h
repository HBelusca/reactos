/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     "Mini" Text UI interface
 * COPYRIGHT:   Copyright 2007 Hervé Poussineau <hpoussin@reactos.org>
 */

#pragma once

/* Textual User Interface Functions ******************************************/
/* Menu Functions ************************************************************/

VOID
MiniTuiDrawMenu(
    _In_ PUI_MENU_INFO MenuInfo);

extern const UIVTBL MiniTuiVtbl;

/* EOF */
