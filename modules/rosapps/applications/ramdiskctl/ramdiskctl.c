// RAMDisk Controller

#include <stdio.h>
#include <conio.h>

#define WIN32_NO_STATUS
#include <windows.h>

/* Ripped from RPC headers */
#define RPCRTAPI DECLSPEC_IMPORT
#ifndef __ROS_LONG64__
typedef long RPC_STATUS;
#else
typedef int RPC_STATUS;
#endif
#define RPC_ENTRY __stdcall // WINAPI
RPCRTAPI RPC_STATUS RPC_ENTRY UuidCreate(UUID*);

#include <ntddrdsk.h>

#define NTOS_MODE_USER
#include <ndk/iofuncs.h>
#include <ndk/obfuncs.h>
#include <ndk/rtlfuncs.h>

void Usage(WCHAR* name)
{
    wprintf(L"Usage: %s\n", name);
}

int wmain(int argc, WCHAR* argv[])
{
    BOOL Success;
    NTSTATUS Status;
    UNICODE_STRING DriverName;
    IO_STATUS_BLOCK IoStatusBlock;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE DriverHandle;
    UCHAR Buffer[FIELD_OFFSET(RAMDISK_CREATE_INPUT, FileName) + MAX_PATH*sizeof(WCHAR)];
    PRAMDISK_CREATE_INPUT RamdiskCreate = (PVOID)Buffer;

    if (argc <= 1)
    {
        Usage(argv[0]);
        // return 0;
    }

    wprintf(L"Press any key to continue...\n");
    _getch();

    RtlZeroMemory(&Buffer, sizeof(Buffer));
    RamdiskCreate->Version = sizeof(RAMDISK_CREATE_INPUT); // The real buffer size is given via the IOCTL
#if 1 /* Only Type 1 or 2 can be used externally */
    RamdiskCreate->DiskType = 1; // RAMDISK_MEMORY_MAPPED_DISK; // RAMDISK_REGISTRY_DISK;
    RamdiskCreate->Options.Readonly = TRUE;
    RamdiskCreate->Options.Fixed = TRUE;
    RamdiskCreate->Options.NoDriveLetter = TRUE; // FALSE;
    UuidCreate(&RamdiskCreate->DiskGuid);
    RamdiskCreate->DiskLength.QuadPart = 0x00300000ULL;
    RamdiskCreate->DiskOffset = 0;
    RamdiskCreate->ViewCount = 10;
    RamdiskCreate->ViewLength = PAGE_SIZE;
    wcscpy(RamdiskCreate->FileName, L"\\GLOBAL??\\C:\\ramdsk_file.vhd");
#else /* Type 4 is forbidden by driver (it's internal to driver), and type 3 is used only when we boot from "ramdisk(0)" */
    RamdiskCreate->DiskType = 4;
    RamdiskCreate->Options.Fixed = TRUE;
    RamdiskCreate->Options.NoDriveLetter = TRUE;
    RamdiskCreate->DiskGuid.Data1 = 3; // Floppy #3
    RamdiskCreate->DiskLength.QuadPart = 1440ULL * 1024;
    RamdiskCreate->BaseAddress = (PVOID)1;
#endif

    RtlInitUnicodeString(&DriverName, DD_RAMDISK_DEVICE_NAME_U);
    InitializeObjectAttributes(&ObjectAttributes,
                               &DriverName,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

#if 0
    DriverHandle = CreateFileW(DD_RAMDISK_DEVICE_NAME_U,
                               GENERIC_ALL | SYNCHRONIZE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL,
                               OPEN_EXISTING,
                               0,
                               NULL);
    if (DriverHandle == INVALID_HANDLE_VALUE)
#else
    Status = NtOpenFile(&DriverHandle,
                        GENERIC_ALL /*| SYNCHRONIZE*/,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ /*| FILE_SHARE_WRITE*/,
                        0/*FILE_SYNCHRONOUS_IO_NONALERT*/);
    if (!NT_SUCCESS(Status))
#endif
    {
        wprintf(L"RamDisk driver not installed, bail out...\n");
        goto Quit;
    }

#if 0
    Success = DeviceIoControl(DriverHandle,
                              FSCTL_CREATE_RAM_DISK,
                              RamdiskCreate, sizeof(Buffer),
                              NULL, 0,
                              NULL, NULL);
    if (!Success)
#else
    Status = NtDeviceIoControlFile(DriverHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   FSCTL_CREATE_RAM_DISK,
                                   RamdiskCreate, sizeof(Buffer),
                                   NULL, 0);
    if (!NT_SUCCESS(Status))
#endif
    {
        wprintf(L"FSCTL_CREATE_RAM_DISK failed, last error = %lu\n", GetLastError());
    }

#if 0
    CloseHandle(DriverHandle);
#else
    NtClose(DriverHandle);
#endif

Quit:
    wprintf(L"Press any key to quit...\n");
    _getch();

    return 0;
}
