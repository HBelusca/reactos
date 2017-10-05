
#include "vhddump.h"
#include "virtdisk_driver.h"

#define NDEBUG
#include <debug.h>

/* GENERIC MEMORY MANAGEMENT FUNCTIONS ***************************************/

PVOID NTAPI
VdpAlloc(IN SIZE_T Size,
         IN ULONG Flags,
         IN ULONG Tag)
{
    UNREFERENCED_PARAMETER(Tag);
    return RtlAllocateHeap(GetProcessHeap(), Flags, Size);
}

VOID NTAPI
VdpFree(IN PVOID Ptr,
        IN ULONG Flags,
        IN ULONG Tag)
{
    UNREFERENCED_PARAMETER(Tag);
    RtlFreeHeap(GetProcessHeap(), Flags, Ptr);
}


/* GENERIC FILE I/O FUNCTIONS ************************************************/

NTSTATUS NTAPI
VdpReadFile(IN  HANDLE FileHandle,
            IN  PLARGE_INTEGER FileOffset,
            OUT PVOID   Buffer,
            IN  SIZE_T  Length,
            OUT PSIZE_T ReadLength OPTIONAL)
{
    if (ReadLength)
        *ReadLength = 0;

    RtlSetLastWin32ErrorAndNtStatusFromNtStatus(STATUS_SUCCESS);
    if (SetFilePointerEx(FileHandle, *FileOffset, NULL, FILE_BEGIN))
        ReadFile(FileHandle, Buffer, Length, ReadLength, NULL);
    return RtlGetLastNtStatus();
}

NTSTATUS NTAPI
VdpWriteFile(IN  HANDLE FileHandle,
             IN  PLARGE_INTEGER FileOffset,
             IN  PVOID   Buffer,
             IN  SIZE_T  Length,
             OUT PSIZE_T WrittenLength OPTIONAL)
{
    if (WrittenLength)
        *WrittenLength = 0;

    RtlSetLastWin32ErrorAndNtStatusFromNtStatus(STATUS_SUCCESS);
    if (SetFilePointerEx(FileHandle, *FileOffset, NULL, FILE_BEGIN))
        WriteFile(FileHandle, Buffer, Length, WrittenLength, NULL);
    return RtlGetLastNtStatus();
}

NTSTATUS NTAPI
VdpSetFileSize(IN HANDLE FileHandle,
               IN ULONG FileSize,    // SIZE_T // UINT64 // ULONGLONG
               IN ULONG OldFileSize) // SIZE_T
{
    UNREFERENCED_PARAMETER(OldFileSize);

    RtlSetLastWin32ErrorAndNtStatusFromNtStatus(STATUS_SUCCESS);
    SetFilePointer(FileHandle, FileSize, NULL, FILE_BEGIN);
    SetEndOfFile(FileHandle);
    return RtlGetLastNtStatus();
}

NTSTATUS NTAPI
VdpFlushFile(IN HANDLE FileHandle,
             IN PLARGE_INTEGER FileOffset,
             IN ULONG Length)
{
    UNREFERENCED_PARAMETER(FileOffset);
    UNREFERENCED_PARAMETER(Length);

    RtlSetLastWin32ErrorAndNtStatusFromNtStatus(STATUS_SUCCESS);
    FlushFileBuffers(FileHandle);
    return RtlGetLastNtStatus();
}


/* VirtDisk BACKEND SUPPORT **************************************************/

extern VIRTUAL_DISK_VTBL VhdVtbl;

PVIRTUAL_DISK_VTBL SupportedBackends[] =
{
    &VhdVtbl,
};


/* PUBLIC APIs ***************************************************************/

