/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Generic Text UI Helpers
 * COPYRIGHT:   Copyright 1998-2003 Brian Palmer <brianp@sginet.com>
 *              Copyright 2022-2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

/* INCLUDES ******************************************************************/

#include <freeldr.h>

#if 0
typedef struct _SMALL_RECT
{
    SHORT Left;
    SHORT Top;
    SHORT Right;
    SHORT Bottom;
} SMALL_RECT, *PSMALL_RECT;
#endif

#define TAG_TUI_SCREENBUFFER 'SiuT'
PVOID TextVideoBuffer = NULL;

extern UCHAR MachDefaultTextColor;

/* FUNCTIONS *****************************************************************/

/*
 * TuiPrintf()
 * Prints formatted text to the screen.
 */
INT
TuiPrintf(
    _In_ PCSTR Format, ...)
{
    INT i;
    INT Length;
    va_list ap;
    CHAR Buffer[512];

    va_start(ap, Format);
    Length = _vsnprintf(Buffer, sizeof(Buffer), Format, ap);
    va_end(ap);

    if (Length == -1)
        Length = (INT)sizeof(Buffer);

    for (i = 0; i < Length; i++)
    {
        MachConsPutChar(Buffer[i]);
    }

    return Length;
}

VOID
TuiTruncateStringEllipsis(
    _Inout_z_ PSTR StringText,
    _In_ ULONG MaxChars)
{
    /* If it's too large, just add some ellipsis past the maximum */
    if (strlen(StringText) > MaxChars)
        strcpy(&StringText[MaxChars - 3], "...");
}

/*
 * DrawText()
 * Displays a string on a single screen line.
 * This function assumes coordinates are zero-based.
 */
VOID
TuiDrawText(
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_ PCSTR Text,
    _In_ UCHAR Attr)
{
    TuiDrawText2(X, Y, 0 /*(ULONG)strlen(Text)*/, Text, Attr);
}

/*
 * DrawText2()
 * Displays a string on a single screen line.
 * This function assumes coordinates are zero-based.
 * MaxNumChars is the maximum number of characters to display.
 * If MaxNumChars == 0, then display the whole string.
 */
VOID
TuiDrawText2(
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_opt_ ULONG MaxNumChars,
    _In_reads_or_z_(MaxNumChars) PCSTR Text,
    _In_ UCHAR Attr)
{
    PUCHAR ScreenMemory = (PUCHAR)TextVideoBuffer;
    ULONG i, j;

    /* Don't display anything if we are out of the screen */
    if ((X >= UiScreenWidth) || (Y >= UiScreenHeight))
        return;

    /* Draw the text, not exceeding the width */
    for (i = X, j = 0; Text[j] && i < UiScreenWidth && (MaxNumChars > 0 ? j < MaxNumChars : TRUE); i++, j++)
    {
        ScreenMemory[((Y*2)*UiScreenWidth)+(i*2)]   = (UCHAR)Text[j];
        ScreenMemory[((Y*2)*UiScreenWidth)+(i*2)+1] = Attr;
    }
}

VOID
TuiDrawCenteredText(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Right,
    _In_ ULONG Bottom,
    _In_ PCSTR TextString,
    _In_ UCHAR Attr)
{
    SIZE_T TextLength;
    SIZE_T Index, LastIndex;
    ULONG  LineBreakCount;
    ULONG  BoxWidth, BoxHeight;
    ULONG  RealLeft, RealTop;
    ULONG  X, Y;
    CHAR   Temp[2];

    /* Query text length */
    TextLength = strlen(TextString);

    /* Count the new lines and the box width */
    LineBreakCount = 0;
    BoxWidth = 0;
    LastIndex = 0;
    for (Index = 0; Index < TextLength; Index++)
    {
        /* Scan for new lines */
        if (TextString[Index] == '\n')
        {
            /* Remember the new line */
            LastIndex = Index;
            LineBreakCount++;
        }
        else
        {
            /* Check for new larger box width */
            if ((Index - LastIndex) > BoxWidth)
            {
                /* Update it */
                BoxWidth = (ULONG)(Index - LastIndex);
            }
        }
    }

    /* Base the box height on the number of lines */
    BoxHeight = LineBreakCount + 1;

    /*
     * Create the centered coordinates.
     * Here, the Left/Top/Right/Bottom rectangle is a hint, around
     * which we center the "real" text rectangle RealLeft/RealTop.
     */
    RealLeft = (Left + Right - BoxWidth + 1) / 2;
    RealTop  = (Top + Bottom - BoxHeight + 1) / 2;

    /* Now go for a second scan */
    LastIndex = 0;
    for (Index = 0; Index < TextLength; Index++)
    {
        /* Look for new lines again */
        if (TextString[Index] == '\n')
        {
            /* Update where the text should start */
            RealTop++;
            LastIndex = 0;
        }
        else
        {
            /* We've got a line of text to print, do it */
            X = (ULONG)(RealLeft + LastIndex);
            Y = RealTop;
            LastIndex++;
            Temp[0] = TextString[Index];
            Temp[1] = ANSI_NULL;
            TuiDrawText(X, Y, Temp, Attr);
        }
    }
}

