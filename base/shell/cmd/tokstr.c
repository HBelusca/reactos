/*
 *  TOKSTR.C - Command-line options tokenizer.
 */

#include "precomp.h"

/* Masks for delimiters */
#define TS_DELIMS1_MASK (TS_DELIMS1_AS_TOKENS | TS_DELIMS1_PFX_TOKENS | TS_DELIMS1_IN_TOKENS)
#define TS_DELIMS2_MASK (TS_DELIMS2_AS_TOKENS | TS_DELIMS2_PFX_TOKENS | TS_DELIMS2_IN_TOKENS)

#define TS_DELIMS12_SHIFT (3) /* == LOG2(TS_DELIMS2_MASK / TS_DELIMS1_MASK) */

#define tokStrChr(str, chr) \
    ((str) ? _tcschr((str), (chr)) : NULL)

/**
 * @brief   Returns TRUE if a character has to be considered as whitespace.
 **/
static BOOL
IsTokWSSeparator(
    IN TCHAR Char,
    IN DWORD Flags,
    IN LPCTSTR WSpaceSeps OPTIONAL,
    IN LPCTSTR OptsDelims1 OPTIONAL,
    IN LPCTSTR OptsDelims2 OPTIONAL)
{
#if 0
    /* If whitespace are not to be considered as separators, bail out */
    if (Flags & TS_WSPACE_NO_SEPS)
        return FALSE;

    if (!Char)
        return FALSE;
#endif

    /*
     * If this is a regular whitespace or a whitespace-like character
     * (e.g. tab, ..., or the specific ',' , ';' or '=') ...
     */
    if (_istspace(Char) || tokStrChr(WSpaceSeps, Char))
    {
        /*
         * ... and is NOT a specified option delimiter to be included
         * either as an explicit token or within a token, succeed.
         */
        if ( !((Flags & TS_DELIMS1_MASK) && tokStrChr(OptsDelims1, Char)) &&
             !((Flags & TS_DELIMS2_MASK) && tokStrChr(OptsDelims2, Char)) )
        {
            return TRUE;
        }
    }

    return FALSE;
}

/**
 * @brief   "Strip" the quotes in the token by adjusting its 'start'
 *          pointer and its length. The pointed string is not modified.
 **/
static VOID
TokStripQuotes(
    IN OUT LPCTSTR* start,
    IN OUT PSIZE_T len)
{
    /* If the token starts with a quote ... */
    if ((**start == _T('"')) && (*len > 0))
    {
        /* ... strip it, as well as the last one if it exists */
        if ((*start)[*len - 1] == _T('"'))
            --(*len);

        ++(*start);
        --(*len);
    }
}

/**
 * @brief   Iterates through a source string, finding the specified separators
 *          (with the exact behaviour depending on input flags) and returning
 *          a pointer to a delimited section of the string, and its length.
 **/
LPCTSTR
TokStrIter(
    IN OUT LPCTSTR* Str,
    OUT PSIZE_T Length,
    IN DWORD Flags,
    IN BOOL StripWS,
    IN LPCTSTR WSpaceSeps OPTIONAL,
    IN LPCTSTR OptsDelims1 OPTIONAL,
    IN LPCTSTR OptsDelims2 OPTIONAL)
{
    LPCTSTR src, start;
    DWORD DelimFlags;

    src = (Str ? *Str : NULL);

    if (!Str || !src)
    {
        *Length = 0;
        if (Str) *Str = NULL;
        return NULL;
    }

    while (*src)
    {
        /* Strip leading whitespace or whitespace-like separators */
        if (StripWS)
        {
            while (*src &&
                   IsTokWSSeparator(*src, Flags, WSpaceSeps,
                                    OptsDelims1, OptsDelims2))
            {
                ++src;
            }
            if (!*src)
                break;
        }

        /* Start the next token */
        start = src;
        while (*src)
        {
            /* Check for whitespace as separators */
            if (!(Flags & TS_WSPACE_NO_SEPS) &&
                IsTokWSSeparator(*src, Flags, WSpaceSeps,
                                 OptsDelims1, OptsDelims2))
            {
                /*
                 * If we are about to start a new token, just advance
                 * the position and ignore the delimiter. Otherwise just
                 * terminate the current token; the delimiter will be
                 * dealt with, the next time.
                 */
                if (src == start)
                    start = ++src;
                break;
            }

            /*
             * Do we have an option delimiter?
             * Check this first, since the quote could also be used as a delimiter.
             *
             * Filter only the delimiters-specific flags. Convert the flags for the
             * second delimiters into those of the first one, in case the character
             * belongs to the second delimiters, so as to re-use the same logic.
             * Set also the low bit when either of the tests pass.
             */
            DelimFlags =  (tokStrChr(OptsDelims1, *src) ?
                                (1 | (Flags & TS_DELIMS1_MASK))
                        : (tokStrChr(OptsDelims2, *src) ?
                                (1 | (Flags & TS_DELIMS2_MASK) >> TS_DELIMS12_SHIFT)
                        : 0));

            if (DelimFlags & 1)
            {
                if (DelimFlags & TS_DELIMS1_AS_TOKENS)
                {
                    /* The option delimiter is a separate token */

                    /*
                     * If we are about to start a new token, make length for
                     * at least one character to store the delimiter as a
                     * separate token. Otherwise just terminate the current
                     * token; the delimiter will be dealt with, the next time.
                     */
                    if (src == start)
                        ++src;
                    break;
                }
                if (DelimFlags & TS_DELIMS1_PFX_TOKENS)
                {
                    /* The option delimiter is a prefix for a new token */
                    if (src == start)
                        ++src; // Prefix the new token with the delimiter.
                    else
                        break; // Otherwise, stop the previous token.
                }
                else
                if (DelimFlags & TS_DELIMS1_IN_TOKENS)
                {
                    /*
                     * The option delimiter is not like whitespace,
                     * and is included within token.
                     */
                    ++src;
                }
                else
                {
                    /* The option delimiter is like a whitespace separator */

                    /*
                     * If we are about to start a new token, just advance
                     * the position and ignore the delimiter. Otherwise just
                     * terminate the current token; the delimiter will be
                     * dealt with, the next time.
                     */
                    if (src == start)
                        start = ++src;
                    break;
                }
            }
            else
            if (*src == _T('"'))
            {
                /* Retrieve the whole quoted text. If we
                 * stopped because of end quote, go past it. */
                do { ++src; } while (*src && *src != _T('"'));
                if (*src)
                    ++src;
            }
            else
            {
                /* Just copy the character */
                ++src;
            }
        }

        if (src > start)
        {
            /* A token was found, return it */
            *Length = (src - start);
            *Str = src;

            if (Flags & TS_STRIP_QUOTES)
                TokStripQuotes(&start, Length);

            return start;
        }
    }

    *Length = 0;
    *Str = NULL;
    return NULL;
}

/* EOF */
