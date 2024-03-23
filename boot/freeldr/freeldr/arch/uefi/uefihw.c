/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Hardware detection routines
 * COPYRIGHT:   Copyright 2022 Justin Miller <justinmiller100@gmail.com>
 */

/* INCLUDES ******************************************************************/

#include <uefildr.h>
#include <drivers/bootvid/framebuf.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(WARNING);

/* GLOBALS *******************************************************************/

extern EFI_SYSTEM_TABLE * GlobalSystemTable;
extern EFI_HANDLE GlobalImageHandle;
extern UCHAR PcBiosDiskCount;
extern EFI_MEMORY_DESCRIPTOR* EfiMemoryMap;
extern UINT32 FreeldrDescCount;

BOOLEAN AcpiPresent = FALSE;

/* FUNCTIONS *****************************************************************/

static
PRSDP_DESCRIPTOR
FindAcpiBios(VOID)
{
    UINTN i;
    RSDP_DESCRIPTOR* rsdp = NULL;
    EFI_GUID acpi2_guid = EFI_ACPI_20_TABLE_GUID;

    for (i = 0; i < GlobalSystemTable->NumberOfTableEntries; i++)
    {
        if (!memcmp(&GlobalSystemTable->ConfigurationTable[i].VendorGuid,
                    &acpi2_guid, sizeof(acpi2_guid)))
        {
            rsdp = (RSDP_DESCRIPTOR*)GlobalSystemTable->ConfigurationTable[i].VendorTable;
            break;
        }
    }

    return rsdp;
}

VOID
DetectAcpiBios(PCONFIGURATION_COMPONENT_DATA SystemKey, ULONG *BusNumber)
{
    PCONFIGURATION_COMPONENT_DATA BiosKey;
    PCM_PARTIAL_RESOURCE_LIST PartialResourceList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptor;
    PRSDP_DESCRIPTOR Rsdp;
    PACPI_BIOS_DATA AcpiBiosData;
    ULONG TableSize;

    Rsdp = FindAcpiBios();

    if (Rsdp)
    {
        /* Set up the flag in the loader block */
        AcpiPresent = TRUE;

        /* Calculate the table size */
        TableSize = FreeldrDescCount * sizeof(BIOS_MEMORY_MAP) +
            sizeof(ACPI_BIOS_DATA) - sizeof(BIOS_MEMORY_MAP);

        /* Set 'Configuration Data' value */
        PartialResourceList = FrLdrHeapAlloc(sizeof(CM_PARTIAL_RESOURCE_LIST) +
                                             TableSize, TAG_HW_RESOURCE_LIST);
        if (PartialResourceList == NULL)
        {
            ERR("Failed to allocate resource descriptor\n");
            return;
        }

        RtlZeroMemory(PartialResourceList, sizeof(CM_PARTIAL_RESOURCE_LIST) + TableSize);
        PartialResourceList->Version = 0;
        PartialResourceList->Revision = 0;
        PartialResourceList->Count = 1;

        PartialDescriptor = &PartialResourceList->PartialDescriptors[0];
        PartialDescriptor->Type = CmResourceTypeDeviceSpecific;
        PartialDescriptor->ShareDisposition = CmResourceShareUndetermined;
        PartialDescriptor->u.DeviceSpecificData.DataSize = TableSize;

        /* Fill the table */
        AcpiBiosData = (PACPI_BIOS_DATA)&PartialResourceList->PartialDescriptors[1];

        if (Rsdp->revision > 0)
        {
            TRACE("ACPI >1.0, using XSDT address\n");
            AcpiBiosData->RSDTAddress.QuadPart = Rsdp->xsdt_physical_address;
        }
        else
        {
            TRACE("ACPI 1.0, using RSDT address\n");
            AcpiBiosData->RSDTAddress.LowPart = Rsdp->rsdt_physical_address;
        }

        AcpiBiosData->Count = FreeldrDescCount;
        memcpy(AcpiBiosData->MemoryMap, EfiMemoryMap,
            FreeldrDescCount * sizeof(BIOS_MEMORY_MAP));

        TRACE("RSDT %p, data size %x\n", Rsdp->rsdt_physical_address, TableSize);

        /* Create new bus key */
        FldrCreateComponentKey(SystemKey,
                               AdapterClass,
                               MultiFunctionAdapter,
                               0x0,
                               0x0,
                               0xFFFFFFFF,
                               "ACPI BIOS",
                               PartialResourceList,
                               sizeof(CM_PARTIAL_RESOURCE_LIST) + TableSize,
                               &BiosKey);

        /* Increment bus number */
        (*BusNumber)++;
    }
}

