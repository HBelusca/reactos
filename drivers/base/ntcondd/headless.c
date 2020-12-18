/*
 * PROJECT:     NT / ReactOS Console Display Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     NTOS Headless Support.
 * COPYRIGHT:   Copyright 2020 Hermes Belusca-Maito
 */

/* INCLUDES ******************************************************************/

#include "ntcondd.h"
#include <ndk/inbvfuncs.h>

#define NDEBUG
#include <debug.h>

/* TYPEDEFS ******************************************************************/

// TODO: Global structure representing the unique headless terminal support.

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


static NTSTATUS
HeadlessWrite()
{
    HeadlessDispatch(HeadlessCmdPutString,
                     String,
                     strlen(String) + sizeof(ANSI_NULL),
                     NULL,
                     NULL);
}

static NTSTATUS
HeadlessInitTerminal(VOID)
{
    HEADLESS_CMD_ENABLE_TERMINAL EnableTerminal;

    EnableTerminal.Enable = TRUE;
    return HeadlessDispatch(HeadlessCmdEnableTerminal,
                            &EnableTerminal,
                            sizeof(EnableTerminal)
                            NULL, NULL);
}

static NTSTATUS
HeadlessDeinitTerminal(VOID)
{
    HEADLESS_CMD_ENABLE_TERMINAL EnableTerminal;

    EnableTerminal.Enable = FALSE;
    return HeadlessDispatch(HeadlessCmdEnableTerminal,
                            &EnableTerminal,
                            sizeof(EnableTerminal)
                            NULL, NULL);
}

// TODO: Function table !!


/* EOF */
