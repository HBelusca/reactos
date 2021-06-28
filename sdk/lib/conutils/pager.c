/*
 * PROJECT:     ReactOS Console Utilities Library
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Console/terminal paging functionality.
 * COPYRIGHT:   Copyright 2017-2021 Hermes Belusca-Maito
 *              Copyright 2021 Katayama Hirofumi MZ
 */

/**
 * @file    pager.c
 * @ingroup ConUtils
 *
 * @brief   Console/terminal paging functionality.
 **/

/* FIXME: Temporary HACK before we cleanly support UNICODE functions */
#define UNICODE
#define _UNICODE

#include <windef.h>
#include <winbase.h>
// #include <winnls.h>
#include <wincon.h>  // Console APIs (only if kernel32 support included)
#include <winnls.h> // for WideCharToMultiByte
#include <strsafe.h>

#include "conutils.h"
#include "stream.h"
#include "screen.h"
#include "pager.h"

/** WIP: Debugging FORM-FEED expansion **/
// #define DEBUG_FORMFEED_EXPANSION

// Temporary HACK
#define CON_STREAM_WRITE    ConStreamWrite

#define CP_SHIFTJIS 932  // Japanese Shift-JIS
#define CP_HANGUL   949  // Korean Hangul/Wansung
#define CP_JOHAB    1361 // Korean Johab
#define CP_GB2312   936  // Chinese Simplified (GB2312)
#define CP_BIG5     950  // Chinese Traditional (Big5)

/* IsFarEastCP(CodePage) */
#define IsCJKCodePage(CodePage) \
    ((CodePage) == CP_SHIFTJIS || (CodePage) == CP_HANGUL || \
  /* (CodePage) == CP_JOHAB || */ \
     (CodePage) == CP_BIG5     || (CodePage) == CP_GB2312)

static inline INT
GetWidthOfCharCJK(
    IN UINT nCodePage,
    IN WCHAR ch)
{
    INT ret = WideCharToMultiByte(nCodePage, 0, &ch, 1, NULL, 0, NULL, NULL);
    if (ret == 0)
        ret = 1;
    else if (ret > 2)
        ret = 2;
    return ret;
}

/**
 * @brief   Retrieves a new text line, or continue fetching the current one.
 *
 * @remark  Manages setting Pager's CurrentLine, ichCurr, iEndLine, and the
 *          line cache (CachedLine, cchCachedLine). Other functions must not
 *          modify these values.
 **/
