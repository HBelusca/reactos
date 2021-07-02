/*
 * PROJECT:     ReactOS Console Utilities Library
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Print helper functions.
 * COPYRIGHT:   Copyright 2017-2021 Hermes Belusca-Maito
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

INT
ConPuts(
    IN PCON_STREAM Stream,
    IN PCWSTR szStr);

INT
ConPrintfV(
    IN PCON_STREAM Stream,
    IN PCWSTR  szStr,
    IN va_list args);

INT
__cdecl
ConPrintf(
    IN PCON_STREAM Stream,
    IN PCWSTR szStr,
    ...);


/*
 * String resources
 */

INT
ConResPutsEx(
    IN PCON_STREAM Stream,
    IN HINSTANCE hInstance OPTIONAL,
    IN UINT   uID,
    IN LANGID LanguageId);

INT
ConResPuts(
    IN PCON_STREAM Stream,
    IN UINT uID);

INT
ConResPrintfExV(
    IN PCON_STREAM Stream,
    IN HINSTANCE hInstance OPTIONAL,
    IN UINT    uID,
    IN LANGID  LanguageId,
    IN va_list args);

INT
ConResPrintfV(
    IN PCON_STREAM Stream,
    IN UINT    uID,
    IN va_list args);

INT
__cdecl
ConResPrintfEx(
    IN PCON_STREAM Stream,
    IN HINSTANCE hInstance OPTIONAL,
    IN UINT   uID,
    IN LANGID LanguageId,
    ...);

INT
__cdecl
ConResPrintf(
    IN PCON_STREAM Stream,
    IN UINT uID,
    ...);


/*
 * Message resources
 */

INT
ConMsgPuts(
    IN PCON_STREAM Stream,
    IN DWORD   dwFlags,
    IN LPCVOID lpSource OPTIONAL,
    IN DWORD   dwMessageId,
    IN DWORD   dwLanguageId);

INT
ConMsgPrintf2V(
    IN PCON_STREAM Stream,
    IN DWORD   dwFlags,
    IN LPCVOID lpSource OPTIONAL,
    IN DWORD   dwMessageId,
    IN DWORD   dwLanguageId,
    IN va_list args);

INT
ConMsgPrintfV(
    IN PCON_STREAM Stream,
    IN DWORD   dwFlags,
    IN LPCVOID lpSource OPTIONAL,
    IN DWORD   dwMessageId,
    IN DWORD   dwLanguageId,
    IN va_list *Arguments OPTIONAL);

INT
__cdecl
ConMsgPrintf(
    IN PCON_STREAM Stream,
    IN DWORD   dwFlags,
    IN LPCVOID lpSource OPTIONAL,
    IN DWORD   dwMessageId,
    IN DWORD   dwLanguageId,
    ...);


/*
 * Message resources as string resources
 */

INT
ConResMsgPrintfExV(
    IN PCON_STREAM Stream,
    IN HINSTANCE hInstance OPTIONAL,
    IN DWORD   dwFlags,
    IN UINT    uID,
    IN LANGID  LanguageId,
    IN va_list *Arguments OPTIONAL);

INT
ConResMsgPrintfV(
    IN PCON_STREAM Stream,
    IN DWORD   dwFlags,
    IN UINT    uID,
    IN va_list *Arguments OPTIONAL);

INT
__cdecl
ConResMsgPrintfEx(
    IN PCON_STREAM Stream,
    IN HINSTANCE hInstance OPTIONAL,
    IN DWORD  dwFlags,
    IN UINT   uID,
    IN LANGID LanguageId,
    ...);

INT
__cdecl
ConResMsgPrintf(
    IN PCON_STREAM Stream,
    IN DWORD dwFlags,
    IN UINT  uID,
    ...);

#ifdef __cplusplus
}
#endif

#endif  /* __CONPRINT_H__ */

/* EOF */
