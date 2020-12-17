/*
 * PROJECT:     NT / ReactOS Headless Terminal Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Driver Management Functions.
 * COPYRIGHT:   Copyright 2020 Hermes Belusca-Maito
 */

/* INCLUDES ******************************************************************/

#include "hdlsterm.h"
// #include <ntoskrnl/include/internal/hdl.h>
#include <../../ntoskrnl/include/internal/hdl.h>
#include <ntddhdls.h>

/* PSEH for SEH Support */
#include <pseh/pseh2.h>

#define NDEBUG
#include <debug.h>

#ifdef __REACTOS__
// Downgrade unsupported NT6.2+ features.
#undef MdlMappingNoExecute
#define MdlMappingNoExecute 0
#endif

/* TYPEDEFS ******************************************************************/

typedef struct _DEVICE_EXTENSION
{
    BOOLEAN Enabled;
    // ULONG   CursorSize;
    // INT     CursorVisible;
    // USHORT  CharAttribute;
    ULONG   Mode;
    // USHORT  Rows;       /* Number of rows        */
    // USHORT  Columns;    /* Number of columns     */
    // USHORT  CursorX, CursorY; /* Cursor position */
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

#if 0
static const UCHAR DefaultPalette[] =
{
    0, 0, 0,
    0, 0, 0xC0,
    0, 0xC0, 0,
    0, 0xC0, 0xC0,
    0xC0, 0, 0,
    0xC0, 0, 0xC0,
    0xC0, 0xC0, 0,
    0xC0, 0xC0, 0xC0,
    0x80, 0x80, 0x80,
    0, 0, 0xFF,
    0, 0xFF, 0,
    0, 0xFF, 0xFF,
    0xFF, 0, 0,
    0xFF, 0, 0xFF,
    0xFF, 0xFF, 0,
    0xFF, 0xFF, 0xFF
};
#endif

/* FUNCTIONS *****************************************************************/

#define GET_USER_BUFFER_IO_BUFFERED(buffer, Irp) \
do { \
    ASSERT((Irp)->AssociatedIrp.SystemBuffer); \
    (buffer) = (Irp)->AssociatedIrp.SystemBuffer; \
} while (0)

#define GET_INPUT_USER_BUFFER_IO_DIRECT(buffer, Irp) \
do { \
    ASSERT((Irp)->AssociatedIrp.SystemBuffer); \
    (buffer) = (Irp)->AssociatedIrp.SystemBuffer; \
} while (0)

#define GET_OUTPUT_USER_BUFFER_IO_DIRECT(buffer, Irp, MmDefaultMapPriority) \
do { \
    ASSERT((Irp)->MdlAddress); \
    (buffer) = MmGetSystemAddressForMdlSafe((Irp)->MdlAddress, \
                                            (MmDefaultMapPriority) \
                                                ? (MmDefaultMapPriority) \
                                                : NormalPagePriority | MdlMappingNoExecute); \
} while (0)

#define GET_INPUT_USER_BUFFER_IO_NEITHER(buffer, Irp) \
do { \
    ASSERT(IoGetCurrentIrpStackLocation(Irp)->Parameters.DeviceIoControl.Type3InputBuffer); \
    (buffer) = IoGetCurrentIrpStackLocation(Irp)->Parameters.DeviceIoControl.Type3InputBuffer; \
} while (0)

#define GET_OUTPUT_USER_BUFFER_IO_NEITHER(buffer, Irp) \
do { \
    ASSERT((Irp)->UserBuffer); \
    (buffer) = (Irp)->UserBuffer; \
} while (0)


typedef enum _DEVICE_IO_TYPE
{
    // DeviceIoUndefined,
    DeviceIoNeither,
    DeviceIoBuffered,
    DeviceIoDirect,
    DeviceIoMaximum
} DEVICE_IO_TYPE, *PDEVICE_IO_TYPE;

#define GET_INPUT_USER_BUFFER_IO(buffer, Irp, deviceIoType, MmDefaultMapPriority) \
do { \
    switch (deviceIoType) \
    { \
    case DeviceIoBuffered: \
        GET_USER_BUFFER_IO_BUFFERED((buffer), (Irp)); \
        break; \
    case DeviceIoDirect: \
        GET_INPUT_USER_BUFFER_IO_DIRECT((buffer), (Irp)); \
        break; \
    case DeviceIoNeither: \
        GET_INPUT_USER_BUFFER_IO_NEITHER((buffer), (Irp)); \
        break; \
    default: \
        ASSERT(FALSE); \
    } \
} while (0)

#define GET_OUTPUT_USER_BUFFER_IO(buffer, Irp, deviceIoType, MmDefaultMapPriority) \
do { \
    switch (deviceIoType) \
    { \
    case DeviceIoBuffered: \
        GET_USER_BUFFER_IO_BUFFERED((buffer), (Irp)); \
        break; \
    case DeviceIoDirect: \
        GET_OUTPUT_USER_BUFFER_IO_DIRECT((buffer), (Irp), (MmDefaultMapPriority)); \
        break; \
    case DeviceIoNeither: \
        GET_OUTPUT_USER_BUFFER_IO_NEITHER((buffer), (Irp)); \
        break; \
    default: \
        ASSERT(FALSE); \
    } \
} while (0)

#if 0 // Informative utility function
static NTSTATUS
HdlGetUserBuffers(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_opt_ MM_PAGE_PRIORITY MmDefaultMapPriority,
    _Out_opt_ PDEVICE_IO_TYPE DeviceIoType,
    _Out_opt_ PVOID* InputBuffer,
    _Out_opt_ PVOID* OutputBuffer)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    DEVICE_IO_TYPE deviceIoType;

    if ((IrpSp->MajorFunction == IRP_MJ_DEVICE_CONTROL) ||
        (IrpSp->MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL) |
        (IrpSp->MajorFunction == IRP_MJ_FILE_SYSTEM_CONTROL))
    {
        switch (METHOD_FROM_CTL_CODE(IrpSp->Parameters.DeviceIoControl.IoControlCode))
        {
        /*
         * Buffered IO.
         *
         * The contents of the user input buffer are captured
         * into the SystemBuffer.
         * Both input and output buffers are the same.
         * Read the content of the buffer before writing to it.
         */
        case METHOD_BUFFERED:
            deviceIoType = DeviceIoBuffered;
            break;

        /*
         * Direct IO
         *
         * The contents of the user input buffer are captured
         * into the SystemBuffer.
         *
         * For the user output buffer:
         * - METHOD_IN_DIRECT:
         *   the I/O manager probes to see whether the virtual address
         *   is readable in the callers access mode, locks the pages in
         *   memory and passes the pointer to MDL describing the buffer
         *   in Irp->MdlAddress.
         *
         * - METHOD_OUT_DIRECT:
         *   the I/O manager probes to see whether the virtual address
         *   is writable in the callers access mode, locks the pages in
         *   memory and passes the pointer to MDL describing the buffer
         *   in Irp->MdlAddress.
         */
        case METHOD_IN_DIRECT:
        case METHOD_OUT_DIRECT:
            deviceIoType = DeviceIoDirect;
            break;

        /*
         * Neither IO
         *
         * The I/O manager assigns the user input to Type3InputBuffer
         * and the output buffer to Irp->UserBuffer.
         * No buffer capture nor kernel-mapping of the buffers is done,
         * neither is performed any validation of user buffer's address
         * range, which is left to the caller to do.
         */
        case METHOD_NEITHER:
        default: // Fall-back case, even if we are fully enumerated all the
                 // possible METHODs (only 4 possible, encoded on 2 bits).
            deviceIoType = DeviceIoNeither;
            break;
        }

        if (DeviceIoType)
            *DeviceIoType = deviceIoType;

        if (InputBuffer)
        {
            GET_INPUT_USER_BUFFER_IO(*InputBuffer, Irp, deviceIoType, MmDefaultMapPriority);
            if (*InputBuffer == NULL)
                return STATUS_INSUFFICIENT_RESOURCES;
        }
        if (OutputBuffer)
        {
            GET_OUTPUT_USER_BUFFER_IO(*OutputBuffer, Irp, deviceIoType, MmDefaultMapPriority);
            if (*OutputBuffer == NULL)
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        return STATUS_SUCCESS;
    }
    else if ((IrpSp->MajorFunction == IRP_MJ_READ) ||
             (IrpSp->MajorFunction == IRP_MJ_WRITE))
    {
        switch (DeviceObject->Flags & (DO_BUFFERED_IO | DO_DIRECT_IO))
        {
        /* Buffered IO (takes precedence) */
        case DO_BUFFERED_IO:
            deviceIoType = DeviceIoBuffered;
            break;

        /* Direct IO */
        case DO_DIRECT_IO:
            deviceIoType = DeviceIoDirect;
            break;

        /* Neither IO */
        default:
            deviceIoType = DeviceIoNeither;
            break;
        }

        if (DeviceIoType)
            *DeviceIoType = deviceIoType;

        if (IrpSp->MajorFunction == IRP_MJ_READ)
        {
            if (InputBuffer)
                *InputBuffer = NULL;

            if (OutputBuffer)
            {
                GET_OUTPUT_USER_BUFFER_IO(*OutputBuffer, Irp, deviceIoType, MmDefaultMapPriority);
                if (*OutputBuffer == NULL)
                    return STATUS_INSUFFICIENT_RESOURCES;
            }
        }
        else // if (IrpSp->MajorFunction == IRP_MJ_WRITE)
        {
            if (OutputBuffer)
                *OutputBuffer = NULL;

            if (InputBuffer)
            {
                GET_OUTPUT_USER_BUFFER_IO(*InputBuffer, Irp, deviceIoType, MmDefaultMapPriority);
                if (*InputBuffer == NULL)
                    return STATUS_INSUFFICIENT_RESOURCES;
            }
        }

        return STATUS_SUCCESS;
    }

    return STATUS_INVALID_DEVICE_REQUEST;
}
#endif

static DRIVER_DISPATCH HdlCreateClose;
static NTSTATUS
NTAPI
HdlCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION stk = IoGetCurrentIrpStackLocation(Irp);

    UNREFERENCED_PARAMETER(DeviceObject);

    if (stk->MajorFunction == IRP_MJ_CREATE)
        Irp->IoStatus.Information = FILE_OPENED;
    // else: IRP_MJ_CLOSE

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

#if 0
static NTSTATUS
HdlReadCooked(
    _Inout_ PVOID Data,
    _In_ SIZE_T DataSize,
    _Out_opt_ PSIZE_T NumBytesRead)
{
    NTSTATUS Status;
    // HEADLESS_RSP_GET_LINE GetLine;
    // SIZE_T InfoSize = sizeof(GetLine);

    if (DataSize == 0)
    {
        /* Nothing to read, bail out */
        Status = STATUS_SUCCESS;
        goto Quit;
    }

    // TODO!
    // Status = HeadlessDispatch(HeadlessCmdGetLine,
                              // NULL,
                              // 0,
                              // &GetLine,
                              // &InfoSize);
    Status = STATUS_SUCCESS;

Quit:
    if (NumBytesRead)
        *NumBytesRead = 0;
    return Status;
}
#endif

static NTSTATUS
HdlReadRaw(
    _Inout_ PVOID Data,
    _In_ SIZE_T DataSize,
    _Out_opt_ PSIZE_T NumBytesRead)
{
    NTSTATUS Status;
    PUCHAR Value = (PUCHAR)Data;
    HEADLESS_RSP_GET_BYTE GetByte;
    SIZE_T InfoSize = sizeof(GetByte);

    if (DataSize == 0)
    {
        /* Nothing to read, bail out */
        Status = STATUS_SUCCESS;
        goto Quit;
    }

    while (DataSize-- > 0)
    {
        // InfoSize = sizeof(GetByte);
        Status = HeadlessDispatch(HeadlessCmdGetByte,
                                  NULL,
                                  0,
                                  &GetByte,
                                  &InfoSize);

        /* Break out if no more data is available */
        if (!NT_SUCCESS(Status)) break;
        if (!GetByte.Value) break;

        *Value++ = GetByte.Value;
    }

Quit:
    if (NumBytesRead)
        *NumBytesRead = Value - (PUCHAR)Data;
    return Status;
}

static DRIVER_DISPATCH HdlRead;
static NTSTATUS
NTAPI
HdlRead(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    NTSTATUS Status;
    PIO_STACK_LOCATION stk = IoGetCurrentIrpStackLocation(Irp);
    PDEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PVOID Data;
    SIZE_T DataSize;

//
// FIXME: Don't do anything if the headless terminal is disabled.
//

    /* Validate input buffer */
    DataSize = stk->Parameters.Read.Length;
    if (DataSize == 0)
    {
        /* Nothing to read, bail out */
        Status = STATUS_SUCCESS;
        goto Quit;
    }
    if (DeviceObject->Flags & DO_BUFFERED_IO)
    {
        /* Buffered IO */
        GET_USER_BUFFER_IO_BUFFERED(Data, Irp);
    }
    else if (DeviceObject->Flags & DO_DIRECT_IO)
    {
        /* Direct IO */
        GET_OUTPUT_USER_BUFFER_IO_DIRECT(Data, Irp, 0);
    }
    else
    {
        /* Neither IO */
        GET_OUTPUT_USER_BUFFER_IO_NEITHER(Data, Irp);
        /// Mama Mia!! This requires the usage of SEH + ProbeForRead() etc...
    }
    if (Data == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        DataSize = 0;
        goto Quit;
    }

    if (DeviceExtension->Enabled)
    {
        // TODO 1: Use Raw or Cooked depending on the mode.
        // TODO 2: Use IO pending queue.
        Status = HdlReadRaw(Data, DataSize, &DataSize);
    }
    else
    {
        Status = STATUS_SUCCESS; // STATUS_DEVICE_NOT_READY; ??
        DataSize = 0;
    }

Quit:
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = DataSize;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    // IoCompleteRequest(Irp, IO_SERIAL_INCREMENT /*IO_VIDEO_INCREMENT*/);

    return Status;
}

#if 0
//
// TODO: FIXME! See IOCTL_HDLSTERM_PUT_STRING / HeadlessCmdPutString
//
static NTSTATUS
HdlWriteCooked(
    _In_ PVOID Data,
    _In_ SIZE_T DataSize,
    _Out_opt_ PSIZE_T NumBytesWritten)
{
    NTSTATUS Status;
    PIO_STACK_LOCATION stk = IoGetCurrentIrpStackLocation(Irp);
    PDEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PCHAR pch = Irp->UserBuffer;
    PUCHAR vidmem;
    ULONG i;
    ULONG j, offset;
    USHORT cursorx, cursory;
    USHORT rows, columns;
    // BOOLEAN processed = !!(DeviceExtension->Mode & ENABLE_PROCESSED_OUTPUT);

    if (!DeviceExtension->Enabled || !DeviceExtension->VideoMemory)
    {
        /* Display is not enabled, we're not allowed to touch it */
        Status = STATUS_SUCCESS;

        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        return Status;
    }

    vidmem  = DeviceExtension->VideoMemory;
    rows    = DeviceExtension->Rows;
    columns = DeviceExtension->Columns;
    cursorx = DeviceExtension->CursorX;
    cursory = DeviceExtension->CursorY;

    if (!processed)
    {
        /* Raw output mode */
    }
    else
    {
        /* Cooked output mode */
        for (i = 0; i < stk->Parameters.Write.Length; i++, pch++)
        {
            switch (*pch)
            {
            case '\b':
            {
                if (cursorx > 0)
                {
                    cursorx--;
                }
                else if (cursory > 0)
                {
                    cursory--;
                    cursorx = columns - 1;
                }
                offset = cursorx + cursory * columns;
                vidmem[offset * 2] = ' ';
                vidmem[offset * 2 + 1] = (char)DeviceExtension->CharAttribute;
                break;
            }

            case '\n':
                cursory++;
                /* Fall back */
            case '\r':
                cursorx = 0;
                break;

            case '\t':
            {
                offset = TAB_WIDTH - (cursorx % TAB_WIDTH);
                while (offset--)
                {
                    vidmem[(cursorx + cursory * columns) * 2] = ' ';
                    cursorx++;
                    if (cursorx >= columns)
                    {
                        cursorx = 0;
                        cursory++;
                        /* We jumped to the next line, stop there */
                        break;
                    }
                }
                break;
            }

            default:
            {
                offset = cursorx + cursory * columns;
                vidmem[offset * 2] = *pch;
                vidmem[offset * 2 + 1] = (char)DeviceExtension->CharAttribute;
                cursorx++;
                if (cursorx >= columns)
                {
                    cursorx = 0;
                    cursory++;
                }
                break;
            }
            }

            /* Scroll up the contents of the screen if we are at the end */
            if (cursory >= rows)
            {
                PUSHORT LinePtr;

                RtlCopyMemory(vidmem,
                              &vidmem[columns * 2],
                              columns * (rows - 1) * 2);

                LinePtr = (PUSHORT)&vidmem[columns * (rows - 1) * 2];

                for (j = 0; j < columns; j++)
                {
                    LinePtr[j] = DeviceExtension->CharAttribute << 8;
                }
                cursory = rows - 1;
                for (j = 0; j < columns; j++)
                {
                    offset = j + cursory * columns;
                    vidmem[offset * 2] = ' ';
                    vidmem[offset * 2 + 1] = (char)DeviceExtension->CharAttribute;
                }
            }
        }
    }

    /* Set the cursor position */
    ASSERT((0 <= cursorx) && (cursorx < DeviceExtension->Columns));
    ASSERT((0 <= cursory) && (cursory < DeviceExtension->Rows));
    DeviceExtension->CursorX = cursorx;
    DeviceExtension->CursorY = cursory;
    HdlSetCursor(DeviceExtension);

    Status = STATUS_SUCCESS;

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_SERIAL_INCREMENT /*IO_VIDEO_INCREMENT*/);

    return Status;
}
#endif

