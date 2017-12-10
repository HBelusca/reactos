/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Console Utilities Library
 * FILE:            sdk/lib/conutils/history.h
 * PURPOSE:         Text-line History and Aliases management for Console applications.
 * PROGRAMMERS:     Hermes Belusca-Maito
 */

#ifndef __HISTORY_H__
#define __HISTORY_H__

#pragma once

#include <wincon.h>  // Console APIs (only if kernel32 support included)


/* Console API functions which are absent from wincon.h */

#define EXENAME_LENGTH (255 + 1)

DWORD
WINAPI
GetConsoleInputExeNameW(IN DWORD nBufferLength,
                        OUT LPWSTR lpExeName);

DWORD
WINAPI
GetConsoleInputExeNameA(IN DWORD nBufferLength,
                        OUT LPSTR lpExeName);

BOOL
WINAPI
SetConsoleInputExeNameW(IN LPCWSTR lpExeName);

BOOL
WINAPI
SetConsoleInputExeNameA(IN LPCSTR lpExeName);

VOID
WINAPI
ExpungeConsoleCommandHistoryW(IN LPCWSTR lpExeName);

VOID
WINAPI
ExpungeConsoleCommandHistoryA(IN LPCSTR lpExeName);

DWORD
WINAPI
GetConsoleCommandHistoryW(OUT LPWSTR lpHistory,
                          IN DWORD cbHistory,
                          IN LPCWSTR lpExeName);

DWORD
WINAPI
GetConsoleCommandHistoryA(OUT LPSTR lpHistory,
                          IN DWORD cbHistory,
                          IN LPCSTR lpExeName);

DWORD
WINAPI
GetConsoleCommandHistoryLengthW(IN LPCWSTR lpExeName);

DWORD
WINAPI
GetConsoleCommandHistoryLengthA(IN LPCSTR lpExeName);

BOOL
WINAPI
SetConsoleNumberOfCommandsW(IN DWORD dwNumCommands,
                            IN LPCWSTR lpExeName);

BOOL
WINAPI
SetConsoleNumberOfCommandsA(IN DWORD dwNumCommands,
                            IN LPCSTR lpExeName);

#ifdef _UNICODE
    #define GetConsoleInputExeName GetConsoleInputExeNameW
    #define SetConsoleInputExeName SetConsoleInputExeNameW
    #define ExpungeConsoleCommandHistory ExpungeConsoleCommandHistoryW
    #define GetConsoleCommandHistory GetConsoleCommandHistoryW
    #define GetConsoleCommandHistoryLength GetConsoleCommandHistoryLengthW
    #define SetConsoleNumberOfCommands SetConsoleNumberOfCommandsW
#else
    #define GetConsoleInputExeName GetConsoleInputExeNameA
    #define SetConsoleInputExeName SetConsoleInputExeNameA
    #define ExpungeConsoleCommandHistory ExpungeConsoleCommandHistoryA
    #define GetConsoleCommandHistory GetConsoleCommandHistoryA
    #define GetConsoleCommandHistoryLength GetConsoleCommandHistoryLengthA
    #define SetConsoleNumberOfCommands SetConsoleNumberOfCommandsA
#endif





BOOL
ConAddToHistoryEx(
    IN HANDLE hConsoleInput,
    IN HANDLE hConsoleOutput,
    IN LPCTSTR ExeName OPTIONAL,
    IN LPCTSTR Line);

BOOL
ConAddToHistory(
    IN LPCTSTR ExeName OPTIONAL,
    IN LPCTSTR Line);

BOOL
ConLoadHistory(
    IN LPCTSTR ExeName OPTIONAL,
    IN LPCTSTR FileName);

BOOL
ConGetHistoryBuffer(
    IN LPCTSTR ExeName OPTIONAL,
    OUT LPTSTR* lpHistory,
    OUT PDWORD pcbHistory);

BOOL
ConSaveHistory(
    IN LPCTSTR ExeName OPTIONAL,
    IN LPCTSTR FileName);

BOOL
ConSetHistorySize(
    IN LPCTSTR ExeName OPTIONAL,
    IN DWORD dwSize);

#if (_WIN32_WINNT >= _WIN32_WINNT_VISTA)

BOOL
ConGetHistoryInfo(
    OUT PUINT pHistoryBufferSize,
    OUT PUINT pNumberOfHistoryBuffers,
    OUT PDWORD pdwFlags);

BOOL
ConSetHistoryInfo(
    IN UINT HistoryBufferSize,
    IN UINT NumberOfHistoryBuffers,
    IN DWORD dwFlags);

#endif

VOID
ConClearHistory(
    IN LPCTSTR ExeName OPTIONAL);








BOOL
ConAddAlias(
    IN LPCTSTR ExeName OPTIONAL,
    IN LPCTSTR Source,
    IN LPCTSTR Target);

DWORD
ConGetAlias(
    IN LPCTSTR ExeName OPTIONAL,
    IN  LPCTSTR Source,
    OUT LPTSTR  TargetBuffer,
    IN  DWORD   TargetBufferLength);

BOOL
ConGetAliasesList(
    IN LPCTSTR ExeName OPTIONAL,
    OUT LPTSTR* lpAliasesList,
    OUT PDWORD  pcbAliasesListLength);

BOOL
ConGetAliasExesList(
    OUT LPTSTR* lpExeNameList,
    OUT PDWORD  pcbExeNameListLength);

VOID
ConClearAliases(
    IN LPCTSTR ExeName OPTIONAL);

BOOL
ConLoadAliases(
    IN LPCTSTR ExeName OPTIONAL,
    IN LPCTSTR FileName);

BOOL
ConSaveAliases(
    IN LPCTSTR ExeName OPTIONAL,
    IN LPCTSTR FileName);



#endif  /* __HISTORY_H__ */

/* EOF */