/*
 * FillArea()
 * This function assumes coordinates are zero-based
 */
VOID TuiFillArea(ULONG Left, ULONG Top, ULONG Right, ULONG Bottom, CHAR FillChar, UCHAR Attr /* Color Attributes */)
{
    PUCHAR ScreenMemory = (PUCHAR)TextVideoBuffer;
    ULONG  i, j;

    /* Clip the area to the screen */
    if ((Left >= UiScreenWidth) || (Top >= UiScreenHeight))
    {
        return;
    }
    if (Right >= UiScreenWidth)
        Right = UiScreenWidth - 1;
    if (Bottom >= UiScreenHeight)
        Bottom = UiScreenHeight - 1;

    /* Loop through each line and column and fill it in */
    for (i = Top; i <= Bottom; ++i)
    {
        for (j = Left; j <= Right; ++j)
        {
            ScreenMemory[((i*2)*UiScreenWidth)+(j*2)] = (UCHAR)FillChar;
            ScreenMemory[((i*2)*UiScreenWidth)+(j*2)+1] = Attr;
        }
    }
}

_Ret_maybenull_
__drv_allocatesMem(Mem)
PUCHAR
TuiSaveScreen(VOID)
{
    PUCHAR Buffer;
    PUCHAR ScreenMemory = (PUCHAR)TextVideoBuffer;
    ULONG i;

    /* Allocate the buffer */
    Buffer = FrLdrTempAlloc(UiScreenWidth * UiScreenHeight * 2,
                            TAG_TUI_SCREENBUFFER);
    if (!Buffer)
        return NULL;

    /* Loop through each cell and copy it */
    for (i=0; i < (UiScreenWidth * UiScreenHeight * 2); i++)
    {
        Buffer[i] = ScreenMemory[i];
    }

    return Buffer;
}

VOID
TuiRestoreScreen(
    _In_opt_ __drv_freesMem(Mem) PUCHAR Buffer)
{
    PUCHAR ScreenMemory = (PUCHAR)TextVideoBuffer;
    ULONG i;

    if (!Buffer)
        return;

    /* Loop through each cell and copy it */
    for (i=0; i < (UiScreenWidth * UiScreenHeight * 2); i++)
    {
        ScreenMemory[i] = Buffer[i];
    }

    /* Free the buffer */
    FrLdrTempFree(Buffer, TAG_TUI_SCREENBUFFER);

    VideoCopyOffScreenBufferToVRAM();
}

BOOLEAN
TuiInitialize(VOID)
{
    /* Do nothing if already initialized */
    if (TextVideoBuffer)
        return TRUE;

    // MachVideoHideShowTextCursor(FALSE);
    // MachVideoSetTextCursorPosition(0, 0);
    // MachVideoClearScreen(ATTR(COLOR_GRAY, COLOR_BLACK));

    TextVideoBuffer = VideoAllocateOffScreenBuffer();
    return !!TextVideoBuffer;
}

