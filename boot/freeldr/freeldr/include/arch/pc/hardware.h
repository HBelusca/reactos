/*
 *  FreeLoader
 *
 *  Copyright (C) 2003  Eric Kohl
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#pragma once

#define CONFIG_CMD(bus, dev_fn, where) \
    (0x80000000 | (((ULONG)(bus)) << 16) | (((dev_fn) & 0x1F) << 11) | (((dev_fn) & 0xE0) << 3) | ((where) & ~3))

#define TAG_HW_RESOURCE_LIST    'lRwH'
#define TAG_HW_DISK_CONTEXT     'cDwH'

/* GLOBALS *******************************************************************/

extern PBOOT_CONTEXT PcBootContext;
#define FrldrBootDrive      ((UCHAR)((PcBootContext)->BootDrive))
#define FrldrBootPartition  ((ULONG)((PcBootContext)->BootPartition))

/* PROTOTYPES ****************************************************************/

/* hardware.c */
VOID StallExecutionProcessor(ULONG Microseconds);
VOID HalpCalibrateStallExecution(VOID);

typedef
PCM_PARTIAL_RESOURCE_LIST
(*GET_HARDDISK_CONFIG_DATA)(UCHAR DriveNumber, ULONG* pSize);

extern GET_HARDDISK_CONFIG_DATA GetHarddiskConfigurationData;

typedef
BOOLEAN
(*FIND_PCI_BIOS)(PPCI_REGISTRY_INFO BusData);

extern FIND_PCI_BIOS FindPciBios;

typedef
ULONG
(*GET_SERIAL_PORT)(ULONG Index, PULONG Irq);

VOID
DetectBiosDisks(PCONFIGURATION_COMPONENT_DATA SystemKey,
                PCONFIGURATION_COMPONENT_DATA BusKey);

/* hwacpi.c */
VOID DetectAcpiBios(PCONFIGURATION_COMPONENT_DATA SystemKey, ULONG *BusNumber);

/* hwapm.c */
VOID DetectApmBios(PCONFIGURATION_COMPONENT_DATA SystemKey, ULONG *BusNumber);

/* hwpci.c */
VOID DetectPciBios(PCONFIGURATION_COMPONENT_DATA SystemKey, ULONG *BusNumber);

/* i386pnp.S */
// ULONG_PTR __cdecl PnpBiosSupported(VOID);
#define PnpBiosSupported()  \
    ((PcBootContext)->ServicesTable->PnpBiosSupported)()
// ULONG __cdecl PnpBiosGetDeviceNodeCount(OUT PULONG NodeSize, OUT PULONG NodeCount);
#define PnpBiosGetDeviceNodeCount(NodeSize, NodeCount)  \
    ((PcBootContext)->ServicesTable->PnpBiosGetDeviceNodeCount)((NodeSize), (NodeCount))
// ULONG __cdecl PnpBiosGetDeviceNode(IN OUT PUCHAR NodeId, OUT PUCHAR NodeBuffer);
#define PnpBiosGetDeviceNode(NodeId, NodeBuffer)        \
    ((PcBootContext)->ServicesTable->PnpBiosGetDeviceNode)((NodeId), (NodeBuffer))

/* i386pxe.S */
// extern PXENV_EXIT __cdecl PxeCallApi(UINT16 Segment, UINT16 Offset, UINT16 Service, VOID *Parameter);
// USHORT __cdecl PxeCallApi(IN USHORT Segment, IN USHORT Offset, IN USHORT Service, IN PVOID Parameter);
#define PxeCallApi(Segment, Offset, Service, Parameter) \
    ((PcBootContext)->ServicesTable->PxeCallApi)((Segment), (Offset), (Service), (Parameter))

/* EOF */
