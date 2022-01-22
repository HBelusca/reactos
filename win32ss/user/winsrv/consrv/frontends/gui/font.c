/*
 * PROJECT:     ReactOS Console Server DLL
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Fonts Management for GUI Consoles.
 * COPYRIGHT:   Copyright 2022 Hermes Belusca-Maito
 */

/*
 * Console font cache.
 * See https://github.com/microsoft/terminal/blob/main/src/propsheet/font.h
 * https://github.com/microsoft/terminal/blob/main/src/propsheet/globals.cpp
 * etc.
 */
typedef struct _FONT_INFO
{
    HFONT hFont;    // By default set to NULL; gets initialized once the font is first used.
    COORD Size;     // Obtained font size
    COORD SizeWant; // Desired size; is {0;0} for Raster font
    LONG Weight;
    PWSTR FaceName; // Pointer to a FaceNames list entry in the cache.
    BYTE Family;
    BYTE tmCharSet;
} FONT_INFO, *PFONT_INFO;

typedef struct _FACENODE
{
    struct _FACENODE* pNext;
    DWORD dwFlag;
    TCHAR atch[];
} FACENODE, *PFACENODE;

typedef struct _FONT_CACHE
{
    ULONG NumberOfFonts;    // Actual number of entries in FontInfo.
    ULONG FontInfoLength;   // Maximum number of entries allocated in FontInfo.
    PFONT_INFO FontInfo;
    PFACENODE FaceNames;    // Single-list of enumerated face names.
} FONT_CACHE, *PFONT_CACHE;

#define FONTTBL_SIZE_INCREMENT  10

/* Console global font cache */
FONT_CACHE FontCache = { 0, 0, NULL, NULL };



//
// AddFaceNode() and DestroyFaceNodes() functions from
// https://github.com/microsoft/terminal/blob/main/src/propsheet/misc.cpp#L87
//
PFACENODE
AddFaceNode(
    _In_reads_or_z_(LF_FACESIZE) PCWSTR FaceName)
{
    PFACENODE pNew, *ppTmp;
    size_t cch;

    /*
     * Is it already here?
     */
    for (ppTmp = &FontCache.FaceNames; *ppTmp; ppTmp = &((*ppTmp)->pNext))
    {
        // FIXME: Case-sensitive or case-insensitive?
        if (wcsicmp(((*ppTmp)->atch), ptsz) == 0)
        {
            /* Found it */
            return *ppTmp;
        }
    }

    cch = wcslen(FaceName);
    pNew = ConsoleAllocHeap(0, sizeof(FACENODE) + (cch + 1) * sizeof(WCHAR));
    if (!pNew)
        return NULL;

    pNew->pNext = NULL;
    pNew->dwFlag = 0;
    StringCchCopyW(pNew->atch, cch + 1, FaceName);
    *ppTmp = pNew;
    return pNew;
}

VOID
DestroyFaceNodes(VOID)
{
    PFACENODE pNext, pTmp;

    pTmp = FontCache.FaceNames;
    while (pTmp != NULL)
    {
        pNext = pTmp->pNext;
        ConsoleFreeHeap(pTmp);
        pTmp = pNext;
    }

    FontCache.FaceNames = NULL;
}

//
// Equivalent of AddFont()
// https://github.com/microsoft/terminal/blob/main/src/propsheet/misc.cpp#L207
//
ULONG
GetAddCachedFontInternal(
    // _In_ COORD Size;     // Obtained font size
    // _In_ COORD SizeWant; // Desired size; is {0;0} for Raster font
    _In_reads_or_z_(LF_FACESIZE) PCWSTR FaceName,
    _In_ LONG Weight,
    _In_ BYTE Family,
    _In_ BYTE tmCharSet)
{
    PFACENODE FaceNode;
    ULONG i;

//
// TODO: Font table sorted by font size.
//

    /* Find whether the font already exists;
     * if so, return its index in the table. */
    for (i = 0; i < FontCache.NumberOfFonts; ++i)
    {
        // if (FontCache.FontInfo[i].Size != xxx)
            // continue;

        if (FontCache.FontInfo[i].Weight != Weight)
            continue;

        if (FontCache.FontInfo[i].Family != Family)
            continue;

        if (FontCache.FontInfo[i].tmCharSet != tmCharSet)
            continue;

        if (wcsicmp(FontCache.FontInfo[i].FaceName, FaceName) != 0)
            continue;

        /* If we are here, we've found the font. Return its index. */
        return i;
    }

    /*
     * We haven't found the font. Create or find a cached face name,
     * enlarge the table if needed and create the font.
     */

    FaceNode = AddFaceNode(FaceName);
    if (!FaceNode)
        return -1;

    if (i >= FontCache.FontInfoLength)
    {
        PFONT_INFO Block;

        /* Allocate a new font table */
        Block = ConsoleAllocHeap(HEAP_ZERO_MEMORY,
                                 (FontCache.FontInfoLength +
                                    FONTTBL_SIZE_INCREMENT) * sizeof(FONT_INFO));
        if (Block == NULL)
        {
            // RtlLeaveCriticalSection(&ProcessData->HandleTableLock);
            return -1; // STATUS_UNSUCCESSFUL;
        }

        /* If we previously had a font table, free it and use the new one */
        if (FontCache.FontInfo)
        {
            /* Copy the font entries from the old table to the new one */
            RtlCopyMemory(Block,
                          FontCache.FontInfo,
                          FontCache.FontInfoLength * sizeof(FONT_INFO));
            ConsoleFreeHeap(FontCache.FontInfo);
        }
        FontCache.FontInfo = Block;
        FontCache.FontInfoLength += FONTTBL_SIZE_INCREMENT;
    }

    FontCache.FontInfo[i].hFont = NULL;

    // FontCache.FontInfo[i].Size;
    // FontCache.FontInfo[i].SizeWant;
    FontCache.FontInfo[i].Weight = Weight;
    FontCache.FontInfo[i].FaceName = FaceNode->atch;
    FontCache.FontInfo[i].Family = Family;
    FontCache.FontInfo[i].tmCharSet = tmCharSet;

    FontCache.NumberOfFonts++;

    // TODO: For TT (ony?) fonts: create bold, underline and mixed fonts.

    return i;
}


VOID
InitFontCache(VOID)
{
    InitTTFontCache();

    FontCache.NumberOfFonts = 0;
    FontCache.FontInfoLength = 0;
    FontCache.FontInfo = NULL;
    FontCache.FaceNames = NULL;
}

VOID
ClearFontCache(VOID)
{
    ULONG i;

    for (i = 0; i < FontCache.NumberOfFonts; ++i)
    {
        if (FontCache.FontInfo[i].hFont)
            DeleteObject(FontCache.FontInfo[i].hFont);
    }
    FontCache.NumberOfFonts = 0;

    ConsoleFreeHeap(FontCache.FontInfo);
    FontCache.FontInfo = NULL;
    FontCache.FontInfoLength = 0;

    DestroyFaceNodes();

    ClearTTFontCache();
}

/* EOF */
