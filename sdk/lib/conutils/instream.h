/*
 * PROJECT:     ReactOS Console Utilities Library
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Provides basic abstraction wrappers around CRT streams or
 *              Win32 console API I/O functions, to deal with i18n + Unicode
 *              related problems.
 * COPYRIGHT:   Copyright 2017-2018 ReactOS Team
 *              Copyright 2017-2018 Hermes Belusca-Maito
 */

/**
 * @file    instream.h
 * @ingroup ConUtils
 *
 * @brief   Console I/O utility API -- Input
 **/

#ifndef __INSTREAM_H__
#define __INSTREAM_H__

#pragma once

#ifndef _UNICODE
#error The ConUtils library at the moment only supports compilation with _UNICODE defined!
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Shadow type, implementation-specific
// typedef struct _CON_STREAM CON_STREAM, *PCON_STREAM;


#ifdef __cplusplus
}
#endif

#endif  /* __INSTREAM_H__ */

/* EOF */
