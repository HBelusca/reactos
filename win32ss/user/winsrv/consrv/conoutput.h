/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Console Server DLL
 * FILE:            win32ss/user/winsrv/consrv/conoutput.h
 * PURPOSE:         Console Output functions
 * PROGRAMMERS:     Jeffrey Morlan
 *                  Hermes Belusca-Maito (hermes.belusca@sfr.fr)
 */

#pragma once

#define ConSrvGetTextModeBuffer(ProcessData, Handle, Ptr, Access)               \
    ConSrvGetObject((ProcessData), (Handle), (PCONSOLE_IO_OBJECT*)(Ptr), NULL,  \
                    (Access), TRUE, TEXTMODE_BUFFER)

#define ConSrvGetGraphicsBuffer(ProcessData, Handle, Ptr, Access)               \
    ConSrvGetObject((ProcessData), (Handle), (PCONSOLE_IO_OBJECT*)(Ptr), NULL,  \
                    (Access), TRUE, GRAPHICS_BUFFER)

#define ConSrvGetScreenBuffer(ProcessData, Handle, Ptr, Access)                 \
    ConSrvGetObject((ProcessData), (Handle), (PCONSOLE_IO_OBJECT*)(Ptr), NULL,  \
                    (Access), TRUE, SCREEN_BUFFER)

#define ConSrvReleaseScreenBuffer(Buff) \
    ConSrvReleaseObject(&(Buff)->Header, TRUE)

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
