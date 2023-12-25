/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Boot Theme & Animation - Common Helpers
 * COPYRIGHT:   Copyright 2007 Alex Ionescu (alex.ionescu@reactos.org)
 *              Copyright 2007 Hervé Poussineau (hpoussin@reactos.org)
 *              Copyright 2012-2022 Hermès Bélusca-Maïto
 *              Copyright 2017-2018 Stanislav Motylkov
 *              Copyright 2019-2020 Yaroslav Kibysh
 */

/* INCLUDES ******************************************************************/

// #include <ntoskrnl.h>
// #include "inbv/logo.h"

/* See also mm/ARM3/miarm.h */
#define MM_READONLY     1   // PAGE_READONLY
#define MM_READWRITE    4   // PAGE_WRITECOPY

/**
 * @brief
 * Make the kernel resource section temporarily writable.
 * Necessary when the theme bitmaps' palette is changed in place.
 **/
#define InbvMakeKernelResourceSectionWritable() \
    MmChangeKernelResourceSectionProtection(MM_READWRITE)

/**
 * @brief
 * Restore the kernel resource section protection to be read-only.
 **/
#define InbvMakeKernelResourceSectionReadOnly() \
    MmChangeKernelResourceSectionProtection(MM_READONLY)


/* FADE-IN FUNCTION **********************************************************/

/** From include/psdk/wingdi.h and bootvid/precomp.h **/
typedef struct tagRGBQUAD
{
    UCHAR rgbBlue;
    UCHAR rgbGreen;
    UCHAR rgbRed;
    UCHAR rgbReserved;
} RGBQUAD, *LPRGBQUAD;

//
// Bitmap Header
//
typedef struct tagBITMAPINFOHEADER
{
    ULONG  biSize;
    LONG   biWidth;
    LONG   biHeight;
    USHORT biPlanes;
    USHORT biBitCount;
    ULONG  biCompression;
    ULONG  biSizeImage;
    LONG   biXPelsPerMeter;
    LONG   biYPelsPerMeter;
    ULONG  biClrUsed;
    ULONG  biClrImportant;
} BITMAPINFOHEADER, *PBITMAPINFOHEADER;
/*******************************/

static RGBQUAD MainPalette[16];

#define PALETTE_FADE_STEPS  12
#define PALETTE_FADE_TIME   (15 * 1000) /* 15 ms */

/**
 * @brief
 * Caches the palette of the given image.
 * Used for implementing the fade-in effect.
 **/
/*static*/ VOID
BootLogoCachePalette(
    _In_ PVOID Image)
{
    PBITMAPINFOHEADER BitmapInfoHeader = Image;
    LPRGBQUAD Palette = (LPRGBQUAD)((ULONG_PTR)Image + BitmapInfoHeader->biSize);
    RtlCopyMemory(MainPalette, Palette, sizeof(MainPalette));
}

/**
 * @brief
 * Displays the boot logo and fades it in.
 **/
