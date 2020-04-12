/*
 * LICENSE:         GPL - See COPYING in the top level directory
 * PROJECT:         ReactOS Console Server DLL
 * FILE:            win32ss/user/winsrv/consrv/handle.h
 * PURPOSE:         Console I/O Handles functions
 * PROGRAMMERS:     David Welch
 *                  Jeffrey Morlan
 *                  Hermes Belusca-Maito (hermes.belusca@sfr.fr)
 */

#pragma once

// This can be understood as a FILE_OBJECT
//
// FIXME: Call that CONSOLE_IO_REFERENCE ???
// Or: CONSOLE_IO_OBJECT_REFERENCE ?
// or: CONSOLE_IO_OBJECT_CONTEXT ?
// or: CONSOLE_IO_OBJECT_INSTANCE ?
//
typedef struct _CONSOLE_IO_OBJECT_REFERENCE
{
    PCONSOLE_IO_OBJECT Object;  /* The object referred to */
    PVOID Context;

    ACCESS_MASK Access;
    ULONG ShareMode;

} CONSOLE_IO_OBJECT_REFERENCE, *PCONSOLE_IO_OBJECT_REFERENCE;


typedef NTSTATUS
(NTAPI *OPEN_METHOD)(
    IN PCONSOLE_IO_OBJECT_REFERENCE ObjectRef,
    /***IN PCONSOLE_IO_OBJECT Object,***/
    IN ACCESS_MASK GrantedAccess);

typedef NTSTATUS
(NTAPI *OKAYTOCLOSE_METHOD)(
    IN PCONSOLE_IO_OBJECT_REFERENCE ObjectRef,
    // IN ACCESS_MASK GrantedAccess,
    IN HANDLE Handle);

// PCONSOLE_OBJECT Object
typedef VOID
(NTAPI *CLOSE_METHOD)(
    IN PCONSOLE_IO_OBJECT Object
    // IN ACCESS_MASK GrantedAccess
    );

typedef VOID
(NTAPI *DELETE_METHOD)(
    IN PCONSOLE_IO_OBJECT Object);



NTSTATUS
ConSrvCreateHandleTable(
    IN OUT PCONSOLE_PROCESS_DATA ProcessData);

VOID
ConSrvDestroyHandleTable(
    IN PCONSOLE_PROCESS_DATA ProcessData);

NTSTATUS
ConSrvInheritHandleTable(
    IN PCONSOLE_PROCESS_DATA SourceProcessData,
    IN PCONSOLE_PROCESS_DATA TargetProcessData);

VOID
ConSrvSweepHandleTable(
    IN PCONSOLE_PROCESS_DATA ProcessData);

/************/

NTSTATUS
ConSrvOpenObjectByPointer(
    IN PCONSOLE_PROCESS_DATA ProcessData,
    IN PCONSOLE_IO_OBJECT Object,
    IN CONSOLE_HANDLE_TYPE Type,
    IN ACCESS_MASK Access,
    IN BOOLEAN Inheritable,
    IN ULONG ShareMode,
    OUT PHANDLE Handle);

NTSTATUS
ConSrvOpenObjectByType(
    IN PCONSOLE_PROCESS_DATA ProcessData,
    IN PCONSRV_CONSOLE Console,
    IN CONSOLE_HANDLE_TYPE Type,
    IN CONSOLE_IO_OBJECT_TYPE IoType OPTIONAL,
    IN ACCESS_MASK Access, // DesiredAccess
    IN BOOLEAN Inheritable,
    IN ULONG ShareMode,
    OUT PHANDLE Handle);

NTSTATUS
ConSrvDuplicateObject(
    IN PCONSOLE_PROCESS_DATA SourceProcessData,
    IN HANDLE SourceHandle,
    IN PCONSOLE_PROCESS_DATA TargetProcessData,
    OUT PHANDLE TargetHandle OPTIONAL,
    IN ACCESS_MASK DesiredAccess,
    IN BOOLEAN Inheritable, // ULONG HandleAttributes
    IN ULONG Options);

NTSTATUS
ConSrvCloseHandle(
    IN PCONSOLE_PROCESS_DATA ProcessData,
    IN HANDLE Handle);



NTSTATUS
ConSrvReferenceObject(
    IN PCONSOLE_IO_OBJECT Object,
    /****/IN CONSOLE_HANDLE_TYPE Type/****/);

NTSTATUS
ConSrvReferenceObjectByPointer(
    IN PCONSOLE_IO_OBJECT Object,
    IN ACCESS_MASK Access,
    /****/IN CONSOLE_HANDLE_TYPE Type,/****/
    IN CONSOLE_IO_OBJECT_TYPE IoType OPTIONAL);

NTSTATUS
ConSrvReferenceObjectByHandle(
    IN PCONSOLE_PROCESS_DATA ProcessData,
    IN HANDLE Handle,
    IN ACCESS_MASK Access,
    IN CONSOLE_HANDLE_TYPE Type OPTIONAL,
    IN CONSOLE_IO_OBJECT_TYPE IoType OPTIONAL,
    OUT PCONSOLE_IO_OBJECT* Object,
    OUT PCONSOLE_IO_OBJECT_REFERENCE* Entry OPTIONAL    // BOF BOF !!!! Not PCONSOLE_IO_HANDLE*
    );

VOID
ConSrvDereferenceObject(
    IN PCONSOLE_IO_OBJECT Object,
    /****/IN CONSOLE_HANDLE_TYPE Type/****/);