VOID
TuiUnInitialize(VOID)
{
    /* Do nothing if already uninitialized */
    if (!TextVideoBuffer)
        return;

    VideoFreeOffScreenBuffer();
    TextVideoBuffer = NULL;

    // MachVideoSetDisplayMode(NULL, FALSE);

    // MachVideoClearScreen(ATTR(COLOR_GRAY, COLOR_BLACK));
    // MachVideoSetTextCursorPosition(0, 0);
    // MachVideoHideShowTextCursor(TRUE);
}

/*
 * DrawShadow()
 * This function assumes coordinates are zero-based
 */
VOID TuiDrawShadow(ULONG Left, ULONG Top, ULONG Right, ULONG Bottom)
{
    PUCHAR ScreenMemory = (PUCHAR)TextVideoBuffer;
    ULONG  Idx;

    /* Shade the bottom of the area */
    if (Bottom < (UiScreenHeight - 1))
    {
        if (UiScreenHeight < 34)
            Idx = Left + 2;
        else
            Idx = Left + 1;

        for (; Idx <= Right; ++Idx)
        {
            ScreenMemory[(((Bottom+1)*2)*UiScreenWidth)+(Idx*2)+1] = ATTR(COLOR_GRAY, COLOR_BLACK);
        }
    }

    /* Shade the right of the area */
    if (Right < (UiScreenWidth - 1))
    {
        for (Idx=Top+1; Idx<=Bottom; Idx++)
        {
            ScreenMemory[((Idx*2)*UiScreenWidth)+((Right+1)*2)+1] = ATTR(COLOR_GRAY, COLOR_BLACK);
        }
    }
    if (UiScreenHeight < 34)
    {
        if ((Right + 1) < (UiScreenWidth - 1))
        {
            for (Idx=Top+1; Idx<=Bottom; Idx++)
            {
                ScreenMemory[((Idx*2)*UiScreenWidth)+((Right+2)*2)+1] = ATTR(COLOR_GRAY, COLOR_BLACK);
            }
        }
    }

    /* Shade the bottom right corner */
    if ((Right < (UiScreenWidth - 1)) && (Bottom < (UiScreenHeight - 1)))
    {
        ScreenMemory[(((Bottom+1)*2)*UiScreenWidth)+((Right+1)*2)+1] = ATTR(COLOR_GRAY, COLOR_BLACK);
    }
    if (UiScreenHeight < 34)
    {
        if (((Right + 1) < (UiScreenWidth - 1)) && (Bottom < (UiScreenHeight - 1)))
        {
            ScreenMemory[(((Bottom+1)*2)*UiScreenWidth)+((Right+2)*2)+1] = ATTR(COLOR_GRAY, COLOR_BLACK);
        }
    }
}

/*
 * DrawBox()
 * This function assumes coordinates are zero-based
 */
VOID
TuiDrawBoxTopLine(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Right,
    _In_ UCHAR VertStyle,
    _In_ UCHAR HorzStyle,
    _In_ UCHAR Attr)
{
    UCHAR ULCorner, URCorner;

    /* Calculate the corner values */
    if (HorzStyle == HORZ)
    {
        if (VertStyle == VERT)
        {
            ULCorner = UL;
            URCorner = UR;
        }
        else // VertStyle == D_VERT
        {
            ULCorner = VD_UL;
            URCorner = VD_UR;
        }
    }
    else // HorzStyle == D_HORZ
    {
        if (VertStyle == VERT)
        {
            ULCorner = HD_UL;
            URCorner = HD_UR;
        }
        else // VertStyle == D_VERT
        {
            ULCorner = D_UL;
            URCorner = D_UR;
        }
    }

    TuiFillArea(Left, Top, Left, Top, ULCorner, Attr);
    TuiFillArea(Left+1, Top, Right-1, Top, HorzStyle, Attr);
    TuiFillArea(Right, Top, Right, Top, URCorner, Attr);
}

