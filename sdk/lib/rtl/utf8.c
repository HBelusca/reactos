/*
 * COPYRIGHT:         GPL v2 - See COPYING in the top level directory
 * PROJECT:           ReactOS system libraries
 * FILE:              lib/rtl/nls.c
 * PURPOSE:           UTF-8 Support functions
 * PROGRAMMERS:       Thomas Faber <thomas.faber@reactos.org>
 */

/* INCLUDES ******************************************************************/

#include "rtl_vista.h"

#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

#define IS_HIGH_SURROGATE(ch)       ((ch) >= 0xd800 && (ch) <= 0xdbff)
#define IS_LOW_SURROGATE(ch)        ((ch) >= 0xdc00 && (ch) <= 0xdfff)

// _IRQL_requires_max_(PASSIVE_LEVEL)
// _Must_inspect_result_
NTSTATUS
NTAPI
RtlUnicodeToUTF8N(
    _Out_writes_bytes_to_(UTF8StringMaxByteCount, *UTF8StringActualByteCount)
        PCHAR UTF8StringDestination,
    _In_ ULONG UTF8StringMaxByteCount,
    _Out_ PULONG UTF8StringActualByteCount,
    _In_reads_bytes_(UnicodeStringByteCount) PCWCH UnicodeStringSource,
    _In_ ULONG UnicodeStringByteCount)
{
    NTSTATUS Status;
    ULONG i;
    ULONG written;
    ULONG ch;
    UCHAR utf8_ch[4];
    ULONG utf8_ch_len;

    if (!UnicodeStringSource)
        return STATUS_INVALID_PARAMETER_4;
    if (!UTF8StringActualByteCount)
        return STATUS_INVALID_PARAMETER;
    if (UTF8StringDestination && (UnicodeStringByteCount % sizeof(WCHAR)))
        return STATUS_INVALID_PARAMETER_5;

// TODO: if UTF8StringDestination == NULL but UTF8StringActualByteCount != NULL,
// return in UTF8StringActualByteCount the number of bytes required to contain
// the entire output string.

    written = 0;
    Status = STATUS_SUCCESS;

    for (i = 0; i < UnicodeStringByteCount / sizeof(WCHAR); i++)
    {
        /* Decode UTF-16 into ch */
        ch = UnicodeStringSource[i];
        if (IS_LOW_SURROGATE(ch))
        {
            ch = 0xfffd;
            Status = STATUS_SOME_NOT_MAPPED;
        }
        else if (IS_HIGH_SURROGATE(ch))
        {
            if (i + 1 < UnicodeStringByteCount / sizeof(WCHAR))
            {
                ch -= 0xd800;
                ch <<= 10;
                if (IS_LOW_SURROGATE(UnicodeStringSource[i + 1]))
                {
                    ch |= UnicodeStringSource[i + 1] - 0xdc00;
                    ch += 0x010000;
                    i++;
                }
                else
                {
                    ch = 0xfffd;
                    Status = STATUS_SOME_NOT_MAPPED;
                }
            }
            else
            {
                ch = 0xfffd;
                Status = STATUS_SOME_NOT_MAPPED;
            }
        }

        /* Encode ch as UTF-8 */
        ASSERT(ch <= 0x10ffff);
        if (ch < 0x80)
        {
            /* 1-byte character */
            utf8_ch[0] = ch & 0x7f;
            utf8_ch_len = 1;
        }
        else if (ch < 0x800)
        {
            /* 110xxxxx: 1+1-byte encoded character */
            utf8_ch[0] = 0xc0 | (ch >>  6 & 0x1f);
            utf8_ch[1] = 0x80 | (ch >>  0 & 0x3f);
            utf8_ch_len = 2;
        }
        else if (ch < 0x10000)
        {
            /* 1110xxxx: 1+2-byte encoded character */
            utf8_ch[0] = 0xe0 | (ch >> 12 & 0x0f);
            utf8_ch[1] = 0x80 | (ch >>  6 & 0x3f);
            utf8_ch[2] = 0x80 | (ch >>  0 & 0x3f);
            utf8_ch_len = 3;
        }
        else if (ch < 0x200000)
        {
            /* 11110xxx: 1+3-byte encoded character */
            utf8_ch[0] = 0xf0 | (ch >> 18 & 0x07);
            utf8_ch[1] = 0x80 | (ch >> 12 & 0x3f);
            utf8_ch[2] = 0x80 | (ch >>  6 & 0x3f);
            utf8_ch[3] = 0x80 | (ch >>  0 & 0x3f);
            utf8_ch_len = 4;
        }
#if 0 /* Extensions to the UTF-8 encoding */
        else if (ch < 0x04000000)
        {
            /* 111110xx: 1+4-byte encoded character */
            utf8_ch[0] = 0xf8 | (ch >> 24 & 0x03);
            utf8_ch[1] = 0x80 | (ch >> 18 & 0x3f);
            utf8_ch[2] = 0x80 | (ch >> 12 & 0x3f);
            utf8_ch[3] = 0x80 | (ch >>  6 & 0x3f);
            utf8_ch[4] = 0x80 | (ch >>  0 & 0x3f);
            utf8_ch_len = 5;
        }
        else if (ch < 0x80000000)
        {
            /* 1111110x: 1+5-byte encoded character */
            utf8_ch[0] = 0xfc | (ch >> 30 & 0x01);
            utf8_ch[1] = 0x80 | (ch >> 24 & 0x3f);
            utf8_ch[2] = 0x80 | (ch >> 18 & 0x3f);
            utf8_ch[3] = 0x80 | (ch >> 12 & 0x3f);
            utf8_ch[4] = 0x80 | (ch >>  6 & 0x3f);
            utf8_ch[5] = 0x80 | (ch >>  0 & 0x3f);
            utf8_ch_len = 6;
        }
#endif

        if (!UTF8StringDestination)
        {
            written += utf8_ch_len;
            continue;
        }

        if (UTF8StringMaxByteCount >= utf8_ch_len)
        {
            RtlCopyMemory(UTF8StringDestination, utf8_ch, utf8_ch_len);
            UTF8StringDestination += utf8_ch_len;
            UTF8StringMaxByteCount -= utf8_ch_len;
            written += utf8_ch_len;
        }
        else
        {
            UTF8StringMaxByteCount = 0;
            Status = STATUS_BUFFER_TOO_SMALL;
        }
    }

    *UTF8StringActualByteCount = written;
    return Status;
}


