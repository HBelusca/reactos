/*
 * COPYRIGHT:         See COPYING in the top level directory
 * PROJECT:           ReactOS kernel
 * PURPOSE:           Functions for creation and destruction of DCs
 * FILE:              win32ss/gdi/ntgdi/device.c
 * PROGRAMER:         Timo Kreuzer (timo.kreuzer@rectos.org)
 */

#include <win32k.h>

// #define NDEBUG
#include <debug.h>

PDC defaultDCstate = NULL;

BOOL FASTCALL
IntCreatePrimarySurface(VOID)
{
    /* Create surface */
    if (!PDEVOBJ_pSurface(gpmdev->ppdevGlobal))
        return FALSE;

    DPRINT("IntCreatePrimarySurface() ppdevGlobal = 0x%p, ppdevGlobal->pSurface = 0x%p\n",
        gpmdev->ppdevGlobal, gpmdev->ppdevGlobal->pSurface);

    /* Init Primary Display Device Capabilities */
    PDEVOBJ_vGetDeviceCaps(gpmdev->ppdevGlobal, &GdiHandleTable->DevCaps);

    return TRUE;
}

VOID FASTCALL
IntDestroyPrimarySurface(VOID)
{
    ASSERT(gpmdev);

    DPRINT("IntDestroyPrimarySurface() ppdevGlobal = 0x%p, ppdevGlobal->pSurface = 0x%p\n",
        gpmdev->ppdevGlobal, gpmdev->ppdevGlobal->pSurface);

    ASSERT(gpmdev->ppdevGlobal->pSurface);
    SURFACE_ShareUnlockSurface(gpmdev->ppdevGlobal->pSurface);
    gpmdev->ppdevGlobal->pSurface = NULL;

    UNIMPLEMENTED;
}

PPDEVOBJ FASTCALL
IntEnumHDev(VOID)
{
// I guess we will soon have more than one primary surface.
// This will do for now.
    return gpmdev->ppdevGlobal;
}


INT
APIENTRY
NtGdiDrawEscape(
    IN HDC hdc,
    IN INT iEsc,
    IN INT cjIn,
    IN OPTIONAL LPSTR pjIn)
{
    UNIMPLEMENTED;
    return 0;
}


