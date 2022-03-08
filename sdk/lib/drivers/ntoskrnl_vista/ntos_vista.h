/*
 * PROJECT:     ReactOS Kernel - Vista+ APIs
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Main Kernel Header
 * COPYRIGHT:   2016 Pierre Schweitzer (pierre@reactos.org)
 *              2020 Victor Perevertkin (victor.perevertkin@reactos.org)
 */

#ifndef _NTOS_VISTA_PCH
#define _NTOS_VISTA_PCH

#include <ntdef.h>

#define _KPCR _KEPCR
#define KPCR  KEPCR
#define PKPCR PKEPCR

#include <ntifs.h>

#undef _KPCR
#undef KPCR
#undef PKPCR

#endif /* _NTOS_VISTA_PCH */
