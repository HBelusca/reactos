/*
 * PROJECT:     NT / ReactOS Console Display Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     NTOS Inbv / Bootvid Support.
 * COPYRIGHT:   Copyright 2020 Hermes Belusca-Maito
 */

/* INCLUDES ******************************************************************/

#include "ntcondd.h"
#include <ndk/inbvfuncs.h>

#define NDEBUG
#include <debug.h>

/* TYPEDEFS ******************************************************************/

// TODO: Global structure representing the unique INBV support.

/* INBV MANAGEMENT FUNCTIONS *************************************************/

static BOOLEAN
ScrResetScreen(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ BOOLEAN FullReset,
    _In_ BOOLEAN Enable);

static PDEVICE_EXTENSION ResetDisplayParametersDeviceExtension = NULL;
static HANDLE InbvThreadHandle = NULL;
static BOOLEAN InbvMonitoring = FALSE;

/*
 * Reinitialize the display to base VGA mode.
 *
 * Returns TRUE if it completely resets the adapter to the given character mode.
 * Returns FALSE otherwise, indicating that the HAL should perform the VGA mode
 * reset itself after HwVidResetHw() returns control.
 *
 * This callback has been registered with InbvNotifyDisplayOwnershipLost()
 * and is called by InbvAcquireDisplayOwnership(), typically when the bugcheck
 * code regains display access. Therefore this routine can be called at any
 * IRQL, and in particular at IRQL = HIGH_LEVEL. This routine must also reside
 * completely in non-paged pool, and cannot perform the following actions:
 * Allocate memory, access pageable memory, use any synchronization mechanisms
 * or call any routine that must execute at IRQL = DISPATCH_LEVEL or below.
 */
static BOOLEAN
NTAPI
ScrResetDisplayParametersEx(
    _In_ ULONG Columns,
    _In_ ULONG Rows,
    _In_ BOOLEAN CalledByInbv)
{
    PDEVICE_EXTENSION DeviceExtension;

    /* Bail out early if we don't have any resettable adapter */
    if (!ResetDisplayParametersDeviceExtension)
        return FALSE; // No adapter found: request HAL to perform a full reset.

    /*
     * If we have been unexpectedly called via a callback from
     * InbvAcquireDisplayOwnership(), start monitoring INBV.
     */
    if (CalledByInbv)
        InbvMonitoring = TRUE;

    DeviceExtension = ResetDisplayParametersDeviceExtension;
    ASSERT(DeviceExtension);

    /* Disable the screen but don't reset all screen settings (OK at high IRQL) */
    return ScrResetScreen(DeviceExtension, FALSE, FALSE);
}

/* This callback is registered with InbvNotifyDisplayOwnershipLost() */
static BOOLEAN
NTAPI
ScrResetDisplayParameters(
    _In_ ULONG Columns,
    _In_ ULONG Rows)
{
    /* Call the extended function, specifying we were called by INBV */
    return ScrResetDisplayParametersEx(Columns, Rows, TRUE);
}

/*
 * (Adapted for ReactOS/Win2k3 from an original comment
 *  by Gé van Geldorp, June 2003, r4937)
 *
 * DISPLAY OWNERSHIP
 *
 * So, who owns the physical display and is allowed to write to it?
 *
 * In NT 5.x (Win2k/Win2k3), upon boot INBV/BootVid owns the display, unless
 * /NOGUIBOOT has been specified in the boot command line. Later in the boot
 * sequence, WIN32K.SYS opens the DISPLAY device. This open call ends up in
 * VIDEOPRT.SYS. This component takes ownership of the display by calling
 * InbvNotifyDisplayOwnershipLost() -- effectively telling INBV to release
 * ownership of the display it previously had. From that moment on, the display
 * is owned by that component and can be switched to graphics mode. The display
 * is not supposed to return to text mode, except in case of a bugcheck.
 * The bugcheck code calls InbvAcquireDisplayOwnership() so as to make INBV
 * re-take display ownership, and calls back the function previously registered
 * by VIDEOPRT.SYS with InbvNotifyDisplayOwnershipLost(). After the bugcheck,
 * execution is halted. So, under NT, the only possible sequence of display
 * modes is text mode -> graphics mode -> text mode (the latter hopefully
 * happening very infrequently).
 *
 * In ReactOS things are a little bit different. We want to have a functional
 * interactive text mode. We should be able to switch back and forth from
 * text mode to graphics mode when a GUI app is started and then finished.
 * Also, when the system bugchecks in graphics mode we want to switch back to
 * text mode and show the bugcheck information. Last but not least, when using
 * KDBG in /DEBUGPORT=SCREEN mode, breaking into the debugger would trigger a
 * switch to text mode, and the user would expect that by continuing execution
 * a switch back to graphics mode is done.
 */
