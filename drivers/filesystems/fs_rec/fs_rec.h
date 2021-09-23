/*
 * PROJECT:     ReactOS File System Recognizer
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Main Header File
 * COPYRIGHT:   Copyright 2002 Eric Kohl
 *              Copyright 2007 Alex Ionescu (alex.ionescu@reactos.org)
 */

#ifndef _FS_REC_H
#define _FS_REC_H

#include <ntifs.h>

/* Tag for memory allocations */
#define FSREC_TAG 'cRsF'

/* Non-standard rounding macros */
#ifndef ROUND_UP
#define ROUND_UP(n, align) \
    ROUND_DOWN(((ULONG)n) + (align) - 1, (align))

#define ROUND_DOWN(n, align) \
    (((ULONG)n) & ~((align) - 1l))
#endif

/* Filesystem Types */
typedef enum _FILE_SYSTEM_TYPE
{
    FS_TYPE_UNUSED,
    FS_TYPE_VFAT,
    FS_TYPE_NTFS,
    FS_TYPE_CDFS,
    FS_TYPE_UDFS,
    FS_TYPE_EXT2,
    FS_TYPE_BTRFS,
    FS_TYPE_REISERFS,
    FS_TYPE_FFS,
} FILE_SYSTEM_TYPE, *PFILE_SYSTEM_TYPE;

/* FS Recognizer State */
typedef enum _FS_REC_STATE
{
    Pending,
    Loaded,
    Unloading
} FS_REC_STATE, *PFS_REC_STATE;

/* Device extension */
typedef struct _DEVICE_EXTENSION
{
    FS_REC_STATE State;
    FILE_SYSTEM_TYPE FsType;
    PDEVICE_OBJECT Alternate;
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

/* Prototypes */
NTSTATUS
NTAPI
FsRecCdfsFsControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
);

NTSTATUS
NTAPI
FsRecVfatFsControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
);

NTSTATUS
NTAPI
FsRecNtfsFsControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
);

NTSTATUS
NTAPI
FsRecUdfsFsControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
);

NTSTATUS
NTAPI
FsRecExt2FsControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
);

NTSTATUS
NTAPI
FsRecBtrfsFsControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
);

NTSTATUS
NTAPI
FsRecReiserfsFsControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
);

NTSTATUS
NTAPI
FsRecFfsFsControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
);

BOOLEAN
NTAPI
FsRecGetDeviceSectors(
    IN PDEVICE_OBJECT DeviceObject,
    IN ULONG SectorSize,
    OUT PLARGE_INTEGER SectorCount
);

BOOLEAN
NTAPI
FsRecGetDeviceSectorSize(
    IN PDEVICE_OBJECT DeviceObject,
    OUT PULONG SectorSize
);

BOOLEAN
NTAPI
FsRecReadBlock(
    IN PDEVICE_OBJECT DeviceObject,
    IN PLARGE_INTEGER Offset,
    IN ULONG Length,
    IN ULONG SectorSize,
    IN OUT PVOID* Buffer,
    OUT PBOOLEAN DeviceError OPTIONAL
);

NTSTATUS
NTAPI
FsRecLoadFileSystem(
    IN PDEVICE_OBJECT DeviceObject,
    IN PWCHAR DriverServiceName
);

#endif /* _FS_REC_H */

/* EOF */
