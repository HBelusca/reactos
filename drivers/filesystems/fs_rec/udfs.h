/*
 * PROJECT:     ReactOS File System Recognizer
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     UDFS Recognizer Header File
 * COPYRIGHT:   Copyright 2009 Dmitry Chapyshev (dmitry@reactos.org)
 */

/* Standard Identifier (EMCA 167r2 2/9.1.2) */
#define VSD_STD_ID_NSR02    "NSR02"    /* (3/9.1) */

/* Standard Identifier (ECMA 167r3 2/9.1.2) */
#define VSD_STD_ID_BEA01    "BEA01"    /* (2/9.2) */
#define VSD_STD_ID_BOOT2    "BOOT2"    /* (2/9.4) */
#define VSD_STD_ID_CD001    "CD001"    /* (ECMA-119) */
#define VSD_STD_ID_CDW02    "CDW02"    /* (ECMA-168) */
#define VSD_STD_ID_NSR03    "NSR03"    /* (3/9.1) */
#define VSD_STD_ID_TEA01    "TEA01"    /* (2/9.3) */

/* Volume Structure Descriptor (ECMA 167r3 2/9.1) */
#define VSD_STD_ID_LEN 5
typedef struct _VOLSTRUCTDESC
{
    UCHAR Type;
    UCHAR Ident[VSD_STD_ID_LEN];
    UCHAR Version;
    UCHAR Data[2041];
} VOLSTRUCTDESC, *PVOLSTRUCTDESC;


/* UDFS Offsets */
#define UDFS_VRS_START_OFFSET  32768
#define UDFS_AVDP_SECTOR       256

#include <pshpack1.h>
typedef struct _TAG
{
    USHORT Identifier;
    USHORT Version;
    UCHAR  Checksum;
    UCHAR  Reserved;
    USHORT SerialNumber;
    USHORT Crc;
    USHORT CrcLength;
    ULONG  Location;
} TAG, *PTAG;

typedef struct _EXTENT
{
    ULONG Length;
    ULONG Location;
} EXTENT, *PEXTENT;

typedef struct _AVDP
{
    TAG DescriptorTag;
    EXTENT MainVolumeDescriptorExtent;
    EXTENT ReserveVolumeDescriptorExtent;
} AVDP, *PAVDP;
#include <poppack.h>

/* EOF */
