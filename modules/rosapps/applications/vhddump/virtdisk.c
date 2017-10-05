
// ApplySnapshotVhdSet
// AddVirtualDiskParent

DWORD
WINAPI
AttachVirtualDisk(
    _In_     HANDLE                          VirtualDiskHandle,
    _In_opt_ PSECURITY_DESCRIPTOR            SecurityDescriptor,
    _In_     ATTACH_VIRTUAL_DISK_FLAG        Flags,
    _In_     ULONG                           ProviderSpecificFlags,
    _In_opt_ PATTACH_VIRTUAL_DISK_PARAMETERS Parameters,
    _In_opt_ LPOVERLAPPED                    Overlapped
)
{
}

// BreakMirrorVirtualDisk

DWORD
WINAPI
CompactVirtualDisk(
    _In_     HANDLE                           VirtualDiskHandle,
    _In_     COMPACT_VIRTUAL_DISK_FLAG        Flags,
    _In_opt_ PCOMPACT_VIRTUAL_DISK_PARAMETERS Parameters,
    _In_opt_ LPOVERLAPPED                     Overlapped
)
{
}

DWORD
WINAPI
CreateVirtualDisk(
    _In_     PVIRTUAL_STORAGE_TYPE           VirtualStorageType,
    _In_     PCWSTR                          Path,
    _In_     VIRTUAL_DISK_ACCESS_MASK        VirtualDiskAccessMask,
    _In_opt_ PSECURITY_DESCRIPTOR            SecurityDescriptor,
    _In_     CREATE_VIRTUAL_DISK_FLAG        Flags,
    _In_     ULONG                           ProviderSpecificFlags,
    _In_     PCREATE_VIRTUAL_DISK_PARAMETERS Parameters,
    _In_opt_ LPOVERLAPPED                    Overlapped,
    _Out_    PHANDLE                         Handle
)
{
}

// DeleteSnapshotVhdSet
// DeleteVirtualDiskMetadata

DWORD
WINAPI
DetachVirtualDisk(
    _In_ HANDLE                   VirtualDiskHandle,
    _In_ DETACH_VIRTUAL_DISK_FLAG Flags,
    _In_ ULONG                    ProviderSpecificFlags
)
{
}

// EnumerateVirtualDiskMetadata

DWORD
WINAPI
ExpandVirtualDisk(
    _In_     HANDLE                          VirtualDiskHandle,
    _In_     EXPAND_VIRTUAL_DISK_FLAG        Flags,
    _In_     PEXPAND_VIRTUAL_DISK_PARAMETERS Parameters,
    _In_opt_ LPOVERLAPPED                    Overlapped
)
{
}

DWORD
WINAPI
GetStorageDependencyInformation(
    _In_        HANDLE                      ObjectHandle,
    _In_        GET_STORAGE_DEPENDENCY_FLAG Flags,
    _In_        ULONG                       StorageDependencyInfoSize,
    _Inout_     PSTORAGE_DEPENDENCY_INFO    StorageDependencyInfo,
    _Inout_opt_ PULONG                      SizeUsed
)
{
}

