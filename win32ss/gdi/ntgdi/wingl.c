/*
 * PROJECT:         ReactOS win32 kernel mode subsystem
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            win32ss/gdi/ntgdi/wingl.c
 * PURPOSE:         WinGL API
 * PROGRAMMER:
 */

#include <win32k.h>

#define NDEBUG
#include <debug.h>

static
INT
FASTCALL
IntGetipfdDevMax(PDC pdc)
{
    INT Ret = 0;
    PPDEVOBJ ppdev = pdc->ppdev;

    if (pdc->ipfdDevMax)
        return pdc->ipfdDevMax;

    if (ppdev->flFlags & PDEV_META_DEVICE)
    {
        return 0;
    }

    if (ppdev->DriverFunctions.DescribePixelFormat)
    {
        Ret = ppdev->DriverFunctions.DescribePixelFormat(
                                                ppdev->dhpdev,
                                                1,
                                                0,
                                                NULL);
    }

    if (Ret)
        pdc->ipfdDevMax = Ret;

    return Ret;
}

_Success_(return != 0)
__kernel_entry
INT
APIENTRY
NtGdiDescribePixelFormat(
    _In_ HDC hdc,
    _In_ INT ipfd,
    _In_ UINT cjpfd,
    _Out_writes_bytes_(cjpfd) PPIXELFORMATDESCRIPTOR ppfd)
{
    PDC pdc;
    PPDEVOBJ ppdev;
    INT Ret = 0;
    PIXELFORMATDESCRIPTOR pfdSafe;

    if ((ppfd == NULL) && (cjpfd != 0)) return 0;

    pdc = DC_LockDc(hdc);
    if (!pdc)
    {
        EngSetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }

    if (!pdc->ipfdDevMax)
        IntGetipfdDevMax(pdc);
    if (!pdc->ipfdDevMax)
    {
        /* EngSetLastError ? */
        goto Exit;
    }

    if (!ppfd)
    {
        Ret = pdc->ipfdDevMax;
        goto Exit;
    }

    if ((ipfd < 1) || (ipfd > pdc->ipfdDevMax))
    {
        EngSetLastError(ERROR_INVALID_PARAMETER);
        goto Exit;
    }

    ppdev = pdc->ppdev;

    if (ppdev->flFlags & PDEV_META_DEVICE)
    {
        UNIMPLEMENTED;
        goto Exit;
    }

    if (ppdev->DriverFunctions.DescribePixelFormat)
    {
        Ret = ppdev->DriverFunctions.DescribePixelFormat(
                                                    ppdev->dhpdev,
                                                    ipfd,
                                                    sizeof(pfdSafe),
                                                    &pfdSafe);
    }

    if (Ret && cjpfd)
    {
        _SEH2_TRY
        {
            cjpfd = min(cjpfd, sizeof(PIXELFORMATDESCRIPTOR));
            ProbeForWrite(ppfd, cjpfd, 1);
            RtlCopyMemory(ppfd, &pfdSafe, cjpfd);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            SetLastNtError(_SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

Exit:
    DC_UnlockDc(pdc);
    return Ret;
}

BOOL
APIENTRY
NtGdiSetPixelFormat(
    _In_ HDC hdc,
    _In_ INT ipfd)
{
    PDC pdc;
    PPDEVOBJ ppdev;
    HWND hWnd;
    PEWNDOBJ pWndObj;
    BOOL Ret = FALSE;

    DPRINT1("Setting pixel format from win32k!\n");

    pdc = DC_LockDc(hdc);
    if (!pdc)
    {
        EngSetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    /* A DC surface is required */
    if (!pdc->dclevel.pSurface)
    {
        EngSetLastError(ERROR_INVALID_HANDLE);
        goto Exit;
    }

    if (!pdc->ipfdDevMax)
        IntGetipfdDevMax(pdc);

    if ((ipfd < 1) || (ipfd > pdc->ipfdDevMax))
    {
        EngSetLastError(ERROR_INVALID_PARAMETER);
        goto Exit;
    }

    if (pdc->fs & DC_DISPLAY)
    {
        ASSERT(pdc->dcattr & DCTYPE_DIRECT);

        UserEnterExclusive();
        hWnd = UserGethWnd(hdc, &pWndObj);
        UserLeave();

        if (!hWnd)
        {
            EngSetLastError(ERROR_INVALID_WINDOW_STYLE);
            goto Exit;
        }

        if (pWndObj && (pWndObj->PixelFormat != ipfd))
        {
            ERR("Window object %p has different pixel format!\n", pWndObj);
            EngSetLastError(ERROR_INVALID_PIXEL_FORMAT);
            goto Exit;
        }
    }
    else
    {
        hWnd = NULL;
    }

    ppdev = pdc->ppdev;

    if (ppdev->flFlags & PDEV_META_DEVICE)
    {
        UNIMPLEMENTED;
        goto Exit;
    }

    if (ppdev->DriverFunctions.SetPixelFormat)
    {
        Ret = ppdev->DriverFunctions.SetPixelFormat(&pdc->dclevel.pSurface->SurfObj,
                                                    ipfd, hWnd);
    }

Exit:
    DC_UnlockDc(pdc);
    return Ret;
}

BOOL
APIENTRY
NtGdiSwapBuffers(
    _In_ HDC hdc)
{
    PDC pdc;
    PPDEVOBJ ppdev;
    HWND hWnd;
    PEWNDOBJ pWndObj;
    BOOL Ret = FALSE;

    pdc = DC_LockDc(hdc);
    if (!pdc)
    {
        EngSetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    /* A DC surface is required */
    if (!pdc->dclevel.pSurface)
    {
        EngSetLastError(ERROR_INVALID_HANDLE);
        goto Exit;
    }

    /* Do nothing if this is not a display DC */
    if (!(pdc->fs & DC_DISPLAY))
    {
        Ret = TRUE;
        goto Exit;
    }
    ASSERT(pdc->dcattr & DCTYPE_DIRECT);

    UserEnterExclusive();
    hWnd = UserGethWnd(hdc, &pWndObj);
    UserLeave();

    if (!hWnd)
    {
        EngSetLastError(ERROR_INVALID_WINDOW_STYLE);
        goto Exit;
    }

    /* pWndObj is needed so exit on NULL pointer */
    if (!pWndObj)
    {
        EngSetLastError(ERROR_INVALID_HANDLE);
        goto Exit;
    }

    ppdev = pdc->ppdev;

    if (ppdev->flFlags & PDEV_META_DEVICE)
    {
        UNIMPLEMENTED;
        goto Exit;
    }

    if (ppdev->DriverFunctions.SwapBuffers)
    {
        Ret = ppdev->DriverFunctions.SwapBuffers(&pdc->dclevel.pSurface->SurfObj,
                                                 pWndObj);
    }

Exit:
    DC_UnlockDc(pdc);
    return Ret;
}

/* EOF */
