/*
 * PROJECT:     ReactOS File System Recognizer
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     BTRFS Recognizer Header File
 * COPYRIGHT:   Copyright 2016 Peter Hater
 *              Copyright 2016 Pierre Schweitzer (pierre@reactos.org)
 */

#include <pshpack1.h>
typedef struct {
    UINT8 uuid[16];
} BTRFS_UUID;

typedef struct _BTRFS_SUPER_BLOCK {
    UINT8 checksum[32];
    BTRFS_UUID uuid;
    UINT64 sb_phys_addr;
    UINT64 flags;
    UINT64 magic;
        // Partial
} BTRFS_SUPER_BLOCK, *PBTRFS_SUPER_BLOCK;
#include <poppack.h>

C_ASSERT(FIELD_OFFSET(BTRFS_SUPER_BLOCK, uuid) == 0x20);
C_ASSERT(FIELD_OFFSET(BTRFS_SUPER_BLOCK, sb_phys_addr) == 0x30);
C_ASSERT(FIELD_OFFSET(BTRFS_SUPER_BLOCK, magic) == 0x40);

#define BTRFS_MAGIC 0x4d5f53665248425f
#define BTRFS_SB_OFFSET 0x10000
#define BTRFS_SB_SIZE 0x1000

/* EOF */
