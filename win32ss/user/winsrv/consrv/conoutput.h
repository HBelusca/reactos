/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Console Server DLL
 * FILE:            win32ss/user/winsrv/consrv/conoutput.h
 * PURPOSE:         Console Output functions
 * PROGRAMMERS:     Jeffrey Morlan
 *                  Hermes Belusca-Maito (hermes.belusca@sfr.fr)
 */

#pragma once

#define ConSrvGetTextModeBuffer(ProcessData, Handle, Ptr, Access)                   \
    ConSrvReferenceObjectByHandle((ProcessData), (Handle), (Access), HANDLE_OUTPUT, \
                    TEXTMODE_BUFFER, (PCONSOLE_IO_OBJECT*)(Ptr), NULL)

#define ConSrvGetGraphicsBuffer(ProcessData, Handle, Ptr, Access)                   \
    ConSrvReferenceObjectByHandle((ProcessData), (Handle), (Access), HANDLE_OUTPUT, \
                    GRAPHICS_BUFFER, (PCONSOLE_IO_OBJECT*)(Ptr), NULL)

#define ConSrvGetScreenBuffer(ProcessData, Handle, Ptr, Access)                     \
    ConSrvReferenceObjectByHandle((ProcessData), (Handle), (Access), HANDLE_OUTPUT, \
                    SCREEN_BUFFER, (PCONSOLE_IO_OBJECT*)(Ptr), NULL)

#define ConSrvReleaseScreenBuffer(Buff) \
    ConSrvDereferenceObject(&(Buff)->Header, HANDLE_OUTPUT)


/* CONSRV OBJECT CALLBACKS ****************************************************/
// OPEN_METHOD
NTSTATUS NTAPI
OpenScreenBuffer(
    IN PCONSOLE_IO_OBJECT_REFERENCE ObjectRef,
    /***IN PCONSOLE_IO_OBJECT Object,***/
    IN ACCESS_MASK GrantedAccess);

// OKAYTOCLOSE_METHOD
NTSTATUS NTAPI
OkayToCloseScreenBuffer(
    IN PCONSOLE_IO_OBJECT_REFERENCE ObjectRef,
    // IN ACCESS_MASK GrantedAccess,
    IN HANDLE Handle);

// CLOSE_METHOD
VOID NTAPI
CloseScreenBuffer(
    IN PCONSOLE_IO_OBJECT Object
    // IN ACCESS_MASK GrantedAccess
    );

// DELETE_METHOD
VOID NTAPI
DeleteScreenBuffer(
    IN /* PCONSOLE_OBJECT */ PCONSOLE_IO_OBJECT Object);


/* PRIVATE FUNCTIONS **********************************************************/

NTSTATUS ConDrvCreateScreenBuffer(OUT PCONSOLE_SCREEN_BUFFER* Buffer,
                                  IN PCONSOLE Console,
                                  IN HANDLE ProcessHandle OPTIONAL,
                                  IN ULONG BufferType,
                                  IN PVOID ScreenBufferInfo);
VOID NTAPI ConDrvDeleteScreenBuffer(PCONSOLE_SCREEN_BUFFER Buffer);
// VOID ConioSetActiveScreenBuffer(PCONSOLE_SCREEN_BUFFER Buffer);

PCONSOLE_SCREEN_BUFFER
ConDrvGetActiveScreenBuffer(IN PCONSOLE Console);

/* EOF */