extern REACTOS_INTERNAL_BGCONTEXT framebufferData;
/****/extern EFI_PIXEL_BITMASK UefiGopPixelBitmask;/****/

static VOID
DetectDisplayController(PCONFIGURATION_COMPONENT_DATA BusKey)
{
    PCONFIGURATION_COMPONENT_DATA ControllerKey;
    PCM_PARTIAL_RESOURCE_LIST PartialResourceList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptor;
    PCM_FRAMEBUF_DEVICE_DATA FramebufferData;
    ULONG Size;

    if (framebufferData.BufferSize == 0)
        return;

    ERR("\nStructure sizes:\n"
        "    sizeof(CM_PARTIAL_RESOURCE_LIST)       = %lu\n"
        "    sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR) = %lu\n"
        "    sizeof(CM_FRAMEBUF_DEVICE_DATA)        = %lu\n\n",
        sizeof(CM_PARTIAL_RESOURCE_LIST),
        sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR),
        sizeof(CM_FRAMEBUF_DEVICE_DATA));

    Size = sizeof(CM_PARTIAL_RESOURCE_LIST) +
           sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR) +
           sizeof(CM_FRAMEBUF_DEVICE_DATA);
    PartialResourceList = FrLdrHeapAlloc(Size, TAG_HW_RESOURCE_LIST);
    if (PartialResourceList == NULL)
    {
        ERR("Failed to allocate resource descriptor\n");
        return;
    }

    /* Initialize resource descriptor */
    RtlZeroMemory(PartialResourceList, Size);
    PartialResourceList->Version  = ARC_VERSION;
    PartialResourceList->Revision = ARC_REVISION;
    PartialResourceList->Count = 2;

    /* Set Memory */
    PartialDescriptor = &PartialResourceList->PartialDescriptors[0];
    PartialDescriptor->Type = CmResourceTypeMemory;
    PartialDescriptor->ShareDisposition = CmResourceShareDeviceExclusive;
    PartialDescriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
    PartialDescriptor->u.Memory.Start.QuadPart = framebufferData.BaseAddress;
    PartialDescriptor->u.Memory.Length = framebufferData.BufferSize;

    /* Set framebuffer-specific data */
    PartialDescriptor = &PartialResourceList->PartialDescriptors[1];
    PartialDescriptor->Type = CmResourceTypeDeviceSpecific;
    PartialDescriptor->ShareDisposition = CmResourceShareUndetermined;
    PartialDescriptor->Flags = 0;
    PartialDescriptor->u.DeviceSpecificData.DataSize =
        sizeof(CM_FRAMEBUF_DEVICE_DATA);

    /* Get pointer to framebuffer-specific data */
    FramebufferData = (PVOID)(PartialDescriptor + 1);
    RtlZeroMemory(FramebufferData, sizeof(*FramebufferData));
    FramebufferData->Version  = 2;
    FramebufferData->Revision = 0;

    FramebufferData->VideoClock = 0; // FIXME: Use EDID

    /* Horizontal and Vertical resolution in pixels */
    FramebufferData->ScreenWidth  = framebufferData.ScreenWidth;
    FramebufferData->ScreenHeight = framebufferData.ScreenHeight;

    /* Number of pixel elements per video memory line */
    FramebufferData->PixelsPerScanLine = framebufferData.PixelsPerScanLine;

    //
    // TODO: Investigate display rotation!
    //
    // See OpenCorePkg OcConsoleLib/ConsoleGop.c
    // if ((mGop.Rotation == 90) || (mGop.Rotation == 270))
    if (FramebufferData->ScreenWidth < FramebufferData->ScreenHeight)
    {
        #define SWAP(x, y) { (x) ^= (y); (y) ^= (x); (x) ^= (y); }
        SWAP(FramebufferData->ScreenWidth, FramebufferData->ScreenHeight);
        FramebufferData->PixelsPerScanLine = FramebufferData->ScreenWidth;
        #undef SWAP
    }

    /* Physical format of the pixel */
    switch (framebufferData.PixelFormat)
    {
        case PixelRedGreenBlueReserved8BitPerColor:
        case PixelBlueGreenRedReserved8BitPerColor:
        {
            FramebufferData->BitsPerPixel = (8 * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
            *(EFI_PIXEL_BITMASK*)&FramebufferData->PixelInformation = EfiPixelMasks[framebufferData.PixelFormat];
            break;
        }

        case PixelBitMask:
        {
            /*
             * When the GOP pixel format is given by PixelBitMask, the pixel
             * element size _may be_ different from 4 bytes.
             * See UEFI Spec Rev.2.10 Section 12.9 "Graphics Output Protocol":
             * example code "GetPixelElementSize()" function.
             */
            FramebufferData->BitsPerPixel =
                PixelBitmasksToBpp(UefiGopPixelBitmask.RedMask,
                                   UefiGopPixelBitmask.GreenMask,
                                   UefiGopPixelBitmask.BlueMask,
                                   UefiGopPixelBitmask.ReservedMask);
            *(EFI_PIXEL_BITMASK*)&FramebufferData->PixelInformation = UefiGopPixelBitmask;
            // FramebufferData->PixelInformation.RedMask      = UefiGopPixelBitmask.RedMask;
            // FramebufferData->PixelInformation.GreenMask    = UefiGopPixelBitmask.GreenMask;
            // FramebufferData->PixelInformation.BlueMask     = UefiGopPixelBitmask.BlueMask;
            // FramebufferData->PixelInformation.ReservedMask = UefiGopPixelBitmask.ReservedMask;
            break;
        }

        case PixelBltOnly:
        default:
        {
            ERR("Unsupported UFEI GOP format %lu\n", framebufferData.PixelFormat);
            FramebufferData->BitsPerPixel = 0;
            RtlZeroMemory(&FramebufferData->PixelInformation,
                          sizeof(FramebufferData->PixelInformation));
            break;
        }
    }

    FldrCreateComponentKey(BusKey,
                           ControllerClass,
                           DisplayController,
                           Output | ConsoleOut,
                           0,
                           0xFFFFFFFF,
                           "UEFI GOP Framebuffer",
                           PartialResourceList,
                           Size,
                           &ControllerKey);

    // NOTE: Don't add a MonitorPeripheral for now...
    // We should use EDID data for it.
}