// _IRQL_requires_max_(PASSIVE_LEVEL)
// _Must_inspect_result_
NTSTATUS
NTAPI
RtlUTF8ToUnicodeN(
    _Out_writes_bytes_to_(UnicodeStringMaxByteCount, *UnicodeStringActualByteCount)
        PWSTR UnicodeStringDestination,
    _In_ ULONG UnicodeStringMaxByteCount,
    _Out_ PULONG UnicodeStringActualByteCount,
    _In_reads_bytes_(UTF8StringByteCount) PCCH UTF8StringSource,
    _In_ ULONG UTF8StringByteCount)
{
    NTSTATUS Status;
    ULONG i, j;
    ULONG written;
    ULONG ch;
    ULONG utf8_trail_bytes;
    WCHAR utf16_ch[3];
    ULONG utf16_ch_len;

    if (!UTF8StringSource)
        return STATUS_INVALID_PARAMETER_4;
    if (!UnicodeStringActualByteCount)
        return STATUS_INVALID_PARAMETER;

    written = 0;
    Status = STATUS_SUCCESS;

    for (i = 0; i < UTF8StringByteCount; i++)
    {
        /* Read UTF-8 lead byte */
        ch = (BYTE)UTF8StringSource[i];
        utf8_trail_bytes = 0;
        if (ch >= 0xf5)
        {
            ch = 0xfffd;
            Status = STATUS_SOME_NOT_MAPPED;
        }
        else if (ch >= 0xf0)
        {
            ch &= 0x07;
            utf8_trail_bytes = 3;
        }
        else if (ch >= 0xe0)
        {
            ch &= 0x0f;
            utf8_trail_bytes = 2;
        }
        else if (ch >= 0xc2)
        {
            ch &= 0x1f;
            utf8_trail_bytes = 1;
        }
        else if (ch >= 0x80)
        {
            /* Overlong or trail byte */
            ch = 0xfffd;
            Status = STATUS_SOME_NOT_MAPPED;
        }

        /* Read UTF-8 trail bytes */
        if (i + utf8_trail_bytes < UTF8StringByteCount)
        {
            for (j = 0; j < utf8_trail_bytes; j++)
            {
                /* The trail bytes should all start with 10xxxxxx */
                if ((UTF8StringSource[i + 1] & 0xc0) == 0x80)
                {
                    ch <<= 6;
                    ch |= UTF8StringSource[i + 1] & 0x3f;
                    i++;
                }
                else
                {
                    ch = 0xfffd;
                    utf8_trail_bytes = 0;
                    Status = STATUS_SOME_NOT_MAPPED;
                    break;
                }
            }
        }
        else
        {
            ch = 0xfffd;
            utf8_trail_bytes = 0;
            Status = STATUS_SOME_NOT_MAPPED;
            i = UTF8StringByteCount;
        }

        /* Encode ch as UTF-16 */
        if ((ch > 0x10ffff) ||
            IS_HIGH_SURROGATE(ch) || IS_LOW_SURROGATE(ch) ||
            // (ch >= 0xd800 && ch <= 0xdfff) ||
            (utf8_trail_bytes == 2 && ch < 0x00800) ||
            (utf8_trail_bytes == 3 && ch < 0x10000))
        {
            /* Invalid codepoint or overlong encoding */
            utf16_ch[0] = 0xfffd;
            utf16_ch[1] = 0xfffd;
            utf16_ch[2] = 0xfffd;
            utf16_ch_len = utf8_trail_bytes;
            Status = STATUS_SOME_NOT_MAPPED;
        }
        else if (ch >= 0x10000)
        {
            /* Surrogate pair */
            ch -= 0x010000;
            utf16_ch[0] = 0xd800 + (ch >> 10 & 0x3ff);
            utf16_ch[1] = 0xdc00 + (ch >>  0 & 0x3ff);
            utf16_ch_len = 2;
        }
        else
        {
            /* Single unit */
            utf16_ch[0] = ch;
            utf16_ch_len = 1;
        }

        if (!UnicodeStringDestination)
        {
            written += utf16_ch_len;
            continue;
        }

        for (j = 0; j < utf16_ch_len; j++)
        {
            if (UnicodeStringMaxByteCount >= sizeof(WCHAR))
            {
                *UnicodeStringDestination++ = utf16_ch[j];
                UnicodeStringMaxByteCount -= sizeof(WCHAR);
                written++;
            }
            else
            {
                UnicodeStringMaxByteCount = 0;
                Status = STATUS_BUFFER_TOO_SMALL;
            }
        }
    }

    *UnicodeStringActualByteCount = written * sizeof(WCHAR);
    return Status;
}