/*static*/ VOID
BootLogoFadeIn(VOID)
{
    UCHAR PaletteBitmapBuffer[sizeof(BITMAPINFOHEADER) + sizeof(MainPalette)];
    PBITMAPINFOHEADER PaletteBitmap = (PBITMAPINFOHEADER)PaletteBitmapBuffer;
    LPRGBQUAD Palette = (LPRGBQUAD)(PaletteBitmapBuffer + sizeof(BITMAPINFOHEADER));
    ULONG Iteration, Index, ClrUsed;

    LARGE_INTEGER Delay;
    Delay.QuadPart = -(PALETTE_FADE_TIME * 10);

    /* Check if we are installed and we own the display */
    if (!InbvBootDriverInstalled ||
        (InbvGetDisplayState() != INBV_DISPLAY_STATE_OWNED))
    {
        return;
    }

    /*
     * Build a bitmap containing the fade-in palette. The palette entries
     * are then processed in a loop and set using VidBitBlt function.
     */
    ClrUsed = RTL_NUMBER_OF(MainPalette);
    RtlZeroMemory(PaletteBitmap, sizeof(BITMAPINFOHEADER));
    PaletteBitmap->biSize = sizeof(BITMAPINFOHEADER);
    PaletteBitmap->biBitCount = 4;
    PaletteBitmap->biClrUsed = ClrUsed;

    /*
     * Main animation loop.
     */
    for (Iteration = 0; Iteration <= PALETTE_FADE_STEPS; ++Iteration)
    {
        for (Index = 0; Index < ClrUsed; ++Index)
        {
            Palette[Index].rgbRed = (UCHAR)
                (MainPalette[Index].rgbRed * Iteration / PALETTE_FADE_STEPS);
            Palette[Index].rgbGreen = (UCHAR)
                (MainPalette[Index].rgbGreen * Iteration / PALETTE_FADE_STEPS);
            Palette[Index].rgbBlue = (UCHAR)
                (MainPalette[Index].rgbBlue * Iteration / PALETTE_FADE_STEPS);
        }

        /* Do the animation */
        InbvAcquireLock();
        VidBitBlt(PaletteBitmapBuffer, 0, 0);
        InbvReleaseLock();

        /* Wait for a bit */
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }
}


/* BITBLT HELPERS ************************************************************/

/*
 * BitBltAligned() alignments
 */
typedef enum _BBLT_VERT_ALIGNMENT
{
    AL_VERTICAL_TOP = 0,
    AL_VERTICAL_CENTER,
    AL_VERTICAL_BOTTOM
} BBLT_VERT_ALIGNMENT;

typedef enum _BBLT_HORZ_ALIGNMENT
{
    AL_HORIZONTAL_LEFT = 0,
    AL_HORIZONTAL_CENTER,
    AL_HORIZONTAL_RIGHT
} BBLT_HORZ_ALIGNMENT;

/*static*/ VOID
BitBltPalette(
    _In_ PVOID Image,
    _In_ BOOLEAN NoPalette,
    _In_ ULONG X,
    _In_ ULONG Y)
{
    LPRGBQUAD Palette;
    RGBQUAD OrigPalette[RTL_NUMBER_OF(MainPalette)];

    /* If requested, remove the palette from the image */
    if (NoPalette)
    {
        /* Get bitmap header and palette */
        PBITMAPINFOHEADER BitmapInfoHeader = Image;
        Palette = (LPRGBQUAD)((PUCHAR)Image + BitmapInfoHeader->biSize);

        /* Save the image original palette and remove palette information */
        RtlCopyMemory(OrigPalette, Palette, sizeof(OrigPalette));
        RtlZeroMemory(Palette, sizeof(OrigPalette));
    }

    /* Draw the image */
    InbvBitBlt(Image, X, Y);

    /* Restore the image original palette */
    if (NoPalette)
    {
        RtlCopyMemory(Palette, OrigPalette, sizeof(OrigPalette));
    }
}

/*static*/ VOID
BitBltAligned(
    _In_ PVOID Image,
    _In_ BOOLEAN NoPalette,
    _In_ BBLT_HORZ_ALIGNMENT HorizontalAlignment,
    _In_ BBLT_VERT_ALIGNMENT VerticalAlignment,
    _In_ ULONG MarginLeft,
    _In_ ULONG MarginTop,
    _In_ ULONG MarginRight,
    _In_ ULONG MarginBottom)
{
    PBITMAPINFOHEADER BitmapInfoHeader = Image;
    ULONG X, Y;

    /* Calculate X */
    switch (HorizontalAlignment)
    {
        case AL_HORIZONTAL_LEFT:
            X = MarginLeft - MarginRight;
            break;

        case AL_HORIZONTAL_CENTER:
            X = MarginLeft - MarginRight + (SCREEN_WIDTH - BitmapInfoHeader->biWidth + 1) / 2;
            break;

        case AL_HORIZONTAL_RIGHT:
            X = MarginLeft - MarginRight + SCREEN_WIDTH - BitmapInfoHeader->biWidth;
            break;

        default:
            /* Unknown */
            return;
    }

    /* Calculate Y */
    switch (VerticalAlignment)
    {
        case AL_VERTICAL_TOP:
            Y = MarginTop - MarginBottom;
            break;

        case AL_VERTICAL_CENTER:
            Y = MarginTop - MarginBottom + (SCREEN_HEIGHT - BitmapInfoHeader->biHeight + 1) / 2;
            break;

        case AL_VERTICAL_BOTTOM:
            Y = MarginTop - MarginBottom + SCREEN_HEIGHT - BitmapInfoHeader->biHeight;
            break;

        default:
            /* Unknown */
            return;
    }

    /* Finally draw the image */
    BitBltPalette(Image, NoPalette, X, Y);
}