static VOID
NTAPI
InbvMonitorThread(
    _In_ PVOID Context)
{
    LARGE_INTEGER Delay;
    USHORT i;

    KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY);

    while (TRUE)
    {
        /*
         * During one second, check the INBV status each 100 milliseconds,
         * then revert to 1 second delay.
         */
        i = 10;
        Delay.QuadPart = (LONGLONG)-100*1000*10; // 100 millisecond delay
        while (!InbvMonitoring)
        {
            KeDelayExecutionThread(KernelMode, FALSE, &Delay);

            if ((i > 0) && (--i == 0))
                Delay.QuadPart = (LONGLONG)-1*1000*1000*10; // 1 second delay
        }

        /*
         * Loop while the display is owned by INBV. We cannot do anything else
         * than polling since INBV does not offer a proper notification system.
         *
         * During one second, check the INBV status each 100 milliseconds,
         * then revert to 1 second delay.
         */
        i = 10;
        Delay.QuadPart = (LONGLONG)-100*1000*10; // 100 millisecond delay
        while (InbvCheckDisplayOwnership())
        {
            KeDelayExecutionThread(KernelMode, FALSE, &Delay);

            if ((i > 0) && (--i == 0))
                Delay.QuadPart = (LONGLONG)-1*1000*1000*10; // 1 second delay
        }

        /* Reset the monitoring */
        InbvMonitoring = FALSE;

        /*
         * Somebody released INBV display ownership, usually by invoking
         * InbvNotifyDisplayOwnershipLost(). However the caller of this
         * function certainly specified a different callback than ours.
         * As we are going to be the only owner of the active display,
         * we need to re-register our own display reset callback.
         */
        InbvNotifyDisplayOwnershipLost(ScrResetDisplayParameters);

        /* Re-enable the screen, keeping the original screen settings */
        if (ResetDisplayParametersDeviceExtension)
            ScrResetScreen(ResetDisplayParametersDeviceExtension, FALSE, TRUE);
    }

    // FIXME: See ScrInbvCleanup().
    // PsTerminateSystemThread(STATUS_SUCCESS);
}

static NTSTATUS
ScrInbvInitialize(VOID)
{
    /* Create the INBV monitoring thread if needed */
    if (!InbvThreadHandle)
    {
        NTSTATUS Status;
        OBJECT_ATTRIBUTES ObjectAttributes = RTL_CONSTANT_OBJECT_ATTRIBUTES(NULL, OBJ_KERNEL_HANDLE);

        Status = PsCreateSystemThread(&InbvThreadHandle,
                                      0,
                                      &ObjectAttributes,
                                      NULL,
                                      NULL,
                                      InbvMonitorThread,
                                      NULL);
        if (!NT_SUCCESS(Status))
            InbvThreadHandle = NULL;
    }

    /* Re-register the display reset callback with INBV */
    InbvNotifyDisplayOwnershipLost(ScrResetDisplayParameters);

    return STATUS_SUCCESS;
}

static NTSTATUS
ScrInbvCleanup(VOID)
{
    // HANDLE ThreadHandle;

    // ResetDisplayParametersDeviceExtension = NULL;
    if (ResetDisplayParametersDeviceExtension)
    {
        InbvNotifyDisplayOwnershipLost(NULL);
        ScrResetDisplayParametersEx(80, 50, FALSE);
        // or InbvAcquireDisplayOwnership(); ?
    }

#if 0
    // TODO: Find the best way to communicate the request.
    /* Signal the INBV monitoring thread and wait for it to terminate */
    ThreadHandle = InterlockedExchangePointer((PVOID*)&InbvThreadHandle, NULL);
    if (ThreadHandle)
    {
        ZwWaitForSingleObject(ThreadHandle, Executive, KernelMode, FALSE, NULL);
        /* Close its handle */
        ObCloseHandle(ThreadHandle, KernelMode);
    }
#endif

    return STATUS_SUCCESS;
}

/* FUNCTIONS *****************************************************************/

