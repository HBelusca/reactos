/*
 * PROJECT:     ReactOS Setup Library
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     ARC path to-and-from NT path resolver.
 * COPYRIGHT:   Copyright 2017-2020 Hermes Belusca-Maito
 */

#pragma once

/* See also UEFI specification - Media Device (UEFI Specs v2.8: 10.3.5.1 Hard Drive) */
typedef enum _DEVICE_SIGNATURE_TYPE
{
    SignatureNone = 0x00,
    SignatureLong = 0x01,
    SignatureGuid = 0x02
} DEVICE_SIGNATURE_TYPE, *PDEVICE_SIGNATURE_TYPE;

typedef struct _DEVICE_SIGNATURE
{
    DEVICE_SIGNATURE_TYPE Type;
    union
    {
        ULONG Long;
        GUID  Guid;
    }
} DEVICE_SIGNATURE, *PDEVICE_SIGNATURE;

#if 0
BOOLEAN
ArcPathNormalize(
    OUT PUNICODE_STRING NormalizedArcPath,
    IN  PCWSTR ArcPath);
#endif

BOOLEAN
ArcPathToNtPath(
    OUT PUNICODE_STRING NtPath,
    IN  PCWSTR ArcPath,
    IN  PPARTLIST PartList OPTIONAL);

/* EOF */
