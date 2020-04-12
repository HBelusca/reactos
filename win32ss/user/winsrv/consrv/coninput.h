/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Console Server DLL
 * FILE:            win32ss/user/winsrv/consrv/coninput.h
 * PURPOSE:         Console Input functions
 * PROGRAMMERS:     Jeffrey Morlan
 *                  Hermes Belusca-Maito (hermes.belusca@sfr.fr)
 */

#pragma once

typedef struct ConsoleInput_t
{
    LIST_ENTRY ListEntry;
    INPUT_RECORD InputEvent;
} ConsoleInput;


/* CONSRV OBJECT CALLBACKS ****************************************************/

// OPEN_METHOD
NTSTATUS NTAPI
OpenInputBuffer(
    IN PCONSOLE_IO_OBJECT_REFERENCE ObjectRef,
    /***IN PCONSOLE_IO_OBJECT Object,***/
    IN ACCESS_MASK GrantedAccess);

// OKAYTOCLOSE_METHOD
NTSTATUS NTAPI
OkayToCloseInputBuffer(
    IN PCONSOLE_IO_OBJECT_REFERENCE ObjectRef,
    // IN ACCESS_MASK GrantedAccess,
    IN HANDLE Handle);

// CLOSE_METHOD
VOID NTAPI
CloseInputBuffer(
    IN PCONSOLE_IO_OBJECT Object
    // IN ACCESS_MASK GrantedAccess
    );
// DELETE_METHOD
VOID NTAPI
DeleteInputBuffer(
    IN /* PCONSOLE_OBJECT */ PCONSOLE_IO_OBJECT Object);


/* PRIVATE FUNCTIONS **********************************************************/

NTSTATUS NTAPI
ConDrvInitInputBuffer(IN PCONSOLE Console,
                      IN ULONG InputBufferSize);
VOID NTAPI
ConDrvDeinitInputBuffer(IN PCONSOLE Console);
