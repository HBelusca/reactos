/*
 * PROJECT:         ReactOS Boot Loader (FreeLDR)
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            boot/freeldr/freeldr/arch/archwsup.c
 * PURPOSE:         Routines for ARC Hardware Tree and Configuration Data
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES *******************************************************************/

#include <freeldr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(HWDETECT);

/* GLOBALS ********************************************************************/

/* Strings corresponding to CONFIGURATION_TYPE */
const PCSTR ArcTypes[MaximumType + 1] = // CmTypeName
{
    "System",
    "CentralProcessor",
    "FloatingPointProcessor",
    "PrimaryICache",
    "PrimaryDCache",
    "SecondaryICache",
    "SecondaryDCache",
    "SecondaryCache",
/** Adapters */
    "EisaAdapter",
    "TcAdapter",
    "ScsiAdapter",
    "DtiAdapter",
    "MultifunctionAdapter",
/** Controllers */
    "DiskController",
    "TapeController",
    "CdRomController",
    "WormController",
    "SerialController",
    "NetworkController",
    "DisplayController",
    "ParallelController",
    "PointerController",
    "KeyboardController",
    "AudioController",
    "OtherController",
/** Peripherals */
    "DiskPeripheral",
    "FloppyDiskPeripheral",
    "TapePeripheral",
    "ModemPeripheral",
    "MonitorPeripheral",
    "PrinterPeripheral",
    "PointerPeripheral",
    "KeyboardPeripheral",
    "TerminalPeripheral",
    "OtherPeripheral",
    "LinePeripheral",
    "NetworkPeripheral",
    "SystemMemory",
    "DockingInformation",
    "RealModeIrqRoutingTable",
    "RealModePCIEnumeration",
    "Undefined"
};

/* Mnemonics corresponding to CONFIGURATION_TYPE */
const PCSTR Mnemonics[MaximumType + 1] =
{
    "arc",
    "cpu",
    "fpu",
    "pic",
    "pdc",
    "sic",
    "sdc",
    "sc",
/** Adapters */
    "eisa",
    "tc",
    "scsi",
    "dti",
    "multi",
/** Controllers */
    "disk",
    "tape",
    "cdrom",
    "worm",
    "serial",
    "net",
    "video",
    "par",
    "point",
    "key",
    "audio",
    "other",
/** Peripherals */
    "rdisk",
    "fdisk",
    "tape",
    "modem",
    "monitor",
    "print",
    "pointer",
    "keyboard",
    "term",
    "other",
    "line",
    "network",
/** Others */
    "mem",  // SystemMemory
    "dock", // DockingInformation
    "irq",  // RealModeIrqRoutingTable
    "pci",  // RealModePCIEnumeration
    "???"   // ""      // Unknown
};

PCONFIGURATION_COMPONENT_DATA FldrArcHwTreeRoot;

/* FUNCTIONS ******************************************************************/

#define TAG_HW_COMPONENT_DATA   'DCwH'
#define TAG_HW_NAME             'mNwH'

//
// ARC Component Configuration Routines
//

static VOID
FldrSetIdentifier(
    _In_ PCONFIGURATION_COMPONENT Component,
    _In_ PCSTR IdentifierString)
{
    SIZE_T IdentifierLength;
    PCHAR Identifier;

    /* Allocate memory for the identifier */
    IdentifierLength = strlen(IdentifierString) + 1;
    Identifier = FrLdrHeapAlloc(IdentifierLength, TAG_HW_NAME);
    if (!Identifier) return;

    /* Copy the identifier */
    RtlCopyMemory(Identifier, IdentifierString, IdentifierLength);

    /* Set component information */
    Component->IdentifierLength = (ULONG)IdentifierLength;
    Component->Identifier = Identifier;
}

VOID
FldrSetConfigurationData(
    _Inout_ PCONFIGURATION_COMPONENT_DATA ComponentData,
    _In_ PCM_PARTIAL_RESOURCE_LIST ResourceList,
    _In_ ULONG Size)
{
    /* Set component information */
    ComponentData->ConfigurationData = ResourceList;
    ComponentData->ComponentEntry.ConfigurationDataLength = Size;
}

VOID
FldrCreateSystemKey(
    _Out_ PCONFIGURATION_COMPONENT_DATA* SystemNode,
    _In_ PCSTR IdentifierString)
{
    PCONFIGURATION_COMPONENT Component;

    /* Allocate the root */
    FldrArcHwTreeRoot = FrLdrHeapAlloc(sizeof(CONFIGURATION_COMPONENT_DATA),
                                       TAG_HW_COMPONENT_DATA);
    if (!FldrArcHwTreeRoot) return;

    /* Set it up */
    Component = &FldrArcHwTreeRoot->ComponentEntry;
    Component->Class = SystemClass;
    Component->Type = MaximumType;
    Component->ConfigurationDataLength = 0;
    Component->Identifier = 0;
    Component->IdentifierLength = 0;
    Component->Flags = 0;
    Component->Version = 0;
    Component->Revision = 0;
    Component->Key = 0;
    Component->AffinityMask = 0xFFFFFFFF;

    /* Set identifier */
    if (IdentifierString)
        FldrSetIdentifier(Component, IdentifierString);

    /* Return the node */
    *SystemNode = FldrArcHwTreeRoot;
}