VOID
TuiDrawBoxBottomLine(
    _In_ ULONG Left,
    _In_ ULONG Bottom,
    _In_ ULONG Right,
    _In_ UCHAR VertStyle,
    _In_ UCHAR HorzStyle,
    _In_ UCHAR Attr)
{
    UCHAR LLCorner, LRCorner;

    /* Calculate the corner values */
    if (HorzStyle == HORZ)
    {
        if (VertStyle == VERT)
        {
            LLCorner = LL;
            LRCorner = LR;
        }
        else // VertStyle == D_VERT
        {
            LLCorner = VD_LL;
            LRCorner = VD_LR;
        }
    }
    else // HorzStyle == D_HORZ
    {
        if (VertStyle == VERT)
        {
            LLCorner = HD_LL;
            LRCorner = HD_LR;
        }
        else // VertStyle == D_VERT
        {
            LLCorner = D_LL;
            LRCorner = D_LR;
        }
    }

    TuiFillArea(Left, Bottom, Left, Bottom, LLCorner, Attr);
    TuiFillArea(Left+1, Bottom, Right-1, Bottom, HorzStyle, Attr);
    TuiFillArea(Right, Bottom, Right, Bottom, LRCorner, Attr);
}

VOID
TuiDrawBox(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Right,
    _In_ ULONG Bottom,
    _In_ UCHAR VertStyle,
    _In_ UCHAR HorzStyle,
    _In_ BOOLEAN Fill,
    _In_ BOOLEAN Shadow,
    _In_ UCHAR Attr)
{
    /* Fill in the box background */
    if (Fill)
        TuiFillArea(Left, Top, Right, Bottom, ' ', Attr);

    /* Fill in the top horizontal line */
    TuiDrawBoxTopLine(Left, Top, Right, VertStyle, HorzStyle, Attr);

    /* Fill in the vertical left and right lines */
    TuiFillArea(Left, Top+1, Left, Bottom-1, VertStyle, Attr);
    TuiFillArea(Right, Top+1, Right, Bottom-1, VertStyle, Attr);

    /* Fill in the bottom horizontal line */
    TuiDrawBoxBottomLine(Left, Bottom, Right, VertStyle, HorzStyle, Attr);

    /* Draw the shadow */
    if (Shadow)
        TuiDrawShadow(Left, Top, Right, Bottom);
}

UCHAR
TuiTextToColor(
    _In_ PCSTR ColorText)
{
    static const struct
    {
        PCSTR ColorName;
        UCHAR ColorValue;
    } Colors[] =
    {
        {"Black"  , COLOR_BLACK  },
        {"Blue"   , COLOR_BLUE   },
        {"Green"  , COLOR_GREEN  },
        {"Cyan"   , COLOR_CYAN   },
        {"Red"    , COLOR_RED    },
        {"Magenta", COLOR_MAGENTA},
        {"Brown"  , COLOR_BROWN  },
        {"Gray"   , COLOR_GRAY   },
        {"DarkGray"    , COLOR_DARKGRAY    },
        {"LightBlue"   , COLOR_LIGHTBLUE   },
        {"LightGreen"  , COLOR_LIGHTGREEN  },
        {"LightCyan"   , COLOR_LIGHTCYAN   },
        {"LightRed"    , COLOR_LIGHTRED    },
        {"LightMagenta", COLOR_LIGHTMAGENTA},
        {"Yellow"      , COLOR_YELLOW      },
        {"White"       , COLOR_WHITE       },
    };
    ULONG i;

    if (_stricmp(ColorText, "Default") == 0)
        return MachDefaultTextColor;

    for (i = 0; i < RTL_NUMBER_OF(Colors); ++i)
    {
        if (_stricmp(ColorText, Colors[i].ColorName) == 0)
            return Colors[i].ColorValue;
    }

    return COLOR_BLACK;
}

UCHAR
TuiTextToFillStyle(
    _In_ PCSTR FillStyleText)
{
    static const struct
    {
        PCSTR FillStyleName;
        UCHAR FillStyleValue;
    } FillStyles[] =
    {
        {"None"  , ' '},
        {"Light" , LIGHT_FILL },
        {"Medium", MEDIUM_FILL},
        {"Dark"  , DARK_FILL  },
    };
    ULONG i;

    for (i = 0; i < RTL_NUMBER_OF(FillStyles); ++i)
    {
        if (_stricmp(FillStyleText, FillStyles[i].FillStyleName) == 0)
            return FillStyles[i].FillStyleValue;
    }

    return LIGHT_FILL;
}


/* UI Helpers that may be promoted to ui.h one day ***************************/

/*
 * Private UI descriptor
 */