static BOOL
GetNextLine(
    IN OUT PCON_PAGER Pager,
    IN PCTCH TextBuff,
    IN SIZE_T cch)
{
    SIZE_T ich = Pager->ich;
    SIZE_T ichStart;
    SIZE_T cchLine;
    BOOL bCacheLine;

    Pager->ichCurr = 0;
    Pager->iEndLine = 0;

    /*
     * If we already had an existing line, then we can safely start a new one
     * and getting rid of any current cached line. Otherwise, we don't have
     * a current line and we may be caching a new one, in which case, continue
     * caching it until it becomes complete.
     */
    // INVESTIGATE: Do that only if (ichStart >= iEndLine) ??
    if (Pager->CurrentLine)
    {
        // ASSERT(Pager->CurrentLine == Pager->CachedLine);
        if (Pager->CachedLine)
        {
            HeapFree(GetProcessHeap(), 0, (PVOID)Pager->CachedLine);
            Pager->CachedLine = NULL;
            Pager->cchCachedLine = 0;
        }

        Pager->CurrentLine = NULL;
    }

    /* Nothing else to read if we are past the end of the buffer */
    if (ich >= cch)
    {
        /* If we have a pending cached line, terminate it now */
        if (Pager->CachedLine)
            goto TerminateLine;

        /* Otherwise, bail out */
        return FALSE;
    }

    /* Start a new line, or continue an existing one */
    ichStart = ich;

    /* Find where this line ends, looking for a NEWLINE character.
     * (NOTE: We cannot use strchr because the buffer is not NULL-terminated) */
    for (; ich < cch; ++ich)
    {
        if (TextBuff[ich] == TEXT('\n'))
        {
            ++ich;
            break;
        }
    }
    Pager->ich = ich;

    cchLine = (ich - ichStart);

    //
    // FIXME: Impose a maximum string limit when the line is cached, in order
    // not to potentially grow memory indefinitely. When the limit is reached,
    // terminate the line.
    //

    /*
     * If we have stopped because we have exhausted the text buffer
     * and we have not found an end-of-line character, this may mean
     * that the text line spans across different text buffers. If we
     * have been told so, cache this line: we will complete it during
     * the next call(s) and only then, display it.
     * Otherwise, consider the line to be terminated now.
     */
    bCacheLine = ((Pager->dwFlags & CON_PAGER_CACHE_INCOMPLETE_LINE) &&
                  (ich >= cch) && (TextBuff[ich - 1] != TEXT('\n')));

    /* Allocate, or re-allocate, the cached line buffer */
    if (bCacheLine && !Pager->CachedLine)
    {
        /* We start caching, allocate the cached line buffer */
        Pager->CachedLine = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                      cchLine * sizeof(TCHAR));
        Pager->cchCachedLine = 0;

        if (!Pager->CachedLine)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
    }
    else if (Pager->CachedLine)
    {
        /* We continue caching, re-allocate the cached line buffer */
        PVOID ptr = HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                (PVOID)Pager->CachedLine,
                                (Pager->cchCachedLine + cchLine) * sizeof(TCHAR));
        if (!ptr)
        {
            HeapFree(GetProcessHeap(), 0, (PVOID)Pager->CachedLine);
            Pager->CachedLine = NULL;
            Pager->cchCachedLine = 0;

            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        Pager->CachedLine = ptr;
    }
    if (Pager->CachedLine)
    {
        /* Copy/append the text to the cached line buffer */
        RtlCopyMemory((PVOID)&Pager->CachedLine[Pager->cchCachedLine],
                      &TextBuff[ichStart],
                      cchLine * sizeof(TCHAR));
        Pager->cchCachedLine += cchLine;
    }
    if (bCacheLine)
    {
        /* The line is currently incomplete, don't proceed further for now */
        return FALSE;
    }

TerminateLine:
    /* The line should be complete now. If we have an existing cached line,
     * it has been completed by appending the remaining text to it. */

    /* We are starting a new line */
    Pager->ichCurr = 0;
    if (Pager->CachedLine)
    {
        Pager->iEndLine = Pager->cchCachedLine;
        Pager->CurrentLine = Pager->CachedLine;
    }
    else
    {
        Pager->iEndLine = cchLine;
        Pager->CurrentLine = &TextBuff[ichStart];
    }

    /* Increase only when we have got a NEWLINE */
    if ((Pager->iEndLine > 0) && (Pager->CurrentLine[Pager->iEndLine - 1] == TEXT('\n')))
        Pager->lineno++;

    return TRUE;
}

/**
 * @brief   Does the main paging work: fetching text lines and displaying them.
 **/
