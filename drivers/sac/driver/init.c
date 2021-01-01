/*
 * PROJECT:     ReactOS Drivers
 * LICENSE:     BSD - See COPYING.ARM in the top level directory
 * FILE:        drivers/sac/driver/init.c
 * PURPOSE:     Driver for the Server Administration Console (SAC) for EMS
 * PROGRAMMERS: ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include "sacdrv.h"

/* FUNCTIONS ******************************************************************/

NTSTATUS
NTAPI
DriverEntry(IN PDRIVER_OBJECT DriverObject,
            IN PUNICODE_STRING RegistryPath)
{
    NTSTATUS Status;
    HEADLESS_RSP_QUERY_INFO HeadlessInformation;
    SIZE_T InfoSize = sizeof(HeadlessInformation);
    UNICODE_STRING DeviceName;
    PDEVICE_OBJECT DeviceObject;
    PSAC_DEVICE_EXTENSION DeviceExtension;
    ULONG i;

    PAGED_CODE();

    SAC_DBG(SAC_DBG_ENTRY_EXIT, "Entering.\n");

    /* Check if EMS is enabled in the kernel */
    HeadlessDispatch(HeadlessCmdQueryInformation,
                     NULL,
                     0,
                     &HeadlessInformation,
                     &InfoSize);
    if ((HeadlessInformation.PortType != HeadlessUndefinedPortType) &&
        ((HeadlessInformation.PortType != HeadlessSerialPort) ||
         (HeadlessInformation.Serial.TerminalAttached)))
    {
DbgBreakPoint();
        /* It is, so create the device */
        RtlInitUnicodeString(&DeviceName, L"\\Device\\SAC");
        Status = IoCreateDevice(DriverObject,
                                sizeof(SAC_DEVICE_EXTENSION),
                                &DeviceName,
                                FILE_DEVICE_UNKNOWN,
                                FILE_DEVICE_SECURE_OPEN,
                                FALSE,
                                &DeviceObject);
        if (NT_SUCCESS(Status))
        {
            /* Setup the device extension */
            DeviceExtension = DeviceObject->DeviceExtension;
            DeviceExtension->Initialized = FALSE;

            /* Initialize the driver object */
            for (i = 0; i < RTL_NUMBER_OF(DriverObject->MajorFunction); ++i)
            {
                DriverObject->MajorFunction[i] = Dispatch;
            }
            DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDeviceControl;
            DriverObject->MajorFunction[IRP_MJ_SHUTDOWN] = DispatchShutdownControl;
            DriverObject->FastIoDispatch = NULL;
            DriverObject->DriverUnload = UnloadHandler;

            /* Initialize driver data */
            if (InitializeGlobalData(RegistryPath, DriverObject))
            {
                /* Initialize device data */
                if (InitializeDeviceData(DeviceObject))
                {
                    /* We're all good, register a shutdown notification */
                    IoRegisterShutdownNotification(DeviceObject);
                    return Status;
                }
            }

            /* One of the initializations failed, bail out */
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
        else
        {
            /* Print a debug statement if enabled */
            SAC_DBG(SAC_DBG_INIT, "Unable to create device object: 0x%lx\n", Status);
        }

        /* Free any data we may have allocated and exit with failure */
        FreeGlobalData();
        SAC_DBG(SAC_DBG_ENTRY_EXIT, "Exiting with status 0x%lx\n", Status);
        return Status;
    }

    /* EMS is not enabled */
    return STATUS_PORT_DISCONNECTED;
}