NTSTATUS
NTAPI
VdCreateDisk(
    IN OUT PVIRTUAL_DISK* VirtualDisk,
    IN PUNICODE_STRING DiskFileName,
    IN UINT64 FileSize)
{
    NTSTATUS Status;
    PVIRTUAL_DISK Disk = NULL;
    HANDLE hFile = NULL;
#if 0
    ULONG FileSize;
    BY_HANDLE_FILE_INFORMATION FileInformation;
    ULONG i;
#endif
    PVIRTUAL_DISK_VTBL Backend = NULL;

    PCWSTR FileName = DiskFileName->Buffer;

    /* Try to open the file */
    // SetLastError(0); // For debugging purposes
    RtlSetLastWin32ErrorAndNtStatusFromNtStatus(STATUS_SUCCESS);
    hFile = CreateFileW(FileName,
                        GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ, // : 0 /* No sharing access */,
                        NULL,
                        CREATE_NEW, // CREATE_ALWAYS, OPEN_ALWAYS
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    DisplayMessage(L"File '%s' creating %s ; GetLastError() = %u\n",
            FileName, hFile != INVALID_HANDLE_VALUE ? L"succeeded" : L"failed", GetLastError());

    /* If we failed, bail out */
    if (hFile == INVALID_HANDLE_VALUE)
    {
        DisplayMessage(L"VdOpenDisk: Error when opening disk file '%s' (Error: %u).", FileName, GetLastError());
        return RtlGetLastNtStatus();
    }

    /* OK, we have a handle to the file */

#if 0
    /*
     * Check that it is really a file, and not a physical drive.
     * For obvious security reasons, we do not want to be able to
     * write directly to physical drives.
     *
     * Redundant checks
     */
    // SetLastError(0);
    RtlSetLastWin32ErrorAndNtStatusFromNtStatus(STATUS_SUCCESS);
    if (!GetFileInformationByHandle(hFile, &FileInformation) &&
        GetLastError() == ERROR_INVALID_FUNCTION)
    {
        /* Objects other than real files are not supported */
        DisplayMessage(L"VdOpenDisk: '%s' is not a valid disk file.", FileName);
        Status = RtlGetLastNtStatus();
        goto Quit;
    }
    // SetLastError(0);
    RtlSetLastWin32ErrorAndNtStatusFromNtStatus(STATUS_SUCCESS);
    if (GetFileSize(hFile, NULL) == INVALID_FILE_SIZE &&
        GetLastError() == ERROR_INVALID_FUNCTION)
    {
        /* Objects other than real files are not supported */
        DisplayMessage(L"VdOpenDisk: '%s' is not a valid disk file.", FileName);
        Status = RtlGetLastNtStatus();
        goto Quit;
    }

    /* Retrieve the size of the file */
    /**/RtlSetLastWin32ErrorAndNtStatusFromNtStatus(STATUS_SUCCESS);/**/
    FileSize = GetFileSize(hFile, NULL);
    if (FileSize == INVALID_FILE_SIZE && GetLastError() != ERROR_SUCCESS)
    {
        /* We failed, bail out */
        DisplayMessage(L"Error when retrieving file size, or size too large (%d)\n", FileSize);
        Status = RtlGetLastNtStatus();
        goto Quit;
    }

    /*
     * Choose an adequate backend by probing successively the virtual disk file format.
     */
    Status  = STATUS_SUCCESS;
    Backend = NULL;
    for (i = 0; i < ARRAYSIZE(SupportedBackends); ++i)
    {
        // DPRINT("VdOpenDisk: Trying to load %s frontend...\n",
               // SupportedBackends[i]->FrontEndName);
        Status = SupportedBackends[i]->Probe(hFile, FileSize);
        if (NT_SUCCESS(Status))
        {
            // DPRINT1("VdOpenDisk: %s probing succeeded\n",
                   // SupportedBackends[i]->FrontEndName);
            DPRINT1("VdOpenDisk: Probing succeeded\n");
            Backend = SupportedBackends[i];
            break;
        }
        else
        {
            // DPRINT1("VdOpenDisk: Probing %s failed, Status = 0x%08lx , continuing...\n",
                    // SupportedBackends[i]->FrontEndName, Status);
            DPRINT1("VdOpenDisk: Probing failed, Status = 0x%08lx , continuing...\n",
                    Status);
        }
    }
    if (!NT_SUCCESS(Status)) // || i >= ARRAYSIZE(SupportedBackends) || !Backend
    {
        DPRINT1("VdOpenDisk: Failed to probe, Status = 0x%08lx\n", Status);
        // STATUS_VIRTDISK_PROVIDER_NOT_FOUND
        goto Quit;
    }
#endif

    // HACK! -- Always use VHD backend for the moment...
    Backend = &VhdVtbl;

    /* Success, mount the image */
    Disk = VdpAlloc(sizeof(*Disk), HEAP_ZERO_MEMORY, 0);
    if (!Disk)
    {
        DisplayMessage(L"Cannot allocate heap!\n");
        Status = STATUS_NO_MEMORY;
        goto Quit;
    }

    /* Copy the image file name (ignore any allocation failure) */
    RtlInitEmptyUnicodeString(&Disk->DiskFileName, NULL, 0);
    if (DiskFileName && DiskFileName->Buffer && DiskFileName->Length &&
        (DiskFileName->Length <= DiskFileName->MaximumLength))
    {
        Disk->DiskFileName.Buffer = VdpAlloc(DiskFileName->Length,
                                             HEAP_ZERO_MEMORY,
                                             0);
        if (Disk->DiskFileName.Buffer)
        {
            Disk->DiskFileName.MaximumLength = DiskFileName->Length;
            RtlCopyUnicodeString(&Disk->DiskFileName, DiskFileName);
        }
    }

    Disk->ParentDisk = NULL;
    Disk->FileHandle = hFile;

    /* Load the backend */
    Disk->BackendData = NULL; // Initialized when the backend effectively opens the disk
    Disk->Backend = Backend;

    Status = Backend->CreateDisk(Disk, FileSize,
                                 0x200000, 512);
    if (!NT_SUCCESS(Status))
    {
        DisplayMessage(L"VdOpenDisk: Failed to mount disk file '%s' in 0x%p.", FileName, Disk);
        goto Quit;
    }

    Disk->Alignment = Backend->GetAlignment(Disk);

    ///* Update its read/write state */
    //Disk->ReadOnly = ReadOnly;

    //
    // TODO: Check whether it's a differencing disk, and if so,
    // try to open its parent. In case of success, initialize
    // Disk->ParentDisk ; otherwise cleanup & fail.
    //

    *VirtualDisk = Disk;

    Status = STATUS_SUCCESS;

Quit:
    if (!NT_SUCCESS(Status))
    {
        if (Disk)
        {
            if (Disk->DiskFileName.Buffer)
                VdpFree(Disk->DiskFileName.Buffer, 0, 0);
            VdpFree(Disk, 0, 0);
        }
        if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
    }
    return Status;
}

NTSTATUS
NTAPI
VdOpenDisk(
    IN OUT PVIRTUAL_DISK* VirtualDisk,
    IN PUNICODE_STRING DiskFileName,
    // IN BOOLEAN  CreateNew,
    IN BOOLEAN  ReadOnly)
{
    NTSTATUS Status;
    PVIRTUAL_DISK Disk = NULL;
    HANDLE hFile = NULL;
    UINT64 FileSize;
    BY_HANDLE_FILE_INFORMATION FileInformation;
    ULONG i;
    PVIRTUAL_DISK_VTBL Backend = NULL;

    PCWSTR FileName = DiskFileName->Buffer;

    /* Try to open the file */
    // SetLastError(0); // For debugging purposes
    RtlSetLastWin32ErrorAndNtStatusFromNtStatus(STATUS_SUCCESS);
    hFile = CreateFileW(FileName,
                        ReadOnly ?  GENERIC_READ
                                 : (GENERIC_READ | GENERIC_WRITE),
                        ReadOnly ? FILE_SHARE_READ : 0 /* No sharing access */,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    DisplayMessage(L"File '%s' opening %s ; GetLastError() = %u\n",
            FileName, hFile != INVALID_HANDLE_VALUE ? L"succeeded" : L"failed", GetLastError());

    /* If we failed, bail out */
    if (hFile == INVALID_HANDLE_VALUE)
    {
        DisplayMessage(L"VdOpenDisk: Error when opening disk file '%s' (Error: %u).", FileName, GetLastError());
        return RtlGetLastNtStatus();
    }

    /* OK, we have a handle to the file */

    /*
     * Check that it is really a file, and not a physical drive.
     * For obvious security reasons, we do not want to be able to
     * write directly to physical drives.
     *
     * Redundant checks
     */
    // SetLastError(0);
    RtlSetLastWin32ErrorAndNtStatusFromNtStatus(STATUS_SUCCESS);
    if (!GetFileInformationByHandle(hFile, &FileInformation) &&
        GetLastError() == ERROR_INVALID_FUNCTION)
    {
        /* Objects other than real files are not supported */
        DisplayMessage(L"VdOpenDisk: '%s' is not a valid disk file.", FileName);
        Status = RtlGetLastNtStatus();
        goto Quit;
    }
    // SetLastError(0);
    RtlSetLastWin32ErrorAndNtStatusFromNtStatus(STATUS_SUCCESS);
    if (GetFileSize(hFile, NULL) == INVALID_FILE_SIZE &&
        GetLastError() == ERROR_INVALID_FUNCTION)
    {
        /* Objects other than real files are not supported */
        DisplayMessage(L"VdOpenDisk: '%s' is not a valid disk file.", FileName);
        Status = RtlGetLastNtStatus();
        goto Quit;
    }

    /* Retrieve the size of the file */
    /**/RtlSetLastWin32ErrorAndNtStatusFromNtStatus(STATUS_SUCCESS);/**/
    FileSize = GetFileSize(hFile, NULL);
    if (FileSize == INVALID_FILE_SIZE && GetLastError() != ERROR_SUCCESS)
    {
        /* We failed, bail out */
        DisplayMessage(L"Error when retrieving file size, or size too large (%d)\n", FileSize);
        Status = RtlGetLastNtStatus();
        goto Quit;
    }

    /*
     * Choose an adequate backend by probing successively the virtual disk file format.
     */
    Status  = STATUS_SUCCESS;
    Backend = NULL;
    for (i = 0; i < ARRAYSIZE(SupportedBackends); ++i)
    {
        // DPRINT("VdOpenDisk: Trying to load %s frontend...\n",
               // SupportedBackends[i]->FrontEndName);
        Status = SupportedBackends[i]->Probe(hFile, FileSize);
        if (NT_SUCCESS(Status))
        {
            // DPRINT1("VdOpenDisk: %s probing succeeded\n",
                   // SupportedBackends[i]->FrontEndName);
            DPRINT1("VdOpenDisk: Probing succeeded\n");
            Backend = SupportedBackends[i];
            break;
        }
        else
        {
            // DPRINT1("VdOpenDisk: Probing %s failed, Status = 0x%08lx , continuing...\n",
                    // SupportedBackends[i]->FrontEndName, Status);
            DPRINT1("VdOpenDisk: Probing failed, Status = 0x%08lx , continuing...\n",
                    Status);
        }
    }
    if (!NT_SUCCESS(Status)) // || i >= ARRAYSIZE(SupportedBackends) || !Backend
    {
        DPRINT1("VdOpenDisk: Failed to probe, Status = 0x%08lx\n", Status);
        // STATUS_VIRTDISK_PROVIDER_NOT_FOUND
        goto Quit;
    }

    /* Success, mount the image */
    Disk = VdpAlloc(sizeof(*Disk), HEAP_ZERO_MEMORY, 0);
    if (!Disk)
    {
        DisplayMessage(L"Cannot allocate heap!\n");
        Status = STATUS_NO_MEMORY;
        goto Quit;
    }

    /* Copy the image file name (ignore any allocation failure) */
    RtlInitEmptyUnicodeString(&Disk->DiskFileName, NULL, 0);
    if (DiskFileName && DiskFileName->Buffer && DiskFileName->Length &&
        (DiskFileName->Length <= DiskFileName->MaximumLength))
    {
        Disk->DiskFileName.Buffer = VdpAlloc(DiskFileName->Length,
                                             HEAP_ZERO_MEMORY,
                                             0);
        if (Disk->DiskFileName.Buffer)
        {
            Disk->DiskFileName.MaximumLength = DiskFileName->Length;
            RtlCopyUnicodeString(&Disk->DiskFileName, DiskFileName);
        }
    }

    Disk->ParentDisk = NULL;
    Disk->FileHandle = hFile;

    /* Load the backend */
    Disk->BackendData = NULL; // Initialized when the backend effectively opens the disk
    Disk->Backend = Backend;

    Status = Backend->OpenDisk(Disk, FileSize, ReadOnly);
    if (!NT_SUCCESS(Status))
    {
        DisplayMessage(L"VdOpenDisk: Failed to mount disk file '%s' in 0x%p.", FileName, Disk);
        goto Quit;
    }

    Disk->Alignment = Backend->GetAlignment(Disk);

    ///* Update its read/write state */
    //Disk->ReadOnly = ReadOnly;

    //
    // TODO: Check whether it's a differencing disk, and if so,
    // try to open its parent. In case of success, initialize
    // Disk->ParentDisk ; otherwise cleanup & fail.
    //

    *VirtualDisk = Disk;

    Status = STATUS_SUCCESS;

Quit:
    if (!NT_SUCCESS(Status))
    {
        if (Disk)
        {
            if (Disk->DiskFileName.Buffer)
                VdpFree(Disk->DiskFileName.Buffer, 0, 0);
            VdpFree(Disk, 0, 0);
        }
        if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
    }
    return Status;
}

NTSTATUS
NTAPI
VdFlushDisk(
    IN PVIRTUAL_DISK VirtualDisk)
{
    ASSERT(VirtualDisk);
    return VirtualDisk->Backend->FlushDisk(VirtualDisk);
}

VOID    // or NTSTATUS, to return whether or not the disk is still used??
NTAPI
VdCloseDisk(
    IN PVIRTUAL_DISK VirtualDisk)
{
    ASSERT(VirtualDisk);

#if 0 // Non-recursive way
    PVIRTUAL_DISK ParentDisk;
    while (VirtualDisk)
    {
        VirtualDisk->Backend->CloseDisk(VirtualDisk);
        ParentDisk = VirtualDisk->ParentDisk;
        // VirtualDisk->ParentDisk = NULL;

        if (VirtualDisk->FileHandle != INVALID_HANDLE_VALUE)
            CloseHandle(VirtualDisk->FileHandle);
        if (VirtualDisk->DiskFileName.Buffer)
            VdpFree(VirtualDisk->DiskFileName.Buffer, 0, 0);
        VdpFree(VirtualDisk, 0, 0);

        VirtualDisk = ParentDisk;
    }
#else // Recursive way
    VirtualDisk->Backend->CloseDisk(VirtualDisk);
    if (VirtualDisk->ParentDisk)
        VdCloseDisk(VirtualDisk->ParentDisk);
    // VirtualDisk->ParentDisk = NULL;

    if (VirtualDisk->FileHandle != INVALID_HANDLE_VALUE)
        CloseHandle(VirtualDisk->FileHandle);
    if (VirtualDisk->DiskFileName.Buffer)
        VdpFree(VirtualDisk->DiskFileName.Buffer, 0, 0);
    VdpFree(VirtualDisk, 0, 0);
    // return STATUS_SUCCESS;
#endif
}

NTSTATUS
NTAPI
VdReadDiskAligned(
    IN  PVIRTUAL_DISK VirtualDisk,
    IN  PLARGE_INTEGER ByteOffset, // OPTIONAL
    OUT PVOID   Buffer,
    IN  SIZE_T  Length,
    OUT PSIZE_T ReadLength OPTIONAL)
{
    NTSTATUS Status = STATUS_SUCCESS;
    LARGE_INTEGER FileOffset;
    SIZE_T ReadBufLength = 0;

    ASSERT(VirtualDisk);

    if (ReadLength)
        *ReadLength = 0;

    if (// !IS_ALIGNED(Buffer, VirtualDisk->Alignment) ||
        !IS_ALIGNED(Length, (SIZE_T)VirtualDisk->Alignment) ||
        !IS_ALIGNED(ByteOffset->QuadPart, (LONGLONG)VirtualDisk->Alignment))
    {
        return STATUS_DATATYPE_MISALIGNMENT;
    }

    // if (ByteOffset->QuadPart + Length > VirtualDisk->TotalSize)
        // return STATUS_INVALID_PARAMETER;

    FileOffset = *ByteOffset;

    /* Read alternatively the data in the disk and possibly in its parents */
    while (Length > 0)
    {
        Status = VirtualDisk->Backend->ReadDiskAligned(VirtualDisk,
                                                       &FileOffset,
                                                       Buffer,
                                                       Length,
                                                       &ReadBufLength);
        // Here, ReadBufLength <= Length
        if ((Status == STATUS_NONEXISTENT_SECTOR) && (ReadBufLength > 0))
        {
            if (VirtualDisk->ParentDisk)
            {
                /* A parent disk is available: read the region in it */
                Status = VdReadDiskAligned(VirtualDisk->ParentDisk,
                                           &FileOffset,
                                           Buffer,
                                           ReadBufLength,
                                           &ReadBufLength);
                // Here, ReadBufLength may have diminished
            }
            else
            {
                /* No parent disk: just zero out the memory region */
                RtlZeroMemory(Buffer, ReadBufLength);
                Status = STATUS_SUCCESS;
            }
        }
        // if ( ((Status == STATUS_NONEXISTENT_SECTOR) && (ReadBufLength == 0)) ||
        //       (Status != STATUS_NONEXISTENT_SECTOR) )
        // FIXME: Check whether ReadBufLength == 0 ??
        if (!NT_SUCCESS(Status))
        {
            /* An unknown error happened, stop there */
            break;
        }

        Buffer  = (PVOID)((ULONG_PTR)Buffer + ReadBufLength);
        Length -= ReadBufLength;
        FileOffset.QuadPart += ReadBufLength;
    }

    if (ReadLength)
        *ReadLength = (FileOffset.QuadPart - ByteOffset->QuadPart);

    return Status;
}

NTSTATUS
NTAPI
VdReadDisk(
    IN  PVIRTUAL_DISK VirtualDisk,
    IN  PLARGE_INTEGER ByteOffset, // OPTIONAL
    OUT PVOID   Buffer,
    IN  SIZE_T  Length,
    OUT PSIZE_T ReadLength OPTIONAL)
{
    NTSTATUS Status;
    PVOID  AlignedBuffer = NULL;
    SIZE_T AlignedLength;
    LARGE_INTEGER AlignedByteOffset;

    ASSERT(VirtualDisk);

    if (ReadLength)
        *ReadLength = 0;

    // if (ByteOffset->QuadPart + Length > VirtualDisk->TotalSize)
        // return STATUS_INVALID_PARAMETER;

    AlignedLength = Length;
    AlignedByteOffset = *ByteOffset;

    /*
     * If the output buffer & lengths are not aligned, align those.
     * If the created buffer is too big, it may be interesting (not done here)
     * to allocate a chunk, and then perform repeated aligned reads.
     */
    if (// !IS_ALIGNED(Buffer, VirtualDisk->Alignment) ||
        !IS_ALIGNED(Length, (SIZE_T)VirtualDisk->Alignment) || // Hmmmmm.....
        !IS_ALIGNED(ByteOffset->QuadPart, (LONGLONG)VirtualDisk->Alignment))
    {
        /*
         * Align the offset and the buffer length to sector boundaries,
         * taking into account for cross-sector regions.
         */
        AlignedByteOffset.QuadPart = ROUND_DOWN(ByteOffset->QuadPart, (LONGLONG)VirtualDisk->Alignment);
        AlignedLength = (SIZE_T)(ROUND_UP(ByteOffset->QuadPart + Length, (LONGLONG)VirtualDisk->Alignment) - AlignedByteOffset.QuadPart);

        AlignedBuffer = VdpAlloc(AlignedLength, 0, 0);
        if (!AlignedBuffer)
            return STATUS_NO_MEMORY; // STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = VdReadDiskAligned(VirtualDisk,
                               &AlignedByteOffset,
                               AlignedBuffer ? AlignedBuffer : Buffer,
                               AlignedLength,
                               &AlignedLength);

    /*
     * If an aligned buffer was used, copy its contents
     * back into the user buffer and free it.
     */
    if (AlignedBuffer)
    {
        /*
         * Compute the offset between the user's buffer start and
         * its aligned value, and store it in 'AlignedByteOffset'.
         */
        AlignedByteOffset.QuadPart = ByteOffset->QuadPart - AlignedByteOffset.QuadPart;
        ASSERT(AlignedByteOffset.HighPart == 0);

        /* Be sure the read data actually intersects the user's buffer area */
        if (AlignedLength > AlignedByteOffset.LowPart)
        {
            AlignedLength = min(AlignedLength - AlignedByteOffset.LowPart, Length);
            RtlCopyMemory(Buffer,
                          (PVOID)((ULONG_PTR)AlignedBuffer + AlignedByteOffset.LowPart),
                          AlignedLength);
        }
        else
        {
            AlignedLength = 0;
        }

        VdpFree(AlignedBuffer, 0, 0);
    }

    if (ReadLength)
        *ReadLength = AlignedLength;

    return Status;
}

NTSTATUS
NTAPI
VdWriteDiskAligned(
    IN  PVIRTUAL_DISK VirtualDisk,
    IN  PLARGE_INTEGER ByteOffset, // OPTIONAL
    IN  PVOID   Buffer,
    IN  SIZE_T  Length,
    OUT PSIZE_T WrittenLength OPTIONAL)
{
    NTSTATUS Status = STATUS_SUCCESS;
    LARGE_INTEGER FileOffset;
    SIZE_T WrittenBufLength = 0;

    ASSERT(VirtualDisk);

    if (WrittenLength)
        *WrittenLength = 0;

    // if (VirtualDisk->ReadOnly)
        // return STATUS_ACCESS_DENIED; // STATUS_MEDIA_WRITE_PROTECTED;

    if (// !IS_ALIGNED(Buffer, VirtualDisk->Alignment) ||
        !IS_ALIGNED(Length, (SIZE_T)VirtualDisk->Alignment) ||
        !IS_ALIGNED(ByteOffset->QuadPart, (LONGLONG)VirtualDisk->Alignment))
    {
        return STATUS_DATATYPE_MISALIGNMENT;
    }

    // if (ByteOffset->QuadPart + Length > VirtualDisk->TotalSize)
        // return STATUS_INVALID_PARAMETER;

    FileOffset = *ByteOffset;

    /*
     * Write the data only in this disk (if it has parents, they are
     * read only and this disk is actually a differencing disk).
     */
    while (Length > 0)
    {
        Status = VirtualDisk->Backend->WriteDiskAligned(VirtualDisk,
                                                        &FileOffset,
                                                        Buffer,
                                                        Length,
                                                        &WrittenBufLength);
        // Here, WrittenBufLength <= Length
        // FIXME: Check whether WrittenBufLength == 0 ??
        if (!NT_SUCCESS(Status))
        {
            /* An unknown error happened, stop there */
            break;
        }

        Buffer  = (PVOID)((ULONG_PTR)Buffer + WrittenBufLength);
        Length -= WrittenBufLength;
        FileOffset.QuadPart += WrittenBufLength;
    }

    if (WrittenLength)
        *WrittenLength = (FileOffset.QuadPart - ByteOffset->QuadPart);

    return Status;
}

NTSTATUS
NTAPI
VdWriteDisk(
    IN  PVIRTUAL_DISK VirtualDisk,
    IN  PLARGE_INTEGER ByteOffset, // OPTIONAL
    IN  PVOID   Buffer,
    IN  SIZE_T  Length,
    OUT PSIZE_T WrittenLength OPTIONAL)
{
    NTSTATUS Status;
    PVOID  AlignedBuffer = NULL;
    SIZE_T AlignedLength;
    LARGE_INTEGER AlignedByteOffset;

    ASSERT(VirtualDisk);

    if (WrittenLength)
        *WrittenLength = 0;

    // if (VirtualDisk->ReadOnly)
        // return STATUS_ACCESS_DENIED; // STATUS_MEDIA_WRITE_PROTECTED;

    // if (ByteOffset->QuadPart + Length > VirtualDisk->TotalSize)
        // return STATUS_INVALID_PARAMETER;

    AlignedLength = Length;
    AlignedByteOffset = *ByteOffset;

    /*
     * If the input buffer & lengths are not aligned, align those.
     * If the created buffer is too big, it may be interesting (not done here)
     * to allocate a chunk, and then perform repeated aligned writes.
     */
    if (// !IS_ALIGNED(Buffer, VirtualDisk->Alignment) ||
        !IS_ALIGNED(Length, (SIZE_T)VirtualDisk->Alignment) || // Hmmmmm.....
        !IS_ALIGNED(ByteOffset->QuadPart, (LONGLONG)VirtualDisk->Alignment))
    {
        /*
         * Align the offset and the buffer length to sector boundaries,
         * taking into account for cross-sector regions.
         */
        AlignedByteOffset.QuadPart = ROUND_DOWN(ByteOffset->QuadPart, (LONGLONG)VirtualDisk->Alignment);
        AlignedLength = (SIZE_T)(ROUND_UP(ByteOffset->QuadPart + Length, (LONGLONG)VirtualDisk->Alignment) - AlignedByteOffset.QuadPart);

        AlignedBuffer = VdpAlloc(AlignedLength, 0, 0);
        if (!AlignedBuffer)
            return STATUS_NO_MEMORY; // STATUS_INSUFFICIENT_RESOURCES;

        /*
         * Fetch full sectors into the aligned buffer,
         * then patch it with user data.
         */
        Status = VdReadDiskAligned(VirtualDisk,
                                   &AlignedByteOffset,
                                   AlignedBuffer,
                                   AlignedLength,
                                   &AlignedLength);
        // ASSERT(NT_SUCCESS(Status) && AlignedLength did not change....);

        RtlCopyMemory((PVOID)((ULONG_PTR)AlignedBuffer + ByteOffset->QuadPart - AlignedByteOffset.QuadPart),
                      Buffer, Length);
    }

    Status = VdWriteDiskAligned(VirtualDisk,
                                &AlignedByteOffset,
                                AlignedBuffer ? AlignedBuffer : Buffer,
                                AlignedLength,
                                &AlignedLength);

    /*
     * If an aligned buffer was used, retrieve the number of bytes from
     * the user buffer actually written (not counting the remaining data
     * from the aligned part). Then free the aligned buffer.
     */
    if (AlignedBuffer)
    {
        /*
         * Compute the offset between the user's buffer start and
         * its aligned value, and store it in 'AlignedByteOffset'.
         */
        AlignedByteOffset.QuadPart = ByteOffset->QuadPart - AlignedByteOffset.QuadPart;
        ASSERT(AlignedByteOffset.HighPart == 0);

        /* Be sure the written data actually intersects the user's buffer area */
        if (AlignedLength > AlignedByteOffset.LowPart)
            AlignedLength = min(AlignedLength - AlignedByteOffset.LowPart, Length);
        else
            AlignedLength = 0;

        VdpFree(AlignedBuffer, 0, 0);
    }

    if (WrittenLength)
        *WrittenLength = AlignedLength;

    return Status;
}

UINT64
NTAPI
VdGetVirtTotalSize(
    IN PVIRTUAL_DISK VirtualDisk)
{
    ASSERT(VirtualDisk);
    return VirtualDisk->Backend->GetVirtTotalSize(VirtualDisk);
}

// UINT64
// NTAPI
// VdGetFileSize(
    // IN PVIRTUAL_DISK VirtualDisk)
// {
    // ASSERT(VirtualDisk);
    // return VirtualDisk->Backend->GetFileSize(VirtualDisk);
// }

NTSTATUS
NTAPI
VdCompactDisk(
    IN PVIRTUAL_DISK VirtualDisk)
{
    NTSTATUS Status = STATUS_NOT_SUPPORTED;

    ASSERT(VirtualDisk);

    if (VirtualDisk->Backend->CompactDisk)
        Status = VirtualDisk->Backend->CompactDisk(VirtualDisk->BackendData);

    if (Status == STATUS_NOT_SUPPORTED)
        DisplayMessage(L"VdCompactDisk: CompactDisk not supported for disk %wZ\n",
            &VirtualDisk->DiskFileName);
    return Status;
}

NTSTATUS
NTAPI
VdExpandDisk(
    IN PVIRTUAL_DISK VirtualDisk)
{
    NTSTATUS Status = STATUS_NOT_SUPPORTED;

    ASSERT(VirtualDisk);

    if (VirtualDisk->Backend->ExpandDisk)
        Status = VirtualDisk->Backend->ExpandDisk(VirtualDisk->BackendData);

    if (Status == STATUS_NOT_SUPPORTED)
        DisplayMessage(L"VdExpandDisk: ExpandDisk not supported for disk %wZ\n",
            &VirtualDisk->DiskFileName);
    return Status;
}

NTSTATUS
NTAPI
VdMergeDisks(
    IN PVIRTUAL_DISK VirtualDisk1,
    IN PVIRTUAL_DISK VirtualDisk2)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
VdRepairDisk(
    IN PVIRTUAL_DISK VirtualDisk)
{
    NTSTATUS Status = STATUS_NOT_SUPPORTED;

    ASSERT(VirtualDisk);

    if (VirtualDisk->Backend->RepairDisk)
        Status = VirtualDisk->Backend->RepairDisk(VirtualDisk->BackendData);

    if (Status == STATUS_NOT_SUPPORTED)
        DisplayMessage(L"VdRepairDisk: RepairDisk not supported for disk %wZ\n",
            &VirtualDisk->DiskFileName);
    return Status;
}
