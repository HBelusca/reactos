/*
 * PROJECT:     ReactOS Console Utilities Library
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Slim readers/writers interface, including CRT and Win32 wrappers.
 * COPYRIGHT:   Copyright 2021 Hermes Belusca-Maito
 */

/**
 * @file    conrw.h
 * @ingroup ConUtils
 *
 * @brief   A header-only interface library for slim readers/writers.
 *          It also includes optional wrappers around CRT and Win32 APIs.
 **/

#ifndef __CONRW_H__
#define __CONRW_H__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef CONST VOID *PCVOID;

/* Forward declarations */
struct _CON_READER32;
struct _CON_READER;
struct _CON_WRITER32;
struct _CON_WRITER;


typedef BOOL
(__stdcall* CON_READER32_FUNC)(
    IN /*PCON_READER32*/ struct _CON_READER32* This,
    IN PVOID Buffer,
    IN ULONG BufferLength,
    OUT PULONG pReadLength OPTIONAL);

typedef struct _CON_READER32
{
    CON_READER32_FUNC __callFunc;
} CON_READER32, *PCON_READER32;


typedef BOOL
(__stdcall* CON_READER_FUNC)(
    IN /*PCON_READER*/ struct _CON_READER* This,
    IN PVOID Buffer,
    IN SIZE_T BufferLength,
    OUT PSIZE_T pReadLength OPTIONAL);

typedef struct _CON_READER
{
    CON_READER_FUNC __callFunc;
} CON_READER, *PCON_READER;


typedef BOOL
(__stdcall* CON_WRITER32_FUNC)(
    IN /*PCON_WRITER32*/ struct _CON_WRITER32* This,
    IN PCVOID Buffer,
    IN ULONG BufferLength,
    OUT PULONG pWrittenLength OPTIONAL);

typedef struct _CON_WRITER32
{
    CON_WRITER32_FUNC __callFunc;
} CON_WRITER32, *PCON_WRITER32;


typedef BOOL
(__stdcall* CON_WRITER_FUNC)(
    IN /*PCON_WRITER*/ struct _CON_WRITER* This,
    IN PCVOID Buffer,
    IN SIZE_T BufferLength,
    OUT PSIZE_T pWrittenLength OPTIONAL);

typedef struct _CON_WRITER
{
    CON_WRITER_FUNC __callFunc;
} CON_WRITER, *PCON_WRITER;


#define __CALL_RW_PARAMS(...) __VA_ARGS__)