static NTSTATUS
HdlWriteRaw(
    _In_ PVOID Data,
    _In_ SIZE_T DataSize,
    _Out_opt_ PSIZE_T NumBytesWritten)
{
    NTSTATUS Status;

    if (DataSize == 0)
    {
        /* Nothing to write, bail out */
        return STATUS_SUCCESS;
    }

    if (NumBytesWritten)
        *NumBytesWritten = 0;

    Status = HeadlessDispatch(HeadlessCmdPutData,
                              Data,
                              DataSize,
                              NULL,
                              NULL);
    if (NT_SUCCESS(Status))
    {
        if (NumBytesWritten)
            *NumBytesWritten = DataSize;
    }

    return Status;
}

static DRIVER_DISPATCH HdlWrite;
static NTSTATUS
NTAPI
HdlWrite(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    NTSTATUS Status;
    PIO_STACK_LOCATION stk = IoGetCurrentIrpStackLocation(Irp);
    PDEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PVOID Data;
    SIZE_T DataSize;

//
// FIXME: Don't do anything if the headless terminal is disabled.
//

    /* Validate input buffer */
    DataSize = stk->Parameters.Write.Length;
    if (DataSize == 0)
    {
        /* Nothing to write, bail out */
        Status = STATUS_SUCCESS;
        goto Quit;
    }
    if (DeviceObject->Flags & DO_BUFFERED_IO)
    {
        /* Buffered IO */
        GET_USER_BUFFER_IO_BUFFERED(Data, Irp);
    }
    else if (DeviceObject->Flags & DO_DIRECT_IO)
    {
        /* Direct IO */
        GET_OUTPUT_USER_BUFFER_IO_DIRECT(Data, Irp, 0);
    }
    else
    {
        /* Neither IO */
        GET_OUTPUT_USER_BUFFER_IO_NEITHER(Data, Irp);
        /// Mama Mia!! This requires the usage of SEH + ProbeForRead() etc...
    }
    if (Data == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        DataSize = 0;
        goto Quit;
    }

    if (DeviceExtension->Enabled)
    {
        // TODO 1: Use Raw or Cooked depending on the mode.
        // TODO 2: Use IO pending queue.
        Status = HdlWriteRaw(Data, DataSize, &DataSize);
    }
    else
    {
        Status = STATUS_SUCCESS; // STATUS_DEVICE_NOT_READY; ??
        DataSize = 0;
    }

Quit:
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = DataSize;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    // IoCompleteRequest(Irp, IO_SERIAL_INCREMENT /*IO_VIDEO_INCREMENT*/);

    return Status;
}