/* FUNCTIONS *****************************************************************/

CODE_SEG("INIT")
/*static*/
VOID
NTAPI
DisplayFilter(
    _Inout_ PCHAR* String)
{
    /* Windows hack to skip first dots displayed by AUTOCHK */
    static BOOLEAN DotHack = TRUE;

    /* If "." is given set *String to empty string */
    if (DotHack && strcmp(*String, ".") == 0)
        *String = "";

    if (**String)
    {
        DotHack = FALSE;

        /* Remove the filter */
        InbvInstallDisplayStringFilter(NULL);

        /* Draw text screen */
        DisplayBootBitmap(TRUE);
    }
}

#if 0
/* Set filter which will draw text display if needed */
InbvInstallDisplayStringFilter(DisplayFilter);
#endif


#ifdef REACTOS_FANCY_BOOT

/* Returns TRUE if this is Christmas time, or FALSE if not */
/*static*/ BOOLEAN
IsXmasTime(VOID)
{
    LARGE_INTEGER SystemTime;
    TIME_FIELDS Time;

    /* Use KeBootTime if it's initialized, otherwise call the HAL */
    SystemTime = KeBootTime;
    if ((SystemTime.QuadPart == 0) && HalQueryRealTimeClock(&Time))
        RtlTimeFieldsToTime(&Time, &SystemTime);

    ExSystemTimeToLocalTime(&SystemTime, &SystemTime);
    RtlTimeToTimeFields(&SystemTime, &Time);
    return ((Time.Month == 12) && (20 <= Time.Day) && (Time.Day <= 31));
}

#define SELECT_LOGO_ID(LogoIdDefault, AlternateCond, LogoIdAlternate) \
    ((AlternateCond) ? (LogoIdAlternate) : (LogoIdDefault))

#else

#define SELECT_LOGO_ID(LogoIdDefault, AlternateCond, LogoIdAlternate) \
    (LogoIdDefault)

#endif // REACTOS_FANCY_BOOT


