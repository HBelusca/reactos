/*
 * PROJECT:     ReactOS Console Utilities Library
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Print helper functions.
 * COPYRIGHT:   Copyright 2017-2018 ReactOS Team
 *              Copyright 2017-2021 Hermes Belusca-Maito
 */

/**
 * @file    conprint.h
 * @ingroup ConUtils
 *
 * @brief   Console I/O utility API -- Print helper functions.
 **/

#ifndef __CONPRINT_H__
#define __CONPRINT_H__

#pragma once

#ifndef _UNICODE
#error The ConUtils library at the moment only supports compilation with _UNICODE defined!
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Strings
 */

DWORD
ConWrite(
    IN PCON_WRITER32 Writer,
    IN PCTCH szStr,
    IN DWORD Len);

DWORD
ConPuts(
    IN PCON_WRITER32 Writer,
    IN PCWSTR szStr);

DWORD
ConPrintfV(
    IN PCON_WRITER32 Writer,
    IN PCWSTR  szStr,
    IN va_list args);

DWORD
__cdecl
ConPrintf(
    IN PCON_WRITER32 Writer,
    IN PCWSTR szStr,
    ...);


/*
 * String resources
 */

DWORD
ConResPutsEx(
    IN PCON_WRITER32 Writer,
    IN HINSTANCE hInstance OPTIONAL,
    IN UINT   uID,
    IN LANGID LanguageId);

DWORD
ConResPuts(
    IN PCON_WRITER32 Writer,
    IN UINT uID);

DWORD
ConResPrintfExV(
    IN PCON_WRITER32 Writer,
    IN HINSTANCE hInstance OPTIONAL,
    IN UINT    uID,
    IN LANGID  LanguageId,
    IN va_list args);

DWORD
ConResPrintfV(
    IN PCON_WRITER32 Writer,
    IN UINT    uID,
    IN va_list args);

DWORD
__cdecl
ConResPrintfEx(
    IN PCON_WRITER32 Writer,
    IN HINSTANCE hInstance OPTIONAL,
    IN UINT   uID,
    IN LANGID LanguageId,
    ...);

DWORD
__cdecl
ConResPrintf(
    IN PCON_WRITER32 Writer,
    IN UINT uID,
    ...);


/*
 * Message resources
 */

DWORD
ConMsgPuts(
    IN PCON_WRITER32 Writer,
    IN DWORD   dwFlags,
    IN LPCVOID lpSource OPTIONAL,
    IN DWORD   dwMessageId,
    IN DWORD   dwLanguageId);

DWORD
ConMsgPrintf2V(
    IN PCON_WRITER32 Writer,
    IN DWORD   dwFlags,
    IN LPCVOID lpSource OPTIONAL,
    IN DWORD   dwMessageId,
    IN DWORD   dwLanguageId,
    IN va_list args);

DWORD
ConMsgPrintfV(
    IN PCON_WRITER32 Writer,
    IN DWORD   dwFlags,
    IN LPCVOID lpSource OPTIONAL,
    IN DWORD   dwMessageId,
    IN DWORD   dwLanguageId,
    IN va_list* Arguments OPTIONAL);

DWORD
__cdecl
ConMsgPrintf(
    IN PCON_WRITER32 Writer,
    IN DWORD   dwFlags,
    IN LPCVOID lpSource OPTIONAL,
    IN DWORD   dwMessageId,
    IN DWORD   dwLanguageId,
    ...);


/*
 * Message resources as string resources
 */

DWORD
ConResMsgPrintfExV(
    IN PCON_WRITER32 Writer,
    IN HINSTANCE hInstance OPTIONAL,
    IN DWORD   dwFlags,
    IN UINT    uID,
    IN LANGID  LanguageId,
    IN va_list* Arguments OPTIONAL);

DWORD
ConResMsgPrintfV(
    IN PCON_WRITER32 Writer,
    IN DWORD   dwFlags,
    IN UINT    uID,
    IN va_list* Arguments OPTIONAL);

DWORD
__cdecl
ConResMsgPrintfEx(
    IN PCON_WRITER32 Writer,
    IN HINSTANCE hInstance OPTIONAL,
    IN DWORD  dwFlags,
    IN UINT   uID,
    IN LANGID LanguageId,
    ...);

DWORD
__cdecl
ConResMsgPrintf(
    IN PCON_WRITER32 Writer,
    IN DWORD dwFlags,
    IN UINT  uID,
    ...);

#ifdef __cplusplus
}
#endif

#endif  /* __CONPRINT_H__ */

/* EOF */