static DRIVER_DISPATCH HdlFlush;
static NTSTATUS
NTAPI
HdlFlush(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    NTSTATUS Status;
    // PIO_STACK_LOCATION stk = IoGetCurrentIrpStackLocation(Irp);
    PDEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    HEADLESS_RSP_GET_BYTE GetByte;
    SIZE_T InfoSize = sizeof(GetByte);

    if (DeviceExtension->Enabled)
    {
        /* Flush the headless terminal input queue */
        for (;;)
        {
            // InfoSize = sizeof(GetByte);
            Status = HeadlessDispatch(HeadlessCmdGetByte,
                                      NULL,
                                      0,
                                      &GetByte,
                                      &InfoSize);

            /* Break out if no more data is available */
            if (!NT_SUCCESS(Status)) break;
            if (!GetByte.Value) break;
        }
    }
    else
    {
        Status = STATUS_SUCCESS; // STATUS_DEVICE_NOT_READY; ??
    }

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_SERIAL_INCREMENT);
    return STATUS_SUCCESS;
}


static struct
{
    ULONG InputBufferLength;
    ULONG OutputBufferLength;
    BOOLEAN ExactLength; // TRUE: Exact size as specified; FALSE: Minimal size.
}
ExpectedBufferSizes[] =
{
    {sizeof(HEADLESS_CMD_ENABLE_TERMINAL), 0, TRUE},    // HeadlessCmdEnableTerminal
    {0, 0, TRUE},   // FIXME! HeadlessCmdCheckForReboot
    {0, 0, FALSE},  // HeadlessCmdPutString
    {0, 0, TRUE},   // HeadlessCmdClearDisplay
    {0, 0, TRUE},   // HeadlessCmdClearToEndOfDisplay
    {0, 0, TRUE},   // HeadlessCmdClearToEndOfLine
    {0, 0, TRUE},   // HeadlessCmdDisplayAttributesOff
    {0, 0, TRUE},   // HeadlessCmdDisplayInverseVideo
    {sizeof(HEADLESS_CMD_SET_COLOR)      , 0, TRUE},   // HeadlessCmdSetColor
    {sizeof(HEADLESS_CMD_POSITION_CURSOR), 0, TRUE},   // HeadlessCmdPositionCursor
    {0, sizeof(HEADLESS_RSP_TERMINAL_POLL)  , TRUE},   // HeadlessCmdTerminalPoll
    {0, sizeof(HEADLESS_RSP_GET_BYTE)       , TRUE},   // HeadlessCmdGetByte
    {0, 0, FALSE},  // FIXME! HeadlessCmdGetLine
    {0, 0, TRUE},   // HeadlessCmdStartBugCheck
    {0, 0, TRUE},   // HeadlessCmdDoBugCheckProcessing
    {0, sizeof(HEADLESS_RSP_QUERY_INFO)     , TRUE},   // HeadlessCmdQueryInformation
    {0, 0, FALSE},  // FIXME! HeadlessCmdAddLogEntry
    {0, 0, TRUE},   // FIXME! HeadlessCmdDisplayLog
    {sizeof(HEADLESS_CMD_SET_BLUE_SCREEN_DATA), 0, FALSE},  // HeadlessCmdSetBlueScreenData
    {0, 0, TRUE},   // FIXME! HeadlessCmdSendBlueScreenData
    {0, sizeof(GUID)                        , TRUE},   // HeadlessCmdQueryGUID
    {0, 0, FALSE},  // HeadlessCmdPutData
};
C_ASSERT(HeadlessCmdPutData == 22);
C_ASSERT(RTL_NUMBER_OF(ExpectedBufferSizes) == HeadlessCmdPutData);