typedef struct _UI_PRIVATE_CONTEXT
{
    PVOID UserContext; //< User-specific context given at initialization time.
    UI_EVENT_PROC EventProc; //< UI event procedure.
    ULONG_PTR RetVal;  //< Return value to set when quitting the UI.
    struct {
        BOOLEAN Quit    : 1; //< TRUE to quit the UI; FALSE by default.
        BOOLEAN DoPaint : 1; //< TRUE when a paint operation needs to be done.
    };
} UI_PRIVATE_CONTEXT, *PUI_PRIVATE_CONTEXT;

VOID // BOOLEAN
UiEndUi(
    _In_ PVOID UiContext,
    _In_ ULONG_PTR Result)
{
    PUI_PRIVATE_CONTEXT context = (PUI_PRIVATE_CONTEXT)UiContext;
    context->Quit = TRUE;
    context->RetVal = Result;
}

ULONG_PTR
UiDispatch(
    _In_ UI_EVENT_PROC EventProc,
    _In_opt_ PVOID InitParam)
{
    UI_PRIVATE_CONTEXT uiContext = {InitParam, EventProc, -1, {FALSE, TRUE}};
    ULONG LastClockSecond;
    ULONG CurrentClockSecond;
    ULONG KeyEvent; // High byte: extended (TRUE/FALSE); Low byte: key.

    /* Create/initialize the UI */
    EventProc(&uiContext, /**/InitParam,/**/ UI_Initialize, (ULONG_PTR)InitParam);
    if (uiContext.Quit)
        goto Quit;

    uiContext.RetVal = 0;
    uiContext.DoPaint = TRUE; // Force initial repaint

    /* Start the timer */
    LastClockSecond = 0; // ArcGetRelativeTime();
    /***/TuiUpdateDateTime();/***/

    /* Process events */
    while (!uiContext.Quit) // or do { ... } while (!uiContext.Quit); ??
    {
        /* Now do the actual repaint and reset the flag */
        if (uiContext.DoPaint)
        {
            EventProc(&uiContext, /**/InitParam,/**/ UI_Paint, 0);
            VideoCopyOffScreenBufferToVRAM();
            uiContext.DoPaint = FALSE;
        }

        /* Check for key presses */
        if (MachConsKbHit())
        {
            /* Get the key (get the extended key if needed) */
            KeyEvent = MachConsGetCh();
            if (KeyEvent == KEY_EXTENDED)
            {
                KeyEvent = MachConsGetCh();
                KeyEvent |= 0x0100; // Set extended flag.
            }

            EventProc(&uiContext, /**/InitParam,/**/ UI_KeyPress, (ULONG_PTR)KeyEvent);
            if (uiContext.Quit)
                break;
        }

        MachHwIdle();

        /* Get the updated time, and check if more than a second has elapsed */
        CurrentClockSecond = ArcGetRelativeTime();
        if (CurrentClockSecond != LastClockSecond)
        {
            /* Update the time information */
            LastClockSecond = CurrentClockSecond;

            // FIXME: Theme-specific
            /* Update the date & time */
            TuiUpdateDateTime();
            uiContext.DoPaint = TRUE; // UiRedraw(&uiContext);

            EventProc(&uiContext, /**/InitParam,/**/ UI_Timer, 0);
        }

        // MachHwIdle();
    }

    /* Terminate/destroy the UI */
    EventProc(&uiContext, /**/InitParam,/**/ UI_Terminate, 0);
Quit:
    return uiContext.RetVal;
}

VOID // BOOLEAN
UiRedraw(
    _In_ PVOID UiContext)
{
    PUI_PRIVATE_CONTEXT context = (PUI_PRIVATE_CONTEXT)UiContext;
    context->DoPaint = TRUE;
    // Do more things?
}

ULONG_PTR
UiSendMsg(
    _In_ PVOID UiContext,
    /**/_In_opt_ PVOID UserContext,/**/
    _In_ UI_EVENT Event,
    _In_ ULONG_PTR EventParam)
{
    PUI_PRIVATE_CONTEXT context = (PUI_PRIVATE_CONTEXT)UiContext;
    return context->EventProc(UiContext, /**/UserContext,/**/ Event, EventParam);
}

/* EOF */
