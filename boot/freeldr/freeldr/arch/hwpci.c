/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 *              or MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     PCI bus detection routines
 * COPYRIGHT:   Copyright 2007 Aleksey Bragin <aleksey@reactos.org>
 *              Copyright 2009 Hervé Poussineau <hpoussin@reactos.org>
 *              Copyright 2026 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#include <freeldr.h>
#include "../ntldr/ntldropts.h"

#include <debug.h>
DBG_DEFAULT_CHANNEL(HWDETECT);

#include <pshpack1.h>
typedef struct _PCI_REGISTRY_DEVICE
{
    union
    {
        struct
        {
            USHORT FunctionNumber : 3;
            USHORT DeviceNumber   : 5;
            USHORT BusNumber      : 8;
        } bits;
        USHORT AsUSHORT;
    } Address;

    PCI_COMMON_CONFIG PciConfig;
} PCI_REGISTRY_DEVICE, *PPCI_REGISTRY_DEVICE;
#include <poppack.h>

/**
 * @brief
 * Retrieves the next PCI device on the specified bus,
 * and obtain its standard configuration information.
 *
 * @param[in]   BusNumber
 * The PCI bus number where to enumerate PCI devices.
 *
 * @param[in,out]   NextSlotNumber
 * In input, specifies the slot where to restart the enumeration.
 * In output, receives the next slot where enumeration can be restarted.
 *
 * @param[out]  PciConfig
 * Optional pointer to a PCI_COMMON_CONFIG buffer receiving
 * the PCI device configuration data.
 *
 * @param[out]  ConfigSize
 * Optional pointer to a ULONG that receives the actual
 * length of the retrieved configuration data.
 *
 * @return
 * The next PCI device slot number on the given bus, or -1 if none.
 *
 * @note    See SpiGetPciConfigData().
 **/
static
ULONG
GetNextPciDevice(
    _In_ ULONG BusNumber,
    _Inout_ PPCI_SLOT_NUMBER NextSlotNumber,
    _Out_opt_ PPCI_COMMON_CONFIG PciConfig,
    _Out_opt_ PULONG ConfigSize)
{
    ULONG DeviceNumber;
    ULONG FunctionNumber;
    PCI_COMMON_CONFIG PciData;
    ULONG DataSize;

    /* Loop through all devices */
    for (DeviceNumber = NextSlotNumber->u.bits.DeviceNumber;
         DeviceNumber < PCI_MAX_DEVICES;
         DeviceNumber++)
    {
        /* Loop through all functions */
        for (FunctionNumber = NextSlotNumber->u.bits.FunctionNumber;
             FunctionNumber < PCI_MAX_FUNCTION;
             FunctionNumber++)
        {
            PCI_SLOT_NUMBER SlotNumber = {0};
            SlotNumber.u.bits.DeviceNumber   = DeviceNumber;
            SlotNumber.u.bits.FunctionNumber = FunctionNumber;

            /* Retrieve PCI configuration data */
            ////RtlZeroMemory(&PciData, sizeof(PciData)); // TODO: Remove?
            DataSize = (PciConfig ? sizeof(PciData) : PCI_COMMON_HDR_LENGTH);
            DataSize = HalGetBusDataByOffset(PCIConfiguration,
                                             BusNumber,
                                             SlotNumber.u.AsULONG,
                                             &PciData,
                                             0,
                                             DataSize);

            /* If the returned size is 0, then the bus is wrong */
            if (DataSize == 0)
            {
                ERR("HalGetBusDataByOffset(%02x:%02x.%x) failed\n",
                    BusNumber, DeviceNumber, FunctionNumber);
                return -1;
            }

            /* If the result is PCI_INVALID_VENDORID, then this
             * device has no more "Functions" */
            if (PciData.VendorID == PCI_INVALID_VENDORID)
                break;

#if 0
            /* Print out the data */
            DbgPrint("%02x:%02x.%x [%02x%02x]: [%04x:%04x] (rev %02x)\n",
                     BusNumber, DeviceNumber, FunctionNumber,
                     PciData.BaseClass,
                     PciData.SubClass,
                     PciData.VendorID,
                     PciData.DeviceID,
                     PciData.RevisionID);

            if (PCI_CONFIGURATION_TYPE(&PciData) == PCI_DEVICE_TYPE)
            {
                DbgPrint("\tSubsystem: [%04x:%04x]\n",
                         PciData.u.type0.SubVendorID,
                         PciData.u.type0.SubSystemID);
            }
#endif

            /* Setup the device and function numbers for the next run */
            NextSlotNumber->u.bits.DeviceNumber   = DeviceNumber;
            NextSlotNumber->u.bits.FunctionNumber = FunctionNumber + 1;

            /* Return the PCI configuration data to the caller */
            if (PciConfig)
                RtlCopyMemory(PciConfig, &PciData, DataSize);
            if (ConfigSize)
                *ConfigSize = DataSize;

            /* And return the slot number of this valid device/function */
            SlotNumber.u.bits.Reserved = 1; // Set it as valid.
            return SlotNumber.u.AsULONG;
        }

        /* Go to the next device and reset the function number */
        NextSlotNumber->u.bits.FunctionNumber = 0;
    }

    /* We are done enumerating */
    NextSlotNumber->u.bits.DeviceNumber = 0;
    return -1;
}