static DRIVER_DISPATCH HdlIoControl;
static NTSTATUS
NTAPI
HdlIoControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    NTSTATUS Status;
    PIO_STACK_LOCATION stk = IoGetCurrentIrpStackLocation(Irp);
    PDEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PVOID InputBuffer, OutputBuffer;

    switch (stk->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_HDLSTERM_ENABLE_TERMINAL:
        {
            HEADLESS_CMD_ENABLE_TERMINAL EnableTerminal;

            /* Validate input buffer */
            if (stk->Parameters.DeviceIoControl.InputBufferLength != sizeof(ULONG))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            GET_USER_BUFFER_IO_BUFFERED(InputBuffer, Irp);

            EnableTerminal.Enable = !!*(PULONG)InputBuffer;

            Status = HeadlessDispatch(HeadlessCmdEnableTerminal,
                                      &EnableTerminal,
                                      sizeof(EnableTerminal),
                                      NULL,
                                      NULL);
            if (NT_SUCCESS(Status) && EnableTerminal.Enable)
            {
                /* Initialize screen at next write */
                HdlWriteRaw("\x1B""c"   /* Reset device */
                            "\x1B[7l"   /* Disable line wrap */
                            "\x1B[3g"   /* Clear all tabs */
                            ,
                            10, NULL);

                /* Cleanup the screen and reset the cursor */
                HdlWriteRaw("\x1B[2J" "\x1B[H", 7, NULL);
            }
            DeviceExtension->Enabled = EnableTerminal.Enable;

            Irp->IoStatus.Information = 0;
            break;
        }

        case IOCTL_HDLSTERM_QUERY_INFO:
        {
            PHEADLESS_RSP_QUERY_INFO HeadlessInfo;
            SIZE_T InfoSize = sizeof(*HeadlessInfo);

            /* Validate output buffer */
            if (stk->Parameters.DeviceIoControl.OutputBufferLength < sizeof(*HeadlessInfo))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            GET_USER_BUFFER_IO_BUFFERED(OutputBuffer, Irp);

            HeadlessInfo = (PHEADLESS_RSP_QUERY_INFO)OutputBuffer;
            RtlZeroMemory(HeadlessInfo, sizeof(*HeadlessInfo));

            Status = HeadlessDispatch(HeadlessCmdQueryInformation,
                                      NULL,
                                      0,
                                      HeadlessInfo,
                                      &InfoSize);
            Irp->IoStatus.Information = sizeof(*HeadlessInfo);
            break;
        }

        case IOCTL_HDLSTERM_SET_COLOR:
        {
            HEADLESS_CMD_SET_COLOR SetColor;

            /* Validate input buffer */
            if (stk->Parameters.DeviceIoControl.InputBufferLength != sizeof(SetColor))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            GET_USER_BUFFER_IO_BUFFERED(InputBuffer, Irp);

            if (DeviceExtension->Enabled)
            {
                // SetColor.TextColor;
                // SetColor.BkgdColor;
                SetColor = *(PHEADLESS_CMD_SET_COLOR)InputBuffer;

                /* Sends a formatted "\x1B[%d;%dm" command */
                Status = HeadlessDispatch(HeadlessCmdSetColor,
                                          &SetColor,
                                          sizeof(SetColor),
                                          NULL,
                                          NULL);
            }
            Irp->IoStatus.Information = 0;
            break;
        }

        case IOCTL_HDLSTERM_CLEAR_DISPLAY:
        {
#if 0
            /* Validate input buffer */
            if (stk->Parameters.DeviceIoControl.InputBufferLength != 0)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
#endif

            if (DeviceExtension->Enabled)
            {
                /* Sends a "\x1B[2J" command */
                Status = HeadlessDispatch(HeadlessCmdClearDisplay,
                                          NULL, 0,
                                          NULL, NULL);
            }
            Irp->IoStatus.Information = 0;
            break;
        }

        case IOCTL_HDLSTERM_CLEAR_TO_END_DISPLAY:
        {
#if 0
            /* Validate input buffer */
            if (stk->Parameters.DeviceIoControl.InputBufferLength != 0)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
#endif

            if (DeviceExtension->Enabled)
            {
                /* Sends a "\x1B[0J" command */
                Status = HeadlessDispatch(HeadlessCmdClearToEndOfDisplay,
                                          NULL, 0,
                                          NULL, NULL);
            }
            Irp->IoStatus.Information = 0;
            break;
        }

        case IOCTL_HDLSTERM_CLEAR_TO_END_LINE:
        {
#if 0
            /* Validate input buffer */
            if (stk->Parameters.DeviceIoControl.InputBufferLength != 0)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
#endif

            if (DeviceExtension->Enabled)
            {
                /* Sends a "\x1B[0K" command */
                Status = HeadlessDispatch(HeadlessCmdClearToEndOfLine,
                                          NULL, 0,
                                          NULL, NULL);
            }
            Irp->IoStatus.Information = 0;
            break;
        }

        case IOCTL_HDLSTERM_ATTRIBUTES_OFF:
        {
#if 0
            /* Validate input buffer */
            if (stk->Parameters.DeviceIoControl.InputBufferLength != 0)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
#endif

            if (DeviceExtension->Enabled)
            {
                /* Sends a "\x1B[0m" command */
                Status = HeadlessDispatch(HeadlessCmdDisplayAttributesOff,
                                          NULL, 0,
                                          NULL, NULL);
            }
            Irp->IoStatus.Information = 0;
            break;
        }

        case IOCTL_HDLSTERM_INVERSE_VIDEO:
        {
#if 0
            /* Validate input buffer */
            if (stk->Parameters.DeviceIoControl.InputBufferLength != 0)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
#endif

            if (DeviceExtension->Enabled)
            {
                /* Sends a "\x1B[7m" command */
                Status = HeadlessDispatch(HeadlessCmdDisplayInverseVideo,
                                          NULL, 0,
                                          NULL, NULL);
            }
            Irp->IoStatus.Information = 0;
            break;
        }

        case IOCTL_HDLSTERM_POSITION_CURSOR:
        {
            HEADLESS_CMD_POSITION_CURSOR CursorPos;

            /* Validate input buffer */
            if (stk->Parameters.DeviceIoControl.InputBufferLength != sizeof(CursorPos))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            GET_USER_BUFFER_IO_BUFFERED(InputBuffer, Irp);

            if (DeviceExtension->Enabled)
            {
                // CursorPos.CursorCol;
                // CursorPos.CursorRow;
                CursorPos = *(PHEADLESS_CMD_POSITION_CURSOR)InputBuffer;

                /* Sends a formatted "\x1B[%d;%dH" command */
                Status = HeadlessDispatch(HeadlessCmdPositionCursor,
                                          &CursorPos,
                                          sizeof(CursorPos),
                                          NULL,
                                          NULL);
            }
            Irp->IoStatus.Information = 0;
            break;
        }

        case IOCTL_HDLSTERM_TERMINAL_POLL:
        {
            HEADLESS_RSP_TERMINAL_POLL TerminalPoll;
            SIZE_T InfoSize = sizeof(TerminalPoll);
            PULONG DataPresent;

            /* Validate output buffer */
            if (stk->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ULONG))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            GET_USER_BUFFER_IO_BUFFERED(OutputBuffer, Irp);

            DataPresent = (PULONG)OutputBuffer;
            *DataPresent = 0;

            if (DeviceExtension->Enabled)
            {
                Status = HeadlessDispatch(HeadlessCmdTerminalPoll,
                                          NULL,
                                          0,
                                          &TerminalPoll,
                                          &InfoSize);
                if (NT_SUCCESS(Status))
                    *DataPresent = TerminalPoll.DataPresent;
            }

            Irp->IoStatus.Information = sizeof(ULONG);
            break;
        }