static BOOL
ConPagerWorker(
    IN PCON_PAGER Pager,
    IN PCTCH TextBuff,
    IN DWORD cch)
{
    const DWORD PageColumns = Pager->PageColumns;
    const DWORD ScrollRows = Pager->ScrollRows;

    BOOL bFinitePaging = ((PageColumns > 0) && (Pager->PageRows > 0));
    LONG nTabWidth = Pager->nTabWidth;

    PCTCH Line;
    SIZE_T ich;
    SIZE_T ichStart;
    SIZE_T iEndLine;
    DWORD iColumn;

    UINT nCodePage = GetConsoleOutputCP();
    BOOL IsCJK = IsCJKCodePage(nCodePage);
    UINT nWidthOfChar = 1;
    BOOL IsDoubleWidthCharTrailing = FALSE;

    /* Normalize the tab width: if negative or too large,
     * cap it to the number of columns. */
    if (PageColumns > 0) // if (bFinitePaging)
    {
        /* Output to console-type device */

        if (Pager->dwFlags & CON_PAGER_EXPAND_TABS)
        {
            if (nTabWidth < 0)
                nTabWidth = PageColumns - 1;
            else
                nTabWidth = min(nTabWidth, PageColumns - 1);
        }
        else
        {
            /* The console expands the TAB to 8 spaces; emulate this */
            nTabWidth = 8;
        }
    }
    else
    {
        /* Output to file-type device */

        if (Pager->dwFlags & CON_PAGER_EXPAND_TABS)
        {
            /* If no column width is known, default to 8 spaces if the
             * original value is negative; otherwise keep the current one. */
            if (nTabWidth < 0)
                nTabWidth = 8;
        }
        else
        {
            /* Keep the TAB character */
        }
    }


    /* Continue displaying the previous line, if any, or start a new one */
    Line = Pager->CurrentLine;
    ichStart = Pager->ichCurr;
    iEndLine = Pager->iEndLine;

ProcessLine:

    /* Stop now if we have displayed more page lines than requested */
    if (bFinitePaging && (Pager->iLine >= ScrollRows))
        goto End;

    if (!Line || (ichStart >= iEndLine))
    {
        /* Start a new line */
        // Pager->nSpacePending = 0;
        if (!GetNextLine(Pager, TextBuff, cch))
            goto End;

        Line = Pager->CurrentLine;
        ichStart = Pager->ichCurr;
        iEndLine = Pager->iEndLine;
    }
    else
    {
        /* Continue displaying the current line */
    }

    // ASSERT(Line && ((ichStart < iEndLine) || (ichStart == iEndLine && iEndLine == 0)));

    /* Determine whether this line segment (from the current position till the end) should be displayed */
    if (Pager->PagerLine)
    {
        CON_PAGER_LINE_STATUS Status = Pager->PagerLine(Pager, &Line[ichStart], iEndLine - ichStart);
        if (Status == PagerLineRescan)
        {
            /* Recheck the conditions above */
            goto ProcessLine;
        }
        else if (Status == PagerLineIgnore)
        {
            /* Done with this line; start a new one */
            Pager->nSpacePending = 0; // And reset any pending space.
            ichStart = iEndLine;
            goto ProcessLine;
        }
    }
    // else Status == PagerLineDoLine: Continue displaying the line.

    iColumn = Pager->iColumnInd;

    /* Print out any pending TAB expansion */
    if (/* (Line[ichStart] == TEXT('\t')) && */ (Pager->nSpacePending > 0))
    {
ExpandTab:
        while (Pager->nSpacePending > 0)
        {
            /* Check whether we are going across the column */
            if ((PageColumns > 0) && (iColumn >= PageColumns))
                break;

            /* We are not, print filling spaces */
            CON_STREAM_WRITE(Pager->Screen->Stream, TEXT(" "), 1);
            ++iColumn;
            --(Pager->nSpacePending);
        }
        if ((PageColumns > 0) /* && (Pager->dwFlags & CON_PAGER_WRAP_LINES) */)
        {
            /* Check whether we are going across the column */
            if (iColumn >= PageColumns)
            {
                // Pager->nSpacePending = 0; // <-- This is the mode of most text editors...

                /* Reposition the cursor to the next line, first column */
                if (!bFinitePaging || (PageColumns < Pager->Screen->csbi.dwSize.X))
                    CON_STREAM_WRITE(Pager->Screen->Stream, TEXT("\n"), 1);

                Pager->iLine++;
                Pager->iTabOffset = (Pager->iTabOffset + iColumn) % Pager->nTabWidth;
                Pager->iColumnInd = 0;

                /* Restart at the character */
                // ASSERT(ichStart == ich);
                goto ProcessLine;
            }
        }
        else
        {
            /* Don't wrap and just absorb the pending spaces */
            iColumn += Pager->nSpacePending;
            Pager->nSpacePending = 0;
        }
    //    /* Advance after the tab */
    //    ++ichStart;
    }


    /* Find, within this line segment (starting from its
     * beginning), until where we can print to the page. */
    for (ich = ichStart; ich < iEndLine; ++ich)
    {
        /* Check whether we are going across the column */
        if ((PageColumns > 0) && (iColumn >= PageColumns))
            break;

        /* NEWLINE character */
        if (Line[ich] == TEXT('\n'))
        {
            // ASSERT(ich == iEndLine - 1);
#if 1
            if ((ich > 0) && Line[ich - 1] == TEXT('\r'))
                --ich; // Remove CR
#endif
            /* We should stop now */
            break;
        }

        /* TAB character */
        if (Line[ich] == TEXT('\t'))
        {
            if ((PageColumns > 0) /** bFinitePaging **/ ||
                (Pager->dwFlags & CON_PAGER_EXPAND_TABS))
            {
                /* We should stop now */
                break;
            }

            /* Output to file-type device without TAB expansion:
             * Treat it as a regular character. */
            ++iColumn;
            continue;
        }

        /* FORM-FEED character */
        if (Line[ich] == TEXT('\f'))
        {
            if (Pager->dwFlags & CON_PAGER_EXPAND_FF)
            {
                /* We should stop now */
                break;
            }

            /* Treat it as a regular character */
            ++iColumn;
            continue;
        }

        /* Other character - Handle double-width for CJK */

        if (IsCJK)
            nWidthOfChar = GetWidthOfCharCJK(nCodePage, Line[ich]);

        /* Care about CJK character presentation only when outputting
         * to a device where the number of columns is known. */
        if ((PageColumns > 0) && IsCJK)
        {
            IsDoubleWidthCharTrailing = (nWidthOfChar == 2) &&
                                        ((iColumn + 1) % PageColumns == 0);
            if (IsDoubleWidthCharTrailing)
            {
                /* Reserve this character for the next line */
                ++iColumn; // Count a blank instead.
                /* We should stop now */
                break;
            }
        }

        iColumn += nWidthOfChar;
    }

    Pager->iColumnInd = iColumn;

    /* Output the pending line segment */
    if (ich - ichStart > 0)
        CON_STREAM_WRITE(Pager->Screen->Stream, &Line[ichStart], ich - ichStart);

    /* Have we finished the line segment? */
    if (ich >= iEndLine)
    {
        /* Restart at the character */
        ichStart = ich;
        goto ProcessLine;
    }

    /* Are we wrapping the line? */
    if ((PageColumns > 0) /* && (Pager->dwFlags & CON_PAGER_WRAP_LINES) */)
    {
        /* Check whether we are going across the column */
        if (iColumn >= PageColumns)
        {
            /* Reposition the cursor to the next line, first column */
            if (!bFinitePaging || (PageColumns < Pager->Screen->csbi.dwSize.X))
                CON_STREAM_WRITE(Pager->Screen->Stream, TEXT("\n"), 1);

            Pager->iLine++;
            Pager->iTabOffset = (Pager->iTabOffset + iColumn) % Pager->nTabWidth;
            Pager->iColumnInd = 0;

            /* Restart at the character */
            ichStart = ich;
            goto ProcessLine;
        }
    }

    /* Handle special characters */

    /* NEWLINE character */
    if (Line[ich] == TEXT('\n'))
    {
        // ASSERT(ich == iEndLine - 1);

        /* Reposition the cursor to the next line, first column */
        CON_STREAM_WRITE(Pager->Screen->Stream, TEXT("\n"), 1);

        Pager->iLine++;
        Pager->iColumn = 0;

        /* Done with this line; start a new one */
        Pager->nSpacePending = 0; // And reset any pending space.
        ichStart = iEndLine;
        goto ProcessLine;
    }

    /* TAB character */
    if (Line[ich] == TEXT('\t'))
    {
#if 0
        if ((PageColumns > 0) /** bFinitePaging **/ ||
            (Pager->dwFlags & CON_PAGER_EXPAND_TABS))
        {
            // Do the stuff done below...
        }
#endif
        if (Pager->dwFlags & CON_PAGER_EXPAND_TABS)
        {
            /* Perform TAB expansion, unless the tab width is zero */
            if (nTabWidth == 0)
            {
                ichStart = ++ich;
                goto ProcessLine;
            }
        }

        /* Keep the TAB pending until it has been developed */
        // INVESTIGATE: Or keep it as it is... and only advance it
        // after having done all the expansion??
        // ichStart = ich;
        ichStart = ++ich;
        /* Reset the number of spaces needed to develop this TAB character */
        Pager->nSpacePending = nTabWidth - ((Pager->iTabOffset + iColumn) % nTabWidth);
        goto ExpandTab;
    }

    /* FORM-FEED character */
    if (Line[ich] == TEXT('\f') &&
        (Pager->dwFlags & CON_PAGER_EXPAND_FF))
    {
        if (ich != ichStart)
        {
            /* We terminate with a FORM-FEED: Go to the next line */

#ifdef DEBUG_FORMFEED_EXPANSION
            ConPuts(Pager->Screen->Stream, TEXT("-->"));
#endif

            /* Reposition the cursor to the next line, first column */
            CON_STREAM_WRITE(Pager->Screen->Stream, TEXT("\n"), 1);

            Pager->iLine++;
            Pager->iColumn = 0;

            /* Restart at the character */
            ichStart = ich;
        }
        else // if (ich == ichStart)
        {
            /* We begin with a FORM-FEED: Clear the screen */

#ifdef DEBUG_FORMFEED_EXPANSION
            ConPuts(Pager->Screen->Stream, TEXT("\f<--"));
#endif

            if (bFinitePaging)
            {
                /* Clear until the end of the page */

                /* If we don't shrink blank lines, continue
                 * flushing the page unconditionally. */

                /* Call the user paging function in order to know
                 * whether we need to output the blank lines. */
                // FIXME: We're not displaying an actual file line,
                // and therefore we shouldn't call the user paging function...
                if (Pager->PagerLine && Pager->PagerLine(Pager, TEXT("\n"), 1) == PagerLineIgnore)
                {
                    /* Only one blank line displayed, that counts in the line count */
                    Pager->iLine++;
                }
                else
                {
                    while (Pager->iLine < ScrollRows)
                    {
                        CON_STREAM_WRITE(Pager->Screen->Stream, TEXT("\n"), 1);
                        Pager->iLine++;
                    }
                }
            }
            else
            {
                /* Just output a FORM-FEED character and a NEWLINE */
                CON_STREAM_WRITE(Pager->Screen->Stream, TEXT("\f\n"), 2);
                Pager->iLine++;
            }

            /* Skip and restart past the character */
            ichStart = ++ich;
        }

        Pager->iColumn = 0;
        Pager->nSpacePending = 0; // And reset any pending space.
        goto ProcessLine;
    }

    /* Restart at the character */
    ichStart = ich;
    goto ProcessLine;


End:
    /*
     * We are exiting, either because we displayed all the required lines
     * (iLine >= ScrollRows), or, because we don't have more data to display.
     */

    Pager->ichCurr = ichStart;
    // INVESTIGATE: Can we get rid of CurrentLine here? // if (ichStart >= iEndLine) ...

    /* Return TRUE if we displayed all the required lines; FALSE otherwise */
    if (bFinitePaging && (Pager->iLine >= ScrollRows))
    {
        Pager->iLine = 0; /* Reset the count of lines being printed */
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}


/**
 * @name ConWritePaging
 *     Pages the contents of a user-specified character buffer on the screen.
 *
 * @param[in]   Pager
 *     Pager object that describes where the paged output is issued.
 *
 * @param[in]   PagePrompt
 *     A user-specific callback, called when a page has been displayed.
 *
 * @param[in]   StartPaging
 *     Set to TRUE for initializing the paging operation; FALSE during paging.
 *
 * @param[in]   szStr
 *     Pointer to the character buffer whose contents are to be paged.
 *
 * @param[in]   len
 *     Length of the character buffer pointed by @p szStr, specified
 *     in number of characters.
 *
 * @return
 *     TRUE when all the contents of the character buffer has been displayed;
 *     FALSE if the paging operation has been stopped (controlled via @p PagePrompt).
 **/
BOOL
ConWritePaging(
    IN PCON_PAGER Pager,
    IN PAGE_PROMPT PagePrompt,
    IN BOOL StartPaging,
    IN PCTCH szStr,
    IN DWORD len)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    BOOL bIsConsole;

    /* Parameters validation */
    if (!Pager)
        return FALSE;

    /* Get the size of the visual screen that can be printed to */
    bIsConsole = ConGetScreenInfo(Pager->Screen, &csbi);
    if (bIsConsole)
    {
        /* Calculate the console screen extent */
        Pager->PageColumns = csbi.dwSize.X;
        Pager->PageRows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
    else
    {
        /* We assume it's a file handle */
        Pager->PageColumns = 0;
        Pager->PageRows = 0;
    }

    if (StartPaging)
    {
        if (bIsConsole && (Pager->PageRows >= 2))
        {
            /* Reset to display one page by default */
            Pager->ScrollRows = Pager->PageRows - 1;
        }
        else
        {
            /* File output, or single line: all lines are displayed at once; reset to a default value */
            Pager->ScrollRows = 0;
        }

        /* Reset the internal data buffer */
        Pager->CachedLine = NULL;
        Pager->cchCachedLine = 0;

        /* Reset the paging state */
        Pager->CurrentLine = NULL;
        Pager->ichCurr = 0;
        Pager->iEndLine = 0;
        Pager->nSpacePending = 0;
        Pager->iColumn = 0;
        Pager->iLine = 0;
        Pager->lineno = 0;
    }

    /* Reset the reading index in the user-provided source buffer */
    Pager->ich = 0;

    /* Run the pager even when the user-provided source buffer is
     * empty, in case we need to flush any remaining cached line. */
    if (!Pager->CachedLine)
    {
        /* No cached line, bail out now */
        if (len == 0 || szStr == NULL)
            return TRUE;
    }

    while (ConPagerWorker(Pager, szStr, len))
    {
        /* Prompt the user only when we display to a console and the screen
         * is not too small: at least one line for the actual paged text and
         * one line for the prompt. */
        if (bIsConsole && (Pager->PageRows >= 2))
        {
            /* Reset to display one page by default */
            Pager->ScrollRows = Pager->PageRows - 1;

            /* Prompt the user; give him some values for statistics */
            // FIXME: Doesn't reflect what's currently being displayed.
            if (!PagePrompt(Pager, Pager->ich, len))
                return FALSE;
        }

        /* If we display to a console, recalculate its screen extent
         * in case the user has redimensioned it during the prompt. */
        if (bIsConsole && ConGetScreenInfo(Pager->Screen, &csbi))
        {
            Pager->PageColumns = csbi.dwSize.X;
            Pager->PageRows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        }
    }

    return TRUE;
}

BOOL
ConPutsPaging(
    IN PCON_PAGER Pager,
    IN PAGE_PROMPT PagePrompt,
    IN BOOL StartPaging,
    IN PCTSTR szStr)
{
    DWORD len;

    /* Return if no string has been given */
    if (szStr == NULL)
        return TRUE;

    len = wcslen(szStr);
    return ConWritePaging(Pager, PagePrompt, StartPaging, szStr, len);
}

BOOL
ConResPagingEx(
    IN PCON_PAGER Pager,
    IN PAGE_PROMPT PagePrompt,
    IN BOOL StartPaging,
    IN HINSTANCE hInstance OPTIONAL,
    IN UINT uID)
{
    INT Len;
    PCWSTR szStr = NULL;

    Len = K32LoadStringW(hInstance, uID, (PWSTR)&szStr, 0);
    if (szStr && Len)
        return ConWritePaging(Pager, PagePrompt, StartPaging, szStr, Len);
    else
        return TRUE;
}

BOOL
ConResPaging(
    IN PCON_PAGER Pager,
    IN PAGE_PROMPT PagePrompt,
    IN BOOL StartPaging,
    IN UINT uID)
{
    return ConResPagingEx(Pager, PagePrompt, StartPaging,
                          NULL /*GetModuleHandleW(NULL)*/, uID);
}

/* EOF */
