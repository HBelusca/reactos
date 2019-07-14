/*
 * Simple types
 */
typedef struct _INBV_RECT
{
    ULONG Left;
    ULONG Top;
    ULONG Right;
    ULONG Bottom;
} INBV_RECT, *PINBV_RECT;

/*
 * Graphical Elements
 */
typedef struct _INBV_COLOR_FILL
{
    INBV_RECT Rect;
    ULONG Color;
} INBV_COLOR_FILL, *PINBV_COLOR_FILL;

typedef struct _INBV_BITMAP_OVERLAY
{
    INBV_RECT Rect;
    ULONG ResourceID;
} INBV_BITMAP_OVERLAY, *PINBV_BITMAP_OVERLAY;

typedef struct _INBV_PROGRESS_BAR
{
    INBV_RECT Rect;
    // Borders
    ULONG Color;
    BT_PROGRESS_INDICATOR InitialIndicator;
} INBV_PROGRESS_BAR, *PINBV_PROGRESS_BAR;

typedef struct _INBV_ROT_BAR
{
    INBV_RECT Rect;
    ROT_BAR_TYPE Type;
} INBV_ROT_BAR, *PINBV_ROT_BAR;

/*
 * Boot theme descriptor
 */
typedef struct _INBV_BOOT_THEME
{
    RGBQUAD BootPalette[16];

    ULONG BootScreenBkgdID;
    ULONG OverlayCount;
    PINBV_BITMAP_OVERLAY Overlays;
    ULONG ProgressBarCount;
    PINBV_PROGRESS_BAR ProgressBars;
    ULONG RotBarCount;
    PINBV_ROT_BAR RotBars;

    ULONG TextModeHeaderID;
    ULONG TextModeFooterID;
    ULONG TextColor;
    INBV_RECT TextScrollRegion;
} INBV_BOOT_THEME, *PINBV_BOOT_THEME;




static VOID
RemovePalette(IN PVOID Image)
{
    UCHAR PaletteBitmapBuffer[sizeof(BITMAPINFOHEADER) + sizeof(MainPalette)];
    PBITMAPINFOHEADER PaletteBitmap = (PBITMAPINFOHEADER)PaletteBitmapBuffer;
    LPRGBQUAD Palette = (LPRGBQUAD)(PaletteBitmapBuffer + sizeof(BITMAPINFOHEADER));

    // /* Check if we are installed and we own the display */
    // if (!InbvBootDriverInstalled ||
        // (InbvDisplayState != INBV_DISPLAY_STATE_OWNED))
    // {
        // return;
    // }

    /*
     * Build a bitmap containing the fade-in palette. The palette entries
     * are then processed in a loop and set using VidBitBlt function.
     */
    RtlZeroMemory(PaletteBitmap, sizeof(BITMAPINFOHEADER));
    PaletteBitmap->biSize = sizeof(BITMAPINFOHEADER);
    PaletteBitmap->biBitCount = 4;
    PaletteBitmap->biClrUsed = RTL_NUMBER_OF(MainPalette);

    /* Remove palette information */
    RtlZeroMemory(Palette, sizeof(MainPalette));

    /* Do the animation */
    InbvAcquireLock();
    VidBitBlt(PaletteBitmapBuffer, 0, 0);
    InbvReleaseLock();
}