static
VOID
DetectInternal(PCONFIGURATION_COMPONENT_DATA SystemKey, ULONG *BusNumber)
{
    PCM_PARTIAL_RESOURCE_LIST PartialResourceList;
    PCONFIGURATION_COMPONENT_DATA BusKey;
    ULONG Size;

    /* Set 'Configuration Data' value */
    Size = sizeof(CM_PARTIAL_RESOURCE_LIST) -
           sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);
    PartialResourceList = FrLdrHeapAlloc(Size, TAG_HW_RESOURCE_LIST);
    if (PartialResourceList == NULL)
    {
        ERR("Failed to allocate resource descriptor\n");
        return;
    }

    /* Initialize resource descriptor */
    RtlZeroMemory(PartialResourceList, Size);
    PartialResourceList->Version  = ARC_VERSION;
    PartialResourceList->Revision = ARC_REVISION;
    PartialResourceList->Count = 0;

    /* Create new bus key */
    FldrCreateComponentKey(SystemKey,
                           AdapterClass,
                           MultiFunctionAdapter,
                           0,
                           0,
                           0xFFFFFFFF,
                           "UEFI Internal",
                           PartialResourceList,
                           Size,
                           &BusKey);

    /* Increment bus number */
    (*BusNumber)++;

    /* Detect devices that do not belong to "standard" buses */
    DetectDisplayController(BusKey);

    /* FIXME: Detect more devices */
}

PCONFIGURATION_COMPONENT_DATA
UefiHwDetect(
    _In_opt_ PCSTR Options)
{
    PCONFIGURATION_COMPONENT_DATA SystemKey;
    ULONG BusNumber = 0;

    TRACE("DetectHardware()\n");

    /* Create the 'System' key */
#if defined(_M_IX86) || defined(_M_AMD64)
    FldrCreateSystemKey(&SystemKey, FALSE, "AT/AT COMPATIBLE");
#elif defined(_M_IA64)
    FldrCreateSystemKey(&SystemKey, FALSE, "Intel Itanium processor family");
#elif defined(_M_ARM) || defined(_M_ARM64)
    FldrCreateSystemKey(&SystemKey, FALSE, "ARM processor family");
#else
    #error Please define a system key for your architecture
#endif

    DetectInternal(SystemKey, &BusNumber);
    /* Detect ACPI */
    DetectAcpiBios(SystemKey, &BusNumber);

    TRACE("DetectHardware() Done\n");
    return SystemKey;
}