static VOID
FldrLinkToParent(
    _In_ PCONFIGURATION_COMPONENT_DATA Parent,
    _In_ PCONFIGURATION_COMPONENT_DATA Child)
{
    PCONFIGURATION_COMPONENT_DATA Sibling;

    /* Get the first sibling */
    Sibling = Parent->Child;

    /* If no sibling exists, then we are the first child */
    if (!Sibling)
    {
        /* Link us in */
        Parent->Child = Child;
    }
    else
    {
        /* Loop each sibling */
        do
        {
            /* This is now the parent */
            Parent = Sibling;
        } while ((Sibling = Sibling->Sibling));

        /* Found the lowest sibling; mark us as its sibling too */
        Parent->Sibling = Child;
    }
}

VOID
FldrCreateComponentKey(
    _In_ PCONFIGURATION_COMPONENT_DATA SystemNode,
    _In_ CONFIGURATION_CLASS Class,
    _In_ CONFIGURATION_TYPE Type,
    _In_ IDENTIFIER_FLAG Flags,
    _In_ ULONG Key,
    _In_ ULONG Affinity,
    _In_ PCSTR IdentifierString,
    _In_ PCM_PARTIAL_RESOURCE_LIST ResourceList,
    _In_ ULONG Size,
    _Out_ PCONFIGURATION_COMPONENT_DATA* ComponentKey)
{
    PCONFIGURATION_COMPONENT_DATA ComponentData;
    PCONFIGURATION_COMPONENT Component;

    /* Allocate the node for this component */
    ComponentData = FrLdrHeapAlloc(sizeof(CONFIGURATION_COMPONENT_DATA),
                                   TAG_HW_COMPONENT_DATA);
    if (!ComponentData) return;

    /* Now save our parent */
    ComponentData->Parent = SystemNode;

    /* Link us to the parent */
    if (SystemNode)
        FldrLinkToParent(SystemNode, ComponentData);

    /* Set us up */
    Component = &ComponentData->ComponentEntry;
    Component->Class = Class;
    Component->Type = Type;
    Component->Flags = Flags;
    Component->Key = Key;
    Component->AffinityMask = Affinity;

    /* Set identifier */
    if (IdentifierString)
        FldrSetIdentifier(Component, IdentifierString);

    /* Set configuration data */
    if (ResourceList)
        FldrSetConfigurationData(ComponentData, ResourceList, Size);

    TRACE("Created key: %s\\%d\n", ArcTypes[min(Type, MaximumType)], Key);

    /* Return the child */
    *ComponentKey = ComponentData;
}


static BOOLEAN
FldrGetComponentPathWorker(
    _In_ PCONFIGURATION_COMPONENT_DATA Component,
    _Inout_ PCHAR* pPath,
    _Inout_ PSIZE_T pLength)
{
    if (Component->Parent)
    {
        NTSTATUS Status;
        if (!FldrGetComponentPathWorker(Component->Parent, pPath, pLength))
            return FALSE;
        Status = RtlStringCbPrintfExA(*pPath, *pLength, pPath, pLength, 0,
                                      "%s(%lu)",
                                      Mnemonics[Component->ComponentEntry.Type],
                                      Component->ComponentEntry.Key);
        if (Status != STATUS_SUCCESS)
            return FALSE;
    }
    else
    {
        /* Parent is NULL, so we are at the top System node: don't print the name */
        if (*pLength > 0)
            **pPath = ANSI_NULL;
    }
    return TRUE;
}

BOOLEAN
FldrGetComponentPath(
    _In_ PCONFIGURATION_COMPONENT_DATA Component,
    _Out_writes_z_(Length) PSTR Path,
    _In_ SIZE_T Length)
{
#if DBG
    PCONFIGURATION_COMPONENT_DATA Current;
    for (Current = Component; Current != NULL; Current = Current->Parent)
    {
        /*TRACE*/ERR("%s(%lu) : Device 0x%p class %d type %d id '%s', parent %p\n",
              Mnemonics[min(Current->ComponentEntry.Type, MaximumType)],
              Current->ComponentEntry.Key,
              Current, Current->ComponentEntry.Class, Current->ComponentEntry.Type,
              VaToPa(Current->ComponentEntry.Identifier), Current->Parent);
    }
#endif
    return FldrGetComponentPathWorker(Component, &Path, &Length);
}
