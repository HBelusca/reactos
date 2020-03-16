/*
 * LICENSE:         GPL - See COPYING in the top level directory
 * PROJECT:         ReactOS Console Server DLL
 * FILE:            win32ss/user/winsrv/consrv/history.h
 * PURPOSE:         Console line input functions
 * PROGRAMMERS:     Jeffrey Morlan
 */

#pragma once

typedef struct _HISTORY_BUFFER *PHISTORY_BUFFER;

//
// FIXME: This is temporary!! It should be replaced by
// HistoryFindBufferByProcess().
//
/*static*/ PHISTORY_BUFFER
HistoryCurrentBuffer(
    IN PCONSRV_CONSOLE Console,
    /**/IN PUNICODE_STRING ExeName,/**/
    IN HANDLE ProcessHandle);

VOID
HistoryAddEntry(
    IN PHISTORY_BUFFER Hist,
    IN PUNICODE_STRING Entry,
    IN BOOLEAN HistoryNoDup);

BOOLEAN
HistoryFindEntryByPrefix(
    IN PHISTORY_BUFFER Hist,
    IN BOOLEAN LineUpPressed,
    IN PUNICODE_STRING Prefix,
    OUT PUNICODE_STRING Entry);

VOID
HistoryGetCurrentEntry(
    IN PHISTORY_BUFFER Hist,
    OUT PUNICODE_STRING Entry);

BOOLEAN
HistoryRecallHistory(
    IN PHISTORY_BUFFER Hist,
    IN INT Offset,
    OUT PUNICODE_STRING Entry);

VOID
HistoryDeleteBuffer(
    IN PHISTORY_BUFFER Hist);

VOID
HistoryDeleteBuffers(
    IN PCONSRV_CONSOLE Console);

VOID
HistoryReshapeAllBuffers(
    IN PCONSRV_CONSOLE Console,
    IN ULONG HistoryBufferSize,
    IN ULONG MaxNumberOfHistoryBuffers,
    IN BOOLEAN HistoryNoDup);