VOID
DetectPciBus(
    _In_ PCONFIGURATION_COMPONENT_DATA SystemKey,
    _Inout_ PULONG BusNumber,
    _In_opt_ PCSTR Options,
    _In_ DETECT_PCI_BUS MachDetectPciBus)
{
    PCI_REGISTRY_INFO BusData;
    PCM_PARTIAL_RESOURCE_LIST PartialResourceList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptor;
    PCONFIGURATION_COMPONENT_DATA BusKey;
    ULONG Size;
    ULONG i;
    BOOLEAN PciEnum;

    /* Detect the PCI buses */
    if (!MachDetectPciBus(SystemKey, BusNumber, &BusData))
        return;

    /* Check whether to enumerate PCI devices during buses enumeration */
    PciEnum = !!NtLdrGetOption(Options, "PCIENUM");

    /* Report PCI buses */
    for (i = 0; i < (ULONG)BusData.NoBuses; i++)
    {
        /* Check if this is the first bus */
        if (i == 0)
        {
            /* Set 'Configuration Data' value */
            Size = FIELD_OFFSET(CM_PARTIAL_RESOURCE_LIST, PartialDescriptors[1]) +
                   sizeof(BusData);
            PartialResourceList = FrLdrHeapAlloc(Size, TAG_HW_RESOURCE_LIST);
            if (!PartialResourceList)
            {
                ERR("Failed to allocate resource descriptor! Ignoring remaining PCI buses (i = %lu, NoBuses = %lu)\n",
                    i, (ULONG)BusData.NoBuses);
                return;
            }

            /* Initialize resource descriptor */
            RtlZeroMemory(PartialResourceList, Size);
            PartialResourceList->Version = 1;
            PartialResourceList->Revision = 1;
            PartialResourceList->Count = 1;

            PartialDescriptor = &PartialResourceList->PartialDescriptors[0];
            PartialDescriptor->Type = CmResourceTypeDeviceSpecific;
            PartialDescriptor->ShareDisposition = CmResourceShareUndetermined;
            PartialDescriptor->u.DeviceSpecificData.DataSize = sizeof(BusData);

            RtlCopyMemory(&PartialResourceList->PartialDescriptors[1],
                          &BusData, sizeof(BusData));
        }
        else
        {
            /* Set 'Configuration Data' value */
            Size = FIELD_OFFSET(CM_PARTIAL_RESOURCE_LIST, PartialDescriptors);
            PartialResourceList = FrLdrHeapAlloc(Size, TAG_HW_RESOURCE_LIST);
            if (!PartialResourceList)
            {
                ERR("Failed to allocate resource descriptor! Ignoring remaining PCI buses (i = %lu, NoBuses = %lu)\n",
                    i, (ULONG)BusData.NoBuses);
                return;
            }

            /* Initialize resource descriptor */
            RtlZeroMemory(PartialResourceList, Size);
        }

        /* Create the bus key */
        FldrCreateComponentKey(SystemKey,
                               AdapterClass,
                               MultiFunctionAdapter,
                               0,
                               0,
                               0xFFFFFFFF,
                               "PCI",
                               PartialResourceList,
                               Size,
                               &BusKey);

        /* Increment bus number */
        (*BusNumber)++;

        if (PciEnum)
        {
            ULONG PciDevicesNumber, Device;
            ULONG DataSize;
            PPCI_REGISTRY_DEVICE PciDevice;
            PCONFIGURATION_COMPONENT_DATA DataKey;

            PCI_SLOT_NUMBER SlotNumber, NextSlotNumber;

            /* Count all the PCI devices on this bus */
            PciDevicesNumber = 0;
            NextSlotNumber.u.AsULONG = 0;
            while (GetNextPciDevice(i, &NextSlotNumber, NULL, NULL) != -1)
            {
                ++PciDevicesNumber;
            }
            DataSize = (PciDevicesNumber * sizeof(PCI_REGISTRY_DEVICE));

            /* Set 'Configuration Data' value */
            Size = FIELD_OFFSET(CM_PARTIAL_RESOURCE_LIST, PartialDescriptors[1]) + DataSize;
            PartialResourceList = FrLdrHeapAlloc(Size, TAG_HW_RESOURCE_LIST);
            if (!PartialResourceList)
            {
                /* Ignore since this is optional */
                continue;
            }

            /* Initialize resource descriptor */
            RtlZeroMemory(PartialResourceList, Size);
            PartialResourceList->Version = 1;
            PartialResourceList->Revision = 1;
            PartialResourceList->Count = 1;

            PartialDescriptor = &PartialResourceList->PartialDescriptors[0];
            PartialDescriptor->Type = CmResourceTypeDeviceSpecific;
            PartialDescriptor->ShareDisposition = CmResourceShareUndetermined;
            PartialDescriptor->u.DeviceSpecificData.DataSize = DataSize;

            /* Get pointer to PCI device data */
            PciDevice = (PPCI_REGISTRY_DEVICE)(PartialDescriptor + 1);

            /* Retrieve all the devices data on this PCI bus */
            Device = 0;
            NextSlotNumber.u.AsULONG = 0;
            while ((Device < PciDevicesNumber) &&
                   (SlotNumber.u.AsULONG = GetNextPciDevice(i, &NextSlotNumber,
                                                            &PciDevice->PciConfig, NULL)) != -1)
            {
                PciDevice->Address.bits.BusNumber = i;
                PciDevice->Address.bits.DeviceNumber   = SlotNumber.u.bits.DeviceNumber;
                PciDevice->Address.bits.FunctionNumber = SlotNumber.u.bits.FunctionNumber;
                ++PciDevice;
                ++Device;
            }

            /* Create the PCI devices configuration enumeration key */
            FldrCreateComponentKey(BusKey,
                                   PeripheralClass,
                                   RealModePCIEnumeration,
                                   0,
                                   0,
                                   0xFFFFFFFF,
                                   "PCI Devices",
                                   PartialResourceList,
                                   Size,
                                   &DataKey);
        }
    }
}

/* EOF */