#define CALL_RW(rwObj) \
    (rwObj)->__callFunc((rwObj), __CALL_RW_PARAMS

#define GET_R32(rObj)   ((struct _CON_READER32*)(rObj))
#define GET_R(rObj)     ((struct _CON_READER*)(rObj))
#define GET_W32(wObj)   ((struct _CON_WRITER32*)(wObj))
#define GET_W(wObj)     ((struct _CON_WRITER*)(wObj))

#define CALL_R32(rObj)  CALL_RW(GET_R32(rObj))
#define CALL_R(rObj)    CALL_RW(GET_R(rObj))
#define CALL_W32(wObj)  CALL_RW(GET_W32(wObj))
#define CALL_W(wObj)    CALL_RW(GET_W(wObj))


#ifdef __cplusplus
#pragma push_macro("DUMMYSTRUCTNAME")
#undef DUMMYSTRUCTNAME
#define DUMMYSTRUCTNAME s
#endif

/**
 * @brief   CRT reader/writer.
 **/

#ifdef USE_CRT_READWRITE

typedef struct _CRT_READER
{
    CON_READER DUMMYSTRUCTNAME;
    FILE* fStream;
} CRT_READER, *PCRT_READER;

__inline
BOOL __stdcall
CrtReader(
    IN PCRT_READER This,
    IN PVOID Buffer,
    IN SIZE_T BufferLength,
    OUT PSIZE_T pReadLength OPTIONAL)
{
    SIZE_T ReadLength = fread(Buffer, 1, BufferLength, This->fStream);
    if (pReadLength) *pReadLength = ReadLength;
    return ferror(This->fStream);
}

#define DEFINE_CRT_READER_(Reader, stream) \
    {Reader, stream}

#define DEFINE_CRT_READER(stream) \
    DEFINE_CRT_READER_(CrtReader, stream)


typedef struct _CRT_WRITER
{
    CON_WRITER DUMMYSTRUCTNAME;
    FILE* fStream;
} CRT_WRITER, *PCRT_WRITER;

__inline
BOOL __stdcall
CrtWriter(
    IN PCRT_WRITER This,
    IN PCVOID Buffer,
    IN SIZE_T BufferLength,
    OUT PSIZE_T pWrittenLength OPTIONAL)
{
    SIZE_T WrittenLength = fwrite(Buffer, 1, BufferLength, This->fStream);
    if (pWrittenLength) *pWrittenLength = WrittenLength;
    return ferror(This->fStream);
}

#define DEFINE_CRT_WRITER_(Writer, stream) \
    {Writer, stream}

#define DEFINE_CRT_WRITER(stream) \
    DEFINE_CRT_WRITER_(CrtWriter, stream)

#endif // USE_CRT_READWRITE


/**
 * @brief   Win32 reader/writer.
 **/

#ifdef USE_WIN32_READWRITE

typedef struct _WIN32_READER32
{
    CON_READER32 DUMMYSTRUCTNAME;
    HANDLE hHandle;
} WIN32_READER32, *PWIN32_READER32;

__inline
BOOL __stdcall
Win32Read32(
    // IN PWIN32_READER32 This,
    IN PCON_READER32 This,
    IN PVOID Buffer,
    IN ULONG BufferLength,
    OUT PULONG pReadLength OPTIONAL)
{
    return ReadFile(((PWIN32_READER32)This)->hHandle, Buffer, BufferLength, pReadLength, NULL);
}

#define DEFINE_WIN32_READER32_(Reader, hHandle) \
    {Reader, hHandle}

#define DEFINE_WIN32_READER32(hHandle) \
    DEFINE_WIN32_READER32_(Win32Read32, hHandle)


typedef struct _WIN32_READER
{
    CON_READER DUMMYSTRUCTNAME;
    HANDLE hHandle;
} WIN32_READER, *PWIN32_READER;

__inline
BOOL __stdcall
Win32Read(
    // IN PWIN32_READER This,
    IN PCON_READER This,
    IN PVOID Buffer,
    IN SIZE_T BufferLength,
    OUT PSIZE_T pReadLength OPTIONAL)
{
#if !defined(_WIN64)
    C_ASSERT(sizeof(SIZE_T) == sizeof(ULONG));
    return ReadFile(((PWIN32_READER)This)->hHandle, Buffer, (ULONG)BufferLength, (PULONG)pReadLength, NULL);
#elif defined(_WIN64)
    BOOL bSuccess = TRUE; /* Assume success */
    ULONG BufLen32;
    ULONG ReadLen32 = 0;
    SIZE_T ReadLength = 0;

    if (BufferLength <= ULONG_MAX)
    {
        BufLen32 = (ULONG)BufferLength;
        bSuccess = ReadFile(((PWIN32_READER)This)->hHandle, Buffer, BufLen32, &ReadLen32, NULL);
        ReadLength = ReadLen32;
    }
    else
    {
        while (BufferLength > 0)
        {
            BufLen32 = (ULONG)min(BufferLength, ULONG_MAX);
            bSuccess = ReadFile(((PWIN32_READER)This)->hHandle, Buffer, BufLen32, &ReadLen32, NULL);
            ReadLength += ReadLen32;
            if (!bSuccess)
                break;
            Buffer = (PVOID)((ULONG_PTR)Buffer + BufLen32);
            BufferLength -= BufLen32;
        }
    }
    if (pReadLength) *pReadLength = ReadLength;
    return bSuccess;
#else
#error Unrecognized CPU bitness.
#endif
}

#define DEFINE_WIN32_READER_(Reader, hHandle) \
    {Reader, hHandle}

#define DEFINE_WIN32_READER(hHandle) \
    DEFINE_WIN32_READER_(Win32Read, hHandle)


typedef struct _WIN32_WRITER32
{
    CON_WRITER32 DUMMYSTRUCTNAME;
    HANDLE hHandle;
} WIN32_WRITER32, *PWIN32_WRITER32;

__inline
BOOL __stdcall
Win32Write32(
    // IN PWIN32_WRITER32 This,
    IN PCON_WRITER32 This,
    IN PCVOID Buffer,
    IN ULONG BufferLength,
    OUT PULONG pWrittenLength OPTIONAL)
{
    return WriteFile(((PWIN32_WRITER32)This)->hHandle, Buffer, BufferLength, pWrittenLength, NULL);
}

#define DEFINE_WIN32_WRITER32_(Writer, hHandle) \
    {Writer, hHandle}

#define DEFINE_WIN32_WRITER32(hHandle) \
    DEFINE_WIN32_WRITER32_(Win32Write32, hHandle)


typedef struct _WIN32_WRITER
{
    CON_WRITER DUMMYSTRUCTNAME;
    HANDLE hHandle;
} WIN32_WRITER, *PWIN32_WRITER;

__inline
BOOL __stdcall
Win32Write(
    // IN PWIN32_WRITER This,
    IN PCON_WRITER This,
    IN PCVOID Buffer,
    IN SIZE_T BufferLength,
    OUT PSIZE_T pWrittenLength OPTIONAL)
{
#if !defined(_WIN64)
    C_ASSERT(sizeof(SIZE_T) == sizeof(ULONG));
    return WriteFile(((PWIN32_WRITER)This)->hHandle, Buffer, (ULONG)BufferLength, (PULONG)pWrittenLength, NULL);
#elif defined(_WIN64)
    BOOL bSuccess = TRUE; /* Assume success */
    ULONG BufLen32;
    ULONG WrittenLen32 = 0;
    SIZE_T WrittenLength = 0;

    if (BufferLength <= ULONG_MAX)
    {
        BufLen32 = (ULONG)BufferLength;
        bSuccess = WriteFile(((PWIN32_WRITER)This)->hHandle, Buffer, BufLen32, &WrittenLen32, NULL);
        WrittenLength = WrittenLen32;
    }
    else
    {
        while (BufferLength > 0)
        {
            BufLen32 = (ULONG)min(BufferLength, ULONG_MAX);
            bSuccess = WriteFile(((PWIN32_WRITER)This)->hHandle, Buffer, BufLen32, &WrittenLen32, NULL);
            WrittenLength += WrittenLen32;
            if (!bSuccess)
                break;
            Buffer = (PCVOID)((ULONG_PTR)Buffer + BufLen32);
            BufferLength -= BufLen32;
        }
    }
    if (pWrittenLength) *pWrittenLength = WrittenLength;
    return bSuccess;
#else
#error Unrecognized CPU bitness.
#endif
}

#define DEFINE_WIN32_WRITER_(Writer, hHandle) \
    {Writer, hHandle}

#define DEFINE_WIN32_WRITER(hHandle) \
    DEFINE_WIN32_WRITER_(Win32Write, hHandle)

#endif // USE_WIN32_READWRITE

#ifdef __cplusplus
#pragma pop_macro("DUMMYSTRUCTNAME")
#endif

#ifdef __cplusplus
}
#endif

#endif  /* __CONRW_H__ */

/* EOF */