static BOOLEAN
ScrResetScreen(
    _In_ PDEVICE_EXTENSION DeviceExtension,
    _In_ BOOLEAN FullReset,
    _In_ BOOLEAN Enable)
{
#define FOREGROUND_LIGHTGRAY (FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED)

    PHYSICAL_ADDRESS BaseAddress;

    /* Allow resets to the same state only for full resets */
    if (!FullReset && (Enable == DeviceExtension->Enabled))
        return FALSE; // STATUS_INVALID_PARAMETER; STATUS_INVALID_DEVICE_REQUEST;

    if (FullReset)
    {
        DeviceExtension->CursorSize    = 5; /* FIXME: value correct?? */
        DeviceExtension->CursorVisible = TRUE;

        if (DeviceExtension->FontBitfield)
        {
            ExFreePoolWithTag(DeviceExtension->FontBitfield, TAG_BLUE);
            DeviceExtension->FontBitfield = NULL;
        }

        /* More initialization */
        DeviceExtension->CharAttribute = BACKGROUND_BLUE | FOREGROUND_LIGHTGRAY;
        DeviceExtension->Mode = ENABLE_PROCESSED_OUTPUT |
                                ENABLE_WRAP_AT_EOL_OUTPUT;
    }

    if (Enable)
    {
        ScrAcquireOwnership(DeviceExtension);

        if (FullReset)
        {
            /*
             * Fully reset the screen and all its settings.
             */

            /* Unmap any previously mapped video memory */
            if (DeviceExtension->VideoMemory)
            {
                ASSERT(DeviceExtension->VideoMemorySize != 0);
                MmUnmapIoSpace(DeviceExtension->VideoMemory, DeviceExtension->VideoMemorySize);
            }
            DeviceExtension->VideoMemory = NULL;
            DeviceExtension->VideoMemorySize = 0;

            /* Free any previously allocated backup screenbuffer */
            if (DeviceExtension->ScreenBuffer)
            {
                ASSERT(DeviceExtension->ScreenBufferSize != 0);
                ExFreePoolWithTag(DeviceExtension->ScreenBuffer, TAG_BLUE);
            }
            DeviceExtension->ScreenBuffer = NULL;
            DeviceExtension->ScreenBufferSize = 0;

            /* Get a pointer to the video memory */
            DeviceExtension->VideoMemorySize = DeviceExtension->Rows * DeviceExtension->Columns * 2;
            if (DeviceExtension->VideoMemorySize == 0)
                return FALSE; // STATUS_INVALID_VIEW_SIZE; STATUS_MAPPED_FILE_SIZE_ZERO;

            /* Map the video memory */
            BaseAddress.QuadPart = VIDMEM_BASE;
            DeviceExtension->VideoMemory =
                (PUCHAR)MmMapIoSpace(BaseAddress, DeviceExtension->VideoMemorySize, MmNonCached);
            if (!DeviceExtension->VideoMemory)
            {
                DeviceExtension->VideoMemorySize = 0;
                return FALSE; // STATUS_NONE_MAPPED; STATUS_NOT_MAPPED_VIEW; STATUS_CONFLICTING_ADDRESSES;
            }

            /* Initialize the backup screenbuffer in non-paged pool (must be accessible at high IRQL) */
            DeviceExtension->ScreenBufferSize = DeviceExtension->VideoMemorySize;
            DeviceExtension->ScreenBuffer =
                (PUCHAR)ExAllocatePoolWithTag(NonPagedPool, DeviceExtension->ScreenBufferSize, TAG_BLUE);
            if (!DeviceExtension->ScreenBuffer)
            {
                DPRINT1("Could not allocate screenbuffer, ignore...\n");
                DeviceExtension->ScreenBufferSize = 0;
            }

            /* (Re-)initialize INBV */
            ScrInbvInitialize();
        }
        else
        {
            /*
             * Restore the previously disabled screen.
             */

            /* Restore the snapshot of the video memory from the backup screenbuffer */
            if (DeviceExtension->ScreenBuffer)
            {
                ASSERT(DeviceExtension->VideoMemory);
                ASSERT(DeviceExtension->ScreenBuffer);
                ASSERT(DeviceExtension->ScreenBufferSize != 0);
                ASSERT(DeviceExtension->VideoMemorySize == DeviceExtension->ScreenBufferSize);

                RtlCopyMemory(DeviceExtension->VideoMemory,
                              DeviceExtension->ScreenBuffer,
                              DeviceExtension->VideoMemorySize);
            }

            /* Restore the cursor state */
            ScrSetCursor(DeviceExtension);
            ScrSetCursorShape(DeviceExtension);
        }
        DeviceExtension->Enabled = TRUE;
    }
    else
    {
        DeviceExtension->Enabled = FALSE;
        if (FullReset)
        {
            /*
             * Fully disable the screen and reset all its settings.
             */

            /* Clean INBV up */
            ScrInbvCleanup();

            /* Unmap any previously mapped video memory */
            if (DeviceExtension->VideoMemory)
            {
                ASSERT(DeviceExtension->VideoMemorySize != 0);
                MmUnmapIoSpace(DeviceExtension->VideoMemory, DeviceExtension->VideoMemorySize);
            }
            DeviceExtension->VideoMemory = NULL;
            DeviceExtension->VideoMemorySize = 0;

            /* Free any previously allocated backup screenbuffer */
            if (DeviceExtension->ScreenBuffer)
            {
                ASSERT(DeviceExtension->ScreenBufferSize != 0);
                ExFreePoolWithTag(DeviceExtension->ScreenBuffer, TAG_BLUE);
            }
            DeviceExtension->ScreenBuffer = NULL;
            DeviceExtension->ScreenBufferSize = 0;

            /* Store dummy values */
            DeviceExtension->Columns = 1;
            DeviceExtension->Rows = 1;
            DeviceExtension->ScanLines = 1;
        }
        else
        {
            /*
             * Partially disable the screen such that it can be restored later.
             */

            /* Take a snapshot of the video memory into the backup screenbuffer */
            if (DeviceExtension->ScreenBuffer)
            {
                ASSERT(DeviceExtension->VideoMemory);
                ASSERT(DeviceExtension->ScreenBuffer);
                ASSERT(DeviceExtension->ScreenBufferSize != 0);
                ASSERT(DeviceExtension->VideoMemorySize == DeviceExtension->ScreenBufferSize);

                RtlCopyMemory(DeviceExtension->ScreenBuffer,
                              DeviceExtension->VideoMemory,
                              DeviceExtension->VideoMemorySize);
            }
        }
    }

    return TRUE; // STATUS_SUCCESS;
}

/* EOF */