#if 0 // TODO in some HdlWrite mode.
        case IOCTL_HDLSTERM_PUT_STRING:
        {
            PUCHAR String, ptr;
            SIZE_T StringSize;

            /* Validate input buffer */
            if (stk->Parameters.DeviceIoControl.InputBufferLength == 0)
            {
                /* Nothing to write, bail out */
                Irp->IoStatus.Information = 0;
                Status = STATUS_SUCCESS;
                break;
            }
            // GET_INPUT_USER_BUFFER_IO_DIRECT(InputBuffer, Irp);
            GET_OUTPUT_USER_BUFFER_IO_DIRECT(String, Irp, 0);
            if (String == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
            StringSize = stk->Parameters.DeviceIoControl.InputBufferLength;

            /* We expect a NULL-terminated ANSI or UTF-8 string:
             * verify that such a NULL is present within the string. */
            ptr = String;
            while ( (((ULONG_PTR)ptr - (ULONG_PTR)String) < StringSize) &&
                    (*ptr != ANSI_NULL) )
            {
                /* Loop as long as we haven't found any NULL */
                ++ptr;
            }
            if ( (((ULONG_PTR)ptr - (ULONG_PTR)String) >= StringSize) ||
                 (*ptr != ANSI_NULL) )
            {
                /* No NULL was found within the string buffer, bail out */
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (DeviceExtension->Enabled)
            {
                Status = HeadlessDispatch(HeadlessCmdPutString,
                                          String,
                                          StringSize,
                                          NULL,
                                          NULL);
            }
            Irp->IoStatus.Information = 0;
            break;
        }
#endif

        case IOCTL_HDLSTERM_SEND_COMMAND:
        {
            SIZE_T InputSize  = stk->Parameters.DeviceIoControl.InputBufferLength;
            SIZE_T OutputSize = stk->Parameters.DeviceIoControl.OutputBufferLength;
            HEADLESS_CMD Command;

            /* Pre-validate input buffer */
            if (InputSize < FIELD_OFFSET(HDLSTERM_COMMAND, Data))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            /* Retrieve the input buffer, probe it for read access
             * and retrieve the command. */
            GET_INPUT_USER_BUFFER_IO_NEITHER(InputBuffer, Irp);
            _SEH2_TRY
            {
                ProbeForRead(InputBuffer, InputSize, sizeof(UCHAR));
                Command = ((PHDLSTERM_COMMAND)InputBuffer)->Command;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
                _SEH2_YIELD(break);
            }
            _SEH2_END;

            /* Validate the command (1-based, see HEADLESS_CMD) */
            if ((Command <= 0) || (Command > HeadlessCmdPutData))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            /* Modify InputSize to reflect the actual size of the
             * data payload, by subtracting from it the header size. */
            InputSize -= FIELD_OFFSET(HDLSTERM_COMMAND, Data);

            /* Verify the sizes of the data buffers */
            if ((ExpectedBufferSizes[Command-1].ExactLength &&
                 (InputSize != ExpectedBufferSizes[Command-1].InputBufferLength))
                ||
                (InputSize < ExpectedBufferSizes[Command-1].InputBufferLength))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            if ((ExpectedBufferSizes[Command-1].OutputBufferLength != 0) &&
                (OutputSize < ExpectedBufferSizes[Command-1].OutputBufferLength))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            /* Normalize the sizes in case of zero-lengths */
            if (ExpectedBufferSizes[Command-1].ExactLength)
            {
                if (ExpectedBufferSizes[Command-1].InputBufferLength == 0)
                    InputSize = 0;
                if (ExpectedBufferSizes[Command-1].OutputBufferLength == 0)
                    OutputSize = 0;
            }

            /* Retrieve the pointer to the actual input data */
            if (InputSize != 0)
            {
                /* Point to the actual data */
                InputBuffer = &((PHDLSTERM_COMMAND)InputBuffer)->Data;
            }
            else
            {
                /* No actual input data */
                InputBuffer = NULL;
            }

//
// FIXME: TODO: In case of Command == HeadlessCmdPutString, we MUST
// also capture the string buffer and ensure it's NULL-terminated, since
// (on Windows) in this case the HeadlessDispatch() won't use the InputSize
// when sending the string to the terminal, but instead will loop until
// it gets an ANSI_NULL.
//

            /* Retrieve the pointer to the output buffer */
            if (OutputSize != 0)
            {
                /* Retrieve the output buffer and probe it for write access */
                GET_OUTPUT_USER_BUFFER_IO_NEITHER(OutputBuffer, Irp);
                _SEH2_TRY
                {
                    ProbeForWrite(OutputBuffer, OutputSize, sizeof(UCHAR));
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    Status = _SEH2_GetExceptionCode();
                    _SEH2_YIELD(break);
                }
                _SEH2_END;
            }
            else
            {
                /* No output buffer */
                OutputBuffer = NULL;
            }

/*
 * NOTE: We are going to access the buffers directly, because we are
 * running in the context of the calling process. Only top-level drivers
 * are guaranteed to have the context of process that made the request.
 *
 * If this implementation is changed and these buffers are being accessed
 * in an arbitrary thread context, for example in a DPC or ISR, if they are
 * being used for DMA, or passing these buffers to the next level driver,
 * these buffers must be mapped in the system process address space.
 * First an MDL, large enough to describe the buffer, must be allocated
 * and initilized. Please note that on a x86 system, the maximum size of
 * a buffer that an MDL can describe is 65508 KB.
 *
 * NOTE 2: It is not actually guaranteed we are always the top-level driver?
 */
            if (DeviceExtension->Enabled)
            {
                /* Send the command -- Protect in SEH */
                _SEH2_TRY
                {
                    Status = HeadlessDispatch(Command,
                                              InputBuffer,
                                              InputSize,
                                              OutputBuffer,
                                              &OutputSize);
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    Status = _SEH2_GetExceptionCode();
                }
                _SEH2_END;

                Irp->IoStatus.Information = OutputSize;
            }
            else
            {
                Irp->IoStatus.Information = 0;
            }

            break;
        }

#if 0
        case IOCTL_HDLSTERM_GET_SCREEN_BUFFER_INFO:
        {
            PCONSOLE_SCREEN_BUFFER_INFO pcsbi;
            USHORT rows = DeviceExtension->Rows;
            USHORT columns = DeviceExtension->Columns;

            /* Validate output buffer */
            if (stk->Parameters.DeviceIoControl.OutputBufferLength < sizeof(CONSOLE_SCREEN_BUFFER_INFO))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            ASSERT(Irp->AssociatedIrp.SystemBuffer);

            pcsbi = (PCONSOLE_SCREEN_BUFFER_INFO)Irp->AssociatedIrp.SystemBuffer;
            RtlZeroMemory(pcsbi, sizeof(CONSOLE_SCREEN_BUFFER_INFO));

            pcsbi->dwSize.X = columns;
            pcsbi->dwSize.Y = rows;

            pcsbi->dwCursorPosition.X = DeviceExtension->CursorX;
            pcsbi->dwCursorPosition.Y = DeviceExtension->CursorY;

            pcsbi->wAttributes = DeviceExtension->CharAttribute;

            pcsbi->srWindow.Left   = 0;
            pcsbi->srWindow.Right  = columns - 1;
            pcsbi->srWindow.Top    = 0;
            pcsbi->srWindow.Bottom = rows - 1;

            pcsbi->dwMaximumWindowSize.X = columns;
            pcsbi->dwMaximumWindowSize.Y = rows;

            Irp->IoStatus.Information = sizeof(CONSOLE_SCREEN_BUFFER_INFO);
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_HDLSTERM_SET_SCREEN_BUFFER_INFO:
        {
            PCONSOLE_SCREEN_BUFFER_INFO pcsbi;

            /* Validate input buffer */
            if (stk->Parameters.DeviceIoControl.InputBufferLength < sizeof(CONSOLE_SCREEN_BUFFER_INFO))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            ASSERT(Irp->AssociatedIrp.SystemBuffer);

            pcsbi = (PCONSOLE_SCREEN_BUFFER_INFO)Irp->AssociatedIrp.SystemBuffer;

            if ( pcsbi->dwCursorPosition.X < 0 || pcsbi->dwCursorPosition.X >= DeviceExtension->Columns ||
                 pcsbi->dwCursorPosition.Y < 0 || pcsbi->dwCursorPosition.Y >= DeviceExtension->Rows )
            {
                Irp->IoStatus.Information = 0;
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            DeviceExtension->CharAttribute = pcsbi->wAttributes;

            /* Set the cursor position */
            ASSERT((0 <= pcsbi->dwCursorPosition.X) && (pcsbi->dwCursorPosition.X < DeviceExtension->Columns));
            ASSERT((0 <= pcsbi->dwCursorPosition.Y) && (pcsbi->dwCursorPosition.Y < DeviceExtension->Rows));
            DeviceExtension->CursorX = pcsbi->dwCursorPosition.X;
            DeviceExtension->CursorY = pcsbi->dwCursorPosition.Y;
            if (DeviceExtension->Enabled)
                HdlSetCursor(DeviceExtension);

            Irp->IoStatus.Information = 0;
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_HDLSTERM_GET_CURSOR_INFO:
        {
            PCONSOLE_CURSOR_INFO pcci;

            /* Validate output buffer */
            if (stk->Parameters.DeviceIoControl.OutputBufferLength < sizeof(CONSOLE_CURSOR_INFO))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            ASSERT(Irp->AssociatedIrp.SystemBuffer);

            pcci = (PCONSOLE_CURSOR_INFO)Irp->AssociatedIrp.SystemBuffer;
            RtlZeroMemory(pcci, sizeof(CONSOLE_CURSOR_INFO));

            pcci->dwSize = DeviceExtension->CursorSize;
            pcci->bVisible = DeviceExtension->CursorVisible;

            Irp->IoStatus.Information = sizeof(CONSOLE_CURSOR_INFO);
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_HDLSTERM_SET_CURSOR_INFO:
        {
            PCONSOLE_CURSOR_INFO pcci;

            /* Validate input buffer */
            if (stk->Parameters.DeviceIoControl.InputBufferLength < sizeof(CONSOLE_CURSOR_INFO))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            ASSERT(Irp->AssociatedIrp.SystemBuffer);

            pcci = (PCONSOLE_CURSOR_INFO)Irp->AssociatedIrp.SystemBuffer;

            DeviceExtension->CursorSize = pcci->dwSize;
            DeviceExtension->CursorVisible = pcci->bVisible;
            if (DeviceExtension->Enabled)
                HdlSetCursorShape(DeviceExtension);

            Irp->IoStatus.Information = 0;
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_HDLSTERM_GET_MODE:
        {
            PCONSOLE_MODE pcm;

            /* Validate output buffer */
            if (stk->Parameters.DeviceIoControl.OutputBufferLength < sizeof(CONSOLE_MODE))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            ASSERT(Irp->AssociatedIrp.SystemBuffer);

            pcm = (PCONSOLE_MODE)Irp->AssociatedIrp.SystemBuffer;
            RtlZeroMemory(pcm, sizeof(CONSOLE_MODE));

            pcm->dwMode = DeviceExtension->Mode;

            Irp->IoStatus.Information = sizeof(CONSOLE_MODE);
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_HDLSTERM_SET_MODE:
        {
            PCONSOLE_MODE pcm;

            /* Validate input buffer */
            if (stk->Parameters.DeviceIoControl.InputBufferLength < sizeof(CONSOLE_MODE))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            ASSERT(Irp->AssociatedIrp.SystemBuffer);

            pcm = (PCONSOLE_MODE)Irp->AssociatedIrp.SystemBuffer;
            DeviceExtension->Mode = pcm->dwMode;

            Irp->IoStatus.Information = 0;
            Status = STATUS_SUCCESS;
            break;
        }

#endif

        default:
            Status = STATUS_NOT_IMPLEMENTED;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_SERIAL_INCREMENT /*IO_VIDEO_INCREMENT*/);

    return Status;
}

/*static*/ DRIVER_DISPATCH HdlDispatch;
/*static*/ NTSTATUS
NTAPI
HdlDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION stk = IoGetCurrentIrpStackLocation(Irp);

    UNREFERENCED_PARAMETER(DeviceObject);

    DPRINT1("HdlDispatch(0x%p): stk->MajorFunction = %lu UNIMPLEMENTED\n",
            DeviceObject, stk->MajorFunction);

    Irp->IoStatus.Status = STATUS_NOT_IMPLEMENTED;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * Module entry point
 */
NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS Status;
    PDEVICE_OBJECT DeviceObject;
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(DD_HDLSTERM_DEVICE_NAME_U);
    HEADLESS_RSP_QUERY_INFO HeadlessInfo;
    SIZE_T InfoSize = sizeof(HeadlessInfo);
    ULONG i;

    PAGED_CODE();
    DPRINT("NT / ReactOS Headless Terminal Driver\n");

    /* Check if EMS is enabled in the kernel */
    HeadlessDispatch(HeadlessCmdQueryInformation,
                     NULL,
                     0,
                     &HeadlessInfo,
                     &InfoSize);
    if ((HeadlessInfo.PortType == HeadlessUndefinedPortType) ||
        ((HeadlessInfo.PortType == HeadlessSerialPort) &&
         !HeadlessInfo.Serial.TerminalAttached))
    {
        /* EMS is not enabled */
        return STATUS_PORT_DISCONNECTED;
    }

    /* It is, so create the device */

    // TODO: Use IoCreateDeviceSecure() instead, with restriction
    // to only SYSTEM users.
    // See SAC: "/* Protect the device against non-admins */"
    Status = IoCreateDevice(DriverObject,
                            sizeof(DEVICE_EXTENSION),
                            &DeviceName,
                            FILE_DEVICE_CONSOLE,
                            FILE_DEVICE_SECURE_OPEN,
                            TRUE, // Exclusive // Then what about the SAC console ??
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;

    // /* Make it use buffered I/O */
    // DeviceObject->Flags |= DO_BUFFERED_IO;

    /* Initialize the driver object */
    for (i = 0; i < RTL_NUMBER_OF(DriverObject->MajorFunction); ++i)
    {
        DriverObject->MajorFunction[i] = HdlDispatch;
    }
    DriverObject->MajorFunction[IRP_MJ_CREATE] = HdlCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]  = HdlCreateClose;
    DriverObject->MajorFunction[IRP_MJ_READ]   = HdlRead;
    DriverObject->MajorFunction[IRP_MJ_WRITE]  = HdlWrite;
    DriverObject->MajorFunction[IRP_MJ_FLUSH_BUFFERS]  = HdlFlush;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = HdlIoControl;
    // DriverObject->MajorFunction[IRP_MJ_SHUTDOWN] = DispatchShutdownControl; // Would need a IoRegisterShutdownNotification()
    // DriverObject->FastIoDispatch = NULL;
    // DriverObject->DriverUnload = UnloadHandler;

    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    return Status;
}

/* EOF */