DWORD
WINAPI
GetVirtualDiskInformation(
    _In_        HANDLE                 VirtualDiskHandle,
    _Inout_     PULONG                 VirtualDiskInfoSize,
    _Inout_     PGET_VIRTUAL_DISK_INFO VirtualDiskInfo,
    _Inout_opt_ PULONG                 SizeUsed
)
{
    // TODO: Check Handle
    if (!VirtualDiskInfo || !VirtualDiskInfoSize)
        return ERROR_INVALID_PARAMETER;

    switch (VirtualDiskInfo->Version)
    {
        case GET_VIRTUAL_DISK_INFO_UNSPECIFIED:
        case GET_VIRTUAL_DISK_INFO_SIZE:
            // VirtualDiskInfo->Size
        case GET_VIRTUAL_DISK_INFO_IDENTIFIER:
            // VirtualDiskInfo->Identifier
        case GET_VIRTUAL_DISK_INFO_PARENT_LOCATION:
            // VirtualDiskInfo->ParentLocation
        case GET_VIRTUAL_DISK_INFO_PARENT_IDENTIFIER:
            // VirtualDiskInfo->ParentIdentifier
        case GET_VIRTUAL_DISK_INFO_PARENT_TIMESTAMP:
            // VirtualDiskInfo->ParentTimestamp
        case GET_VIRTUAL_DISK_INFO_VIRTUAL_STORAGE_TYPE:
            // VirtualDiskInfo->VirtualStorageType
        case GET_VIRTUAL_DISK_INFO_PROVIDER_SUBTYPE:
            // VirtualDiskInfo->ProviderSubtype
        case GET_VIRTUAL_DISK_INFO_IS_4K_ALIGNED:
            // VirtualDiskInfo->Is4kAligned
        case GET_VIRTUAL_DISK_INFO_PHYSICAL_DISK:
            // VirtualDiskInfo->PhysicalDisk
        case GET_VIRTUAL_DISK_INFO_VHD_PHYSICAL_SECTOR_SIZE:
            // VirtualDiskInfo->VhdPhysicalSectorSize
        case GET_VIRTUAL_DISK_INFO_SMALLEST_SAFE_VIRTUAL_SIZE:
            // VirtualDiskInfo->SmallestSafeVirtualSize
        case GET_VIRTUAL_DISK_INFO_FRAGMENTATION:
            // VirtualDiskInfo->FragmentationPercentage
        case GET_VIRTUAL_DISK_INFO_IS_LOADED:
            // VirtualDiskInfo->IsLoaded
        case GET_VIRTUAL_DISK_INFO_VIRTUAL_DISK_ID:
            // VirtualDiskInfo->VirtualDiskId
        case GET_VIRTUAL_DISK_INFO_CHANGE_TRACKING_STATE:
            // VirtualDiskInfo->ChangeTrackingState
        {
            UNIMPLEMENTED;
            break;
        }

        default:
        {
            ERR(": VirtualDiskInfo->Version %d unknown\n", VirtualDiskInfo->Version);
            break;
        }
    }

    // TODO!
    UNIMPLEMENTED;

    return 0;
}

// GetVirtualDiskMetadata

DWORD
WINAPI
GetVirtualDiskOperationProgress(
    _In_  HANDLE                 VirtualDiskHandle,
    _In_  LPOVERLAPPED           Overlapped,
    _Out_ PVIRTUAL_DISK_PROGRESS Progress
)
{
}

DWORD
WINAPI
GetVirtualDiskPhysicalPath(
    _In_      HANDLE VirtualDiskHandle,
    _Inout_   PULONG DiskPathSizeInBytes,
    _Out_opt_ PWSTR  DiskPath
)
{
}

DWORD
WINAPI
MergeVirtualDisk(
    _In_     HANDLE                         VirtualDiskHandle,
    _In_     MERGE_VIRTUAL_DISK_FLAG        Flags,
    _In_     PMERGE_VIRTUAL_DISK_PARAMETERS Parameters,
    _In_opt_ LPOVERLAPPED                   Overlapped
)
{
}

// MirrorVirtualDisk
// ModifyVhdSet

DWORD
WINAPI
OpenVirtualDisk(
    _In_     PVIRTUAL_STORAGE_TYPE         VirtualStorageType,
    _In_     PCWSTR                        Path,
    _In_     VIRTUAL_DISK_ACCESS_MASK      VirtualDiskAccessMask,
    _In_     OPEN_VIRTUAL_DISK_FLAG        Flags,
    _In_opt_ POPEN_VIRTUAL_DISK_PARAMETERS Parameters,
    _Out_    PHANDLE                       Handle
)
{
}

// QueryChangesVirtualDisk
// RawSCSIVirtualDisk
// ResizeVirtualDisk

DWORD
WINAPI
SetVirtualDiskInformation(
    _In_ HANDLE                 VirtualDiskHandle,
    _In_ PSET_VIRTUAL_DISK_INFO VirtualDiskInfo
)
{
}

// SetVirtualDiskMetadata
// TakeSnapshotVhdSet