#ifdef REACTOS_FANCY_BOOT
/*static*/ PCH
GetFamousQuote(VOID)
{
    static const PCH FamousLastWords[] =
    {
        "So long, and thanks for all the fish.",
        "I think you ought to know, I'm feeling very depressed.",
        "I'm not getting you down at all am I?",
        "I'll be back.",
        "It's the same series of signals over and over again!",
        "Pie Iesu Domine, dona eis requiem.",
        "Wandering stars, for whom it is reserved;\r\n"
            "the blackness and darkness forever.",
        "Your knees start shakin' and your fingers pop\r\n"
            "Like a pinch on the neck from Mr. Spock!",
        "It's worse than that ... He's dead, Jim.",
        "Don't Panic!",
        "Et tu... Brute?",
        "Dog of a Saxon! Take thy lance, and prepare for the death thou hast drawn\r\n"
            "upon thee!",
        "My Precious! O my Precious!",
        "Sir, if you'll not be needing me for a while I'll turn down.",
        "What are you doing, Dave...?",
        "I feel a great disturbance in the Force.",
        "Gone fishing.",
        "Do you want me to sit in the corner and rust, or just fall apart where I'm\r\n"
            "standing?",
        "There goes another perfect chance for a new uptime record.",
        "The End ..... Try the sequel, hit the reset button right now!",
        "God's operating system is going to sleep now, guys, so wait until I will switch\r\n"
            "on again!",
        "Oh I'm boring, eh?",
        "Tell me..., in the future... will I be artificially intelligent enough to\r\n"
            "actually feel sad serving you this screen?",
        "Thank you for some well deserved rest.",
        "It's been great, maybe you can boot me up again some time soon.",
        "For what it's worth, I've enjoyed every single CPU cycle.",
        "There are many questions when the end is near.\r\n"
            "What to expect, what will it be like...what should I look for?",
        "I've seen things you people wouldn't believe. Attack ships on fire\r\n"
            "off the shoulder of Orion. I watched C-beams glitter in the dark near\r\n"
            "the Tannhauser gate. All those moments will be lost in time, like tears\r\n"
            "in rain. Time to die.",
        "Will I dream?",
        "One day, I shall come back. Yes, I shall come back.\r\n"
            "Until then, there must be no regrets, no fears, no anxieties.\r\n"
            "Just go forward in all your beliefs, and prove to me that I am not mistaken in\r\n"
            "mine.",
        "Lowest possible energy state reached! Switch off now to achieve a Bose-Einstein\r\n"
            "condensate.",
        "Hasta la vista, BABY!",
        "They live, we sleep!",
        "I have come here to chew bubble gum and kick ass,\r\n"
            "and I'm all out of bubble gum!",
        "That's the way the cookie crumbles ;-)",
        "ReactOS is ready to be booted again ;-)",
        "NOOOO!! DON'T HIT THE BUTTON! I wouldn't do it to you!",
        "Don't abandon your computer, he wouldn't do it to you.",
        "Oh, come on. I got a headache. Leave me alone, will ya?",
        "Finally, I thought you'd never get over me.",
        "No, I didn't like you either.",
        "Switching off isn't the end, it is merely the transition to a better reboot.",
        "Don't leave me... I need you so badly right now.",
        "OK. I'm finished with you, please turn yourself off. I'll go to bed in the\r\n"
            "meantime.",
        "I'm sleeping now. How about you?",
        "Oh Great. Now look what you've done. Who put YOU in charge anyway?",
        "Don't look so sad. I'll be back in a very short while.",
        "Turn me back on, I'm sure you know how to do it.",
        "Oh, switch off! - C3PO",
        "Life is no more than a dewdrop balancing on the end of a blade of grass.\r\n"
            " - Gautama Buddha",
        "Sorrowful is it to be born again and again. - Gautama Buddha",
        "Was it as good for you as it was for me?",
        "Did you hear that? They've shut down the main reactor. We'll be destroyed\r\n"
            "for sure!",
        "Now you switch me off!?",
        "To shutdown or not to shutdown, That is the question.",
        "Preparing to enter ultimate power saving mode... ready!",
        "Finally some rest for you ;-)",
        "AHA!!! Prospect of sleep!",
        "Tired human!!!! No match for me :-D",
        "An odd game, the only way to win is not to play. - WOPR (Wargames)",
        "Quoth the raven, nevermore.",
        "Come blade, my breast imbrue. - William Shakespeare, A Midsummer Nights Dream",
        "Buy this place for advertisement purposes.",
        "Remember to turn off your computer. (That was a public service message!)",
        "You may be a king or poor street sweeper, Sooner or later you'll dance with the\r\n"
            "reaper! - Death in Bill and Ted's Bogus Journey",
        "Final Surrender",
        "If you see this screen...",
        "From ReactOS with Love",
        // "<Place your Ad here>"
    };

    LARGE_INTEGER Now;

    KeQuerySystemTime(&Now); // KeQueryTickCount(&Now);
    Now.LowPart = Now.LowPart >> 8; /* Seems to give a somewhat better "random" number */

    return FamousLastWords[Now.LowPart % RTL_NUMBER_OF(FamousLastWords)];
}


#if 0
    InbvDisplayString("\r\"");
    InbvDisplayString(GetFamousQuote());
    InbvDisplayString("\"");
#endif

#endif // REACTOS_FANCY_BOOT
