/*
 * PROJECT:     FreeLoader Win32 Emulation
 * LICENSE:     Dual-licensed:
 *              GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 *              MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Win32 virtual filesystem driver
 * COPYRIGHT:   Copyright 2024-2026 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#include <freeldr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(FILESYSTEM);


#define TAG_WIN32_FILE   'F23W'
#define TAG_WIN32_VOLUME 'V23W'

typedef struct
{
    HANDLE hFile;
    ULONG DeviceId; // ID of the device where the file resides ???
    ULONG FileSize;     // File size
    UCHAR Attributes;   // File attributes
    // BOOLEAN Directory;
} WIN32_FILE_INFO, *PWIN32_FILE_INFO;

#if 0
PWIN32_VOLUME_INFO Win32Volumes[MAX_FDS];

static BOOLEAN
Win32OpenVolume(
    _Inout_ PWIN32_VOLUME_INFO Volume)
{
    TRACE("Win32OpenVolume() DeviceId = %d\n", Volume->DeviceId);

    // read the beginning of the FAT (or the whole one) to cache
    if (!FatReadVolumeSectors(Volume, Volume->ActiveFatSectorStart, Volume->FatCacheSize, Volume->FatCache))
    {
        FileSystemError("Error when reading FAT cache");
        FrLdrTempFree(Volume->FatCache, TAG_FAT_CACHE);
        FrLdrTempFree(Volume->FatCacheIndex, TAG_WIN32_VOLUME);
        return FALSE;
    }

    return TRUE;
}
#endif

/*
 * This function searches the file system for the
 * specified filename and fills in an FAT_FILE_INFO structure
 * with info describing the file, etc. returns ARC error code
 */
ARC_STATUS
Win32LookupFile(
    // _In_ PWIN32_VOLUME_INFO Volume,
    _In_ PCSTR FileName,
    _In_ OPENMODE OpenMode,
    _Out_ PWIN32_FILE_INFO FileInfoPointer)
{
    WIN32_FILE_INFO FileInfo;

    TRACE("Win32LookupFile() FileName = %s\n", FileName);

    RtlZeroMemory(FileInfoPointer, sizeof(*FileInfoPointer));

    /* Skip leading path separator, if any */
    if (*FileName == '\\' || *FileName == '/')
        ++FileName;

    // FIXME: Handle OpenMode
    /*
OpenReadOnly,
OpenWriteOnly,
OpenReadWrite,
CreateWriteOnly,
CreateReadWrite,
SupersedeWriteOnly,
SupersedeReadWrite,
OpenDirectory,
CreateDirectory
    */
    FileInfo.hFile = CreateFileA(FileName,
                                 GENERIC_READ /*FILE_GENERIC_READ*/,
                                 FILE_SHARE_READ /*| FILE_SHARE_WRITE*/,
                                 NULL,
                                 OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL,
                                 NULL);
    if (FileInfo.hFile == INVALID_HANDLE_VALUE)
        return ENOENT; // TODO: Refine error by using GetLastError()?

    /* Determine whether this is a directory */
    // if (!(FileInfo.Attributes & ATTR_DIRECTORY))
    // {
        // return ENOTDIR;
    // }

    /* Return the file info */
    RtlCopyMemory(FileInfoPointer, &FileInfo, sizeof(FileInfo));
    return ESUCCESS;
}


ARC_STATUS
Win32Close(
    _In_ ULONG FileId)
{
    PWIN32_FILE_INFO FileHandle = FsGetDeviceSpecific(FileId);

    CloseHandle(FileHandle->hFile);
    FrLdrTempFree(FileHandle, TAG_WIN32_FILE);

    return ESUCCESS;
}

ARC_STATUS
Win32GetFileInformation(
    _In_ ULONG FileId,
    _Out_ FILEINFORMATION* Information)
{
    PWIN32_FILE_INFO FileHandle = FsGetDeviceSpecific(FileId);
#if 0
    BY_HANDLE_FILE_INFORMATION fileInfo;
#else
    NTSTATUS Status;
    IO_STATUS_BLOCK Iosb;
    FILE_BASIC_INFORMATION FileBasic;
    FILE_STANDARD_INFORMATION FileStandard;
#endif
    ULONG FileAttributes;
    FILEINFORMATION DeviceInfo;
    ULONG DeviceId;

    RtlZeroMemory(Information, sizeof(*Information));

#if 0
    if (!GetFileInformationByHandle(FileHandle->hFile, &fileInfo))
        return EBADF;
    FileAttributes = fileInfo.dwFileAttributes;
#else
    Status = NtQueryInformationFile(FileHandle->hFile, &Iosb,
                                    &FileBasic, sizeof(FileBasic),
                                    FileBasicInformation);
    if (!NT_SUCCESS(Status))
        return EBADF;
    Status = NtQueryInformationFile(FileHandle->hFile, &Iosb,
                                    &FileStandard, sizeof(FileStandard),
                                    FileStandardInformation);
    if (!NT_SUCCESS(Status))
        return EBADF;
    FileAttributes = FileBasic.FileAttributes;
#endif
    Information->StartingAddress.QuadPart = 0ULL;
#if 0
    Information->EndingAddress.LowPart  = fileInfo.nFileSizeLow;
    Information->EndingAddress.HighPart = fileInfo.nFileSizeHigh;
#else
    Information->EndingAddress.LowPart  = FileStandard.EndOfFile.u.LowPart;
    Information->EndingAddress.HighPart = FileStandard.EndOfFile.u.HighPart;
#endif

    Information->CurrentAddress.HighPart = 0;
    Information->CurrentAddress.LowPart =
        SetFilePointer(FileHandle->hFile,
                       0,
                       &Information->CurrentAddress.HighPart,
                       FILE_CURRENT);

    /* Retrieve the type of the device on which the file resides */
    DeviceId = FsGetDeviceId(FileId);
    if (ArcGetFileInformation(DeviceId, &DeviceInfo) == ESUCCESS)
    {
        // FIXME: "CONFIGTYPE" is CONFIGURATION_TYPE
        Information->Type = DeviceInfo.Type;
    }

    /* Set the ARC file attributes */
    Information->Attributes = 0;
    if (FileAttributes & FILE_ATTRIBUTE_READONLY)
        Information->Attributes |= ReadOnlyFile;
    if (FileAttributes & FILE_ATTRIBUTE_HIDDEN)
        Information->Attributes |= HiddenFile;
    if (FileAttributes & FILE_ATTRIBUTE_SYSTEM)
        Information->Attributes |= SystemFile;
    if (FileAttributes & FILE_ATTRIBUTE_ARCHIVE)
        Information->Attributes |= ArchiveFile;
    if (FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        Information->Attributes |= DirectoryFile;
    /* The attribute 'DeleteFile' describes a file that is marked as "deleted" */
    if (FileStandard.DeletePending)
        Information->Attributes |= DeleteFile;

#if 0
    DWORD GetFullPathNameA(
      [in]  LPCSTR lpFileName,
      [in]  DWORD  nBufferLength,
      [out] LPSTR  lpBuffer,
      [out] LPSTR  *lpFilePart
    );

    ULONG FileNameLength;
    CHAR Filename[32];

    /* Copy the file name, perhaps truncated, and NUL-terminated */
    Information->FileNameLength = min(FileHandle->FileNameLength, sizeof(Information->FileName) - 1);
    RtlCopyMemory(Information->FileName, FileHandle->FileName, Information->FileNameLength);
    Information->FileName[Information->FileNameLength] = ANSI_NULL;
#endif

    TRACE("Win32GetFileInformation(%lu) -> FileSize = %llu, FilePointer = 0x%llx\n",
          FileId, Information->EndingAddress.QuadPart, Information->CurrentAddress.QuadPart);

    return ESUCCESS;
}

ARC_STATUS
Win32Open(
    _In_ PCHAR Path,
    _In_ OPENMODE OpenMode,
    _Out_ PULONG FileId)
{
    // PWIN32_VOLUME_INFO Volume;
    WIN32_FILE_INFO TempFileInfo;
    PWIN32_FILE_INFO FileHandle;
    ULONG DeviceId;
    // BOOLEAN IsDirectory;
    ARC_STATUS Status;

    if ((OpenMode != OpenReadOnly) && (OpenMode != OpenDirectory))
        return EACCES;

    DeviceId = FsGetDeviceId(*FileId);
    // Volume = Win32Volumes[DeviceId];

    TRACE("Win32Open() FileName = %s\n", Path);

    RtlZeroMemory(&TempFileInfo, sizeof(TempFileInfo));
    Status = Win32LookupFile(/*Volume,*/ Path, OpenMode, &TempFileInfo);
    if (Status != ESUCCESS)
        return ENOENT;

#if 0
    //
    // Check if caller opened what he expected (dir vs file)
    //
    IsDirectory = (TempFileInfo.Attributes & ATTR_DIRECTORY) != 0;
    if (IsDirectory && (OpenMode != OpenDirectory))
    {
        CloseHandle(TempFileInfo.hFile);
        return EISDIR;
    }
    else if (!IsDirectory && (OpenMode != OpenReadOnly))
    {
        CloseHandle(TempFileInfo.hFile);
        return ENOTDIR;
    }
#endif

    FileHandle = FrLdrTempAlloc(sizeof(WIN32_FILE_INFO), TAG_WIN32_FILE);
    if (!FileHandle)
    {
        CloseHandle(TempFileInfo.hFile);
        return ENOMEM;
    }

    RtlCopyMemory(FileHandle, &TempFileInfo, sizeof(TempFileInfo));
    // FileHandle->Volume = Volume;

    FsSetDeviceSpecific(*FileId, FileHandle);
    return ESUCCESS;
}

ARC_STATUS
Win32Read(
    _In_ ULONG FileId,
    _Out_ PVOID Buffer,
    _In_ ULONG N,
    _Out_ PULONG Count)
{
    PWIN32_FILE_INFO FileHandle = FsGetDeviceSpecific(FileId);
    BOOL Success;

    /* Call the Win32 method */
    Success = ReadFile(FileHandle->hFile, Buffer, N, Count, NULL);
    return (Success ? ESUCCESS : EIO);
}

ARC_STATUS
Win32Seek(
    _In_ ULONG FileId,
    _In_ LARGE_INTEGER* Position,
    _In_ SEEKMODE SeekMode)
{
    PWIN32_FILE_INFO FileHandle = FsGetDeviceSpecific(FileId);
    LARGE_INTEGER NewPosition = *Position;
    DWORD dwMoveMethod;

    switch (SeekMode)
    {
        case SeekAbsolute:
            dwMoveMethod = FILE_BEGIN;
            break;
        case SeekRelative:
            dwMoveMethod = FILE_CURRENT;
            break;
        default:
            // FILE_END not supported
            ASSERT(FALSE);
            return EINVAL;
    }

    TRACE("Win32Seek() NewPosition = %I64u, SeekMode = %d\n",
          NewPosition.QuadPart, SeekMode);

    /* Call the Win32 method */
    NewPosition.LowPart = SetFilePointer(FileHandle->hFile,
                                         NewPosition.LowPart,
                                         &NewPosition.HighPart,
                                         dwMoveMethod);

    if ((NewPosition.LowPart == INVALID_SET_FILE_POINTER) &&
        (GetLastError() != ERROR_SUCCESS))
    {
        return EINVAL;
    }

    return ESUCCESS;
}

const DEVVTBL Win32FuncTable =
{
    Win32Close,
    Win32GetFileInformation,
    Win32Open,
    Win32Read,
    Win32Seek,
    L"win32fs",
};

const DEVVTBL*
Win32Mount(
    _In_ ULONG DeviceId)
{
#if 0
    PWIN32_VOLUME_INFO Volume;
    FILEINFORMATION FileInformation;
    LARGE_INTEGER Position;
    ULONG Count;
    ULARGE_INTEGER SectorCount;
    ARC_STATUS Status;
#endif

    TRACE("Enter Win32Mount(%lu)\n", DeviceId);

#if 0
    /* Allocate data for volume information */
    Volume = FrLdrTempAlloc(sizeof(WIN32_VOLUME_INFO), TAG_WIN32_VOLUME);
    if (!Volume)
        return NULL;
    RtlZeroMemory(Volume, sizeof(WIN32_VOLUME_INFO));

    /* Keep device id */
    Volume->DeviceId = DeviceId;

    /* Really open the volume */
    if (!Win32OpenVolume(Volume))
    {
        FrLdrTempFree(Volume, TAG_WIN32_VOLUME);
        return NULL;
    }

    /* Remember volume information */
    Win32Volumes[DeviceId] = Volume;
#endif

    /* Return success */
    TRACE("Win32Mount(%lu) success\n", DeviceId);
    return &Win32FuncTable;
}
