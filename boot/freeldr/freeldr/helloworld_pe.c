
#define NTOSAPI
#include <ntddk.h>
#include <ntifs.h>
#include <ioaccess.h>
#include <ketypes.h>
#include <mmtypes.h>
#include <ndk/asm.h>
#include <ndk/rtlfuncs.h>
#include <ndk/ldrtypes.h>
#include <ndk/halfuncs.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <winerror.h>
#include <ntstrsafe.h>

#include <arch/pc/pcbios.h>
// #include <arch/pc/machpc.h>
////////////////////////////////// #include <arch/pc/x86common.h>
// #include <arch/pc/pxe.h>

#if defined(_M_IX86)
#include <arch/i386/i386.h>
#include <internal/i386/intrin_i.h>
#endif


///////////////////////

extern char __ImageBase;

#if 0
#include <pshpack1.h>
typedef struct
{
    unsigned long    eax;
    unsigned long    ebx;
    unsigned long    ecx;
    unsigned long    edx;

    unsigned long    esi;
    unsigned long    edi;
    unsigned long    ebp;

    unsigned short    ds;
    unsigned short    es;
    unsigned short    fs;
    unsigned short    gs;

    unsigned long    eflags;

} DWORDREGS;

typedef struct
{
    unsigned short    ax, _upper_ax;
    unsigned short    bx, _upper_bx;
    unsigned short    cx, _upper_cx;
    unsigned short    dx, _upper_dx;

    unsigned short    si, _upper_si;
    unsigned short    di, _upper_di;
    unsigned short    bp, _upper_bp;

    unsigned short    ds;
    unsigned short    es;
    unsigned short    fs;
    unsigned short    gs;

    unsigned short    flags, _upper_flags;

} WORDREGS;

typedef struct
{
    unsigned char    al;
    unsigned char    ah;
    unsigned short    _upper_ax;
    unsigned char    bl;
    unsigned char    bh;
    unsigned short    _upper_bx;
    unsigned char    cl;
    unsigned char    ch;
    unsigned short    _upper_cx;
    unsigned char    dl;
    unsigned char    dh;
    unsigned short    _upper_dx;

    unsigned short    si, _upper_si;
    unsigned short    di, _upper_di;
    unsigned short    bp, _upper_bp;

    unsigned short    ds;
    unsigned short    es;
    unsigned short    fs;
    unsigned short    gs;

    unsigned short    flags, _upper_flags;

} BYTEREGS;


typedef union
{
    DWORDREGS    x;
    DWORDREGS    d;
    WORDREGS    w;
    BYTEREGS    b;
} REGS;
#include <poppack.h>

// This macro tests the Carry Flag
// If CF is set then the call failed (usually)
#define INT386_SUCCESS(regs)    ((regs.x.eflags & EFLAGS_CF) == 0)

#endif

#include <arch/pc/startup.h>

SU_INT386 Int386;

///////////////////////


#define TEXTMODE_BUFFER      0xb8000
#define TEXTMODE_BUFFER_SIZE 0x8000

#define TEXT_COLS  80
#define TEXT_LINES 25

VOID
PcConsPutChar(int Ch)
{
  REGS Regs;

  /* If we are displaying a CR '\n' then do a LF also */
  if ('\n' == Ch)
    {
      /* Display the LF */
      PcConsPutChar('\r');
    }

  /* If we are displaying a TAB '\t' then display 8 spaces ' ' */
  if ('\t' == Ch)
    {
      /* Display the 8 spaces ' ' */
      PcConsPutChar(' ');
      PcConsPutChar(' ');
      PcConsPutChar(' ');
      PcConsPutChar(' ');
      PcConsPutChar(' ');
      PcConsPutChar(' ');
      PcConsPutChar(' ');
      PcConsPutChar(' ');
      return;
    }

  /* Int 10h AH=0Eh
   * VIDEO - TELETYPE OUTPUT
   *
   * AH = 0Eh
   * AL = character to write
   * BH = page number
   * BL = foreground color (graphics modes only)
   */
  Regs.b.ah = 0x0E;
  Regs.b.al = Ch;
  Regs.w.bx = 1;
  Int386(0x10, &Regs, &Regs);
}

#define KEY_EXTENDED    0x00

BOOLEAN
PcConsKbHit(VOID)
{
  REGS Regs;

  /* Int 16h AH=01h
   * KEYBOARD - CHECK FOR KEYSTROKE
   *
   * AH = 01h
   * Return:
   * ZF set if no keystroke available
   * ZF clear if keystroke available
   * AH = BIOS scan code
   * AL = ASCII character
   */
  Regs.b.ah = 0x01;
  Int386(0x16, &Regs, &Regs);

  return 0 == (Regs.x.eflags & EFLAGS_ZF);
}

int
PcConsGetCh(void)
{
  REGS Regs;
  static BOOLEAN ExtendedKey = FALSE;
  static char ExtendedScanCode = 0;

  /* If the last time we were called an
   * extended key was pressed then return
   * that keys scan code. */
  if (ExtendedKey)
    {
      ExtendedKey = FALSE;
      return ExtendedScanCode;
    }

  /* Int 16h AH=00h
   * KEYBOARD - GET KEYSTROKE
   *
   * AH = 00h
   * Return:
   * AH = BIOS scan code
   * AL = ASCII character
   */
  Regs.b.ah = 0x00;
  Int386(0x16, &Regs, &Regs);

  /* Check for an extended keystroke */
  if (0 == Regs.b.al)
    {
      ExtendedKey = TRUE;
      ExtendedScanCode = Regs.b.ah;
    }

  /* Return keystroke */
  return Regs.b.al;
}


#define VIDEOVGA_MEM_ADDRESS  0xA0000
#define VIDEOTEXT_MEM_ADDRESS 0xB8000
#define VIDEOTEXT_MEM_SIZE    0x8000

static ULONG ScreenWidth = 80;                           /* Screen Width in characters */
static ULONG ScreenHeight = 25;                          /* Screen Height in characters */
static ULONG BytesPerScanLine = 160;                     /* Number of bytes per scanline (delta) */

VOID
PcVideoClearScreen(UCHAR Attr)
{
  USHORT AttrChar;
  USHORT *BufPtr;

  AttrChar = ((USHORT) Attr << 8) | ' ';
  for (BufPtr = (USHORT *) VIDEOTEXT_MEM_ADDRESS;
       BufPtr < (USHORT *) (VIDEOTEXT_MEM_ADDRESS + VIDEOTEXT_MEM_SIZE);
       BufPtr++)
    {
      _PRAGMA_WARNING_SUPPRESS(__WARNING_DEREF_NULL_PTR)
      *BufPtr = AttrChar;
    }
}

VOID
PcVideoPutChar(int Ch, UCHAR Attr, unsigned X, unsigned Y)
{
  USHORT *BufPtr;

  BufPtr = (USHORT *) (ULONG_PTR)(VIDEOTEXT_MEM_ADDRESS + Y * BytesPerScanLine + X * 2);
  *BufPtr = ((USHORT) Attr << 8) | (Ch & 0xff);
}

VOID
PcVideoGetTextCursorPosition(ULONG* X, ULONG* Y)
{
    REGS    Regs;

    // Int 10h AH=03h
    // VIDEO - GET CURSOR POSITION AND SIZE
    //
    // AH = 03h
    // BH = page number
    // 0-3 in modes 2&3
    // 0-7 in modes 0&1
    // 0 in graphics modes
    // Return:
    // AX = 0000h (Phoenix BIOS)
    // CH = start scan line
    // CL = end scan line
    // DH = row (00h is top)
    // DL = column (00h is left)
    Regs.b.ah = 0x03;
    Regs.b.bh = 0x00;
    Int386(0x10, &Regs, &Regs);

    *X = Regs.b.dl;
    *Y = Regs.b.dh;
}

VOID
PcVideoSetTextCursorPosition(UCHAR X, UCHAR Y)
{
  REGS Regs;

  /* Int 10h AH=02h
   * VIDEO - SET CURSOR POSITION
   *
   * AH = 02h
   * BH = page number
   * 0-3 in modes 2&3
   * 0-7 in modes 0&1
   * 0 in graphics modes
   * DH = row (00h is top)
   * DL = column (00h is left)
   * Return:
   * Nothing
   */
  Regs.b.ah = 0x02;
  Regs.b.bh = 0x00;
  Regs.b.dh = Y;
  Regs.b.dl = X;
  Int386(0x10, &Regs, &Regs);
}

static VOID
PcVideoDefineCursor(UCHAR StartScanLine, UCHAR EndScanLine)
{
  REGS Regs;

  /* Int 10h AH=01h
   * VIDEO - SET TEXT-MODE CURSOR SHAPE
   *
   * AH = 01h
   * CH = cursor start and options
   * CL = bottom scan line containing cursor (bits 0-4)
   * Return:
   * Nothing
   *
   * Specify the starting and ending scan lines to be occupied
   * by the hardware cursor in text modes.
   *
   * AMI 386 BIOS and AST Premier 386 BIOS will lock up the
   * system if AL is not equal to the current video mode.
   *
   * Bitfields for cursor start and options:
   *
   * Bit(s)    Description
   * 7         should be zero
   * 6,5       cursor blink
   * (00=normal, 01=invisible, 10=erratic, 11=slow).
   * (00=normal, other=invisible on EGA/VGA)
   * 4-0       topmost scan line containing cursor
   */
  Regs.b.ah = 0x01;
  Regs.b.al = 0x03;
  Regs.b.ch = StartScanLine;
  Regs.b.cl = EndScanLine;
  Int386(0x10, &Regs, &Regs);
}

VOID
PcVideoHideShowTextCursor(BOOLEAN Show)
{
  if (Show)
    {
      PcVideoDefineCursor(0x0D, 0x0E);
    }
  else
    {
      PcVideoDefineCursor(0x20, 0x00);
    }
}

VOID
PcHwIdle(VOID)
{
    REGS Regs;

    /* Select APM 1.0+ function */
    Regs.b.ah = 0x53;

    /* Function 05h: CPU idle */
    Regs.b.al = 0x05;

    /* Call INT 15h */
    Int386(0x15, &Regs, &Regs);

    /* Check if successfull (CF set on error) */
    if (INT386_SUCCESS(Regs))
        return;

    /*
     * No futher processing here.
     * Optionally implement HLT instruction handling.
     */
}



#define SCREEN_ATTR 0x1F    // Bright white on blue background

/* Used to store the current X and Y position on the screen */
ULONG i386_ScreenPosX = 0;
ULONG i386_ScreenPosY = 0;

static void
i386PrintText(UCHAR Attr, char *pszText)
{
    char chr;
    while (1)
    {
        chr = *pszText++;

        if (chr == 0) break;
        if (chr == '\n')
        {
            i386_ScreenPosY++;
            i386_ScreenPosX = 0;
            continue;
        }

        PcVideoPutChar(chr, Attr, i386_ScreenPosX, i386_ScreenPosY);
        i386_ScreenPosX++;
    }
}

static void
PrintTextColorV(UCHAR Attr, const char *format, va_list argptr)
{
    char buffer[1024];

    _vsnprintf(buffer, sizeof(buffer), format, argptr);
    buffer[sizeof(buffer) - 1] = 0;
    i386PrintText(Attr, buffer);
}

static void
PrintTextColor(UCHAR Attr, const char *format, ...)
{
    va_list argptr;

    va_start(argptr, format);
    PrintTextColorV(Attr, format, argptr);
    va_end(argptr);
}

static void
PrintText(const char *format, ...)
{
    va_list argptr;

    va_start(argptr, format);
    PrintTextColorV(SCREEN_ATTR, format, argptr);
    va_end(argptr);
}


// We need to emulate these, because the original ones don't work in freeldr
// These functions are here, because they need to be in the main compilation unit
// and cannot be in a library.
int __cdecl wctomb(char *mbchar, wchar_t wchar)
{
    *mbchar = (char)wchar;
    return 1;
}

int __cdecl mbtowc(wchar_t *wchar, const char *mbchar, size_t count)
{
    *wchar = (wchar_t)*mbchar;
    return 1;
}

// The wctype table is 144 KB, too much for poor freeldr
int __cdecl iswctype(wint_t wc, wctype_t wctypeFlags)
{
    return _isctype((char)wc, wctypeFlags);
}


VOID
NTAPI
RtlAssert(IN PVOID FailedAssertion,
          IN PVOID FileName,
          IN ULONG LineNumber,
          IN PCHAR Message OPTIONAL)
{
    if (Message)
    {
        PrintText("Assertion \'%s\' failed at %s line %lu: %s\n",
                 (PCHAR)FailedAssertion,
                 (PCHAR)FileName,
                 LineNumber,
                 Message);
    }
    else
    {
        PrintText("Assertion \'%s\' failed at %s line %lu\n",
                 (PCHAR)FailedAssertion,
                 (PCHAR)FileName,
                 LineNumber);
    }

    // DbgBreakPoint();
    for (;;);
}


VOID NTAPI NtProcessStartup(IN PBOOT_CONTEXT BootContext)
{
    INT i;
    ULONG oldScreenPosX;
    ULONG oldScreenPosY;

    /* Check the validity of the BootContext structure */
    if (!IS_BOOT_CONTEXT_VALID(BootContext))
        return; // Not valid, bail out quickly.
    ASSERT(BootContext->ImageBase == &__ImageBase);

    /* Initialize globals using the BootContext structure */
    Int386 = BootContext->ServicesTable->Int386;

    PcVideoHideShowTextCursor(FALSE);
    PcVideoSetTextCursorPosition(0, 0);
    PcVideoGetTextCursorPosition(&i386_ScreenPosX, &i386_ScreenPosY);

    PcVideoClearScreen(0x20 | 0x0F); // Background green, foreground white
    PrintTextColor(0x20 | 0x0F, "\nHello from the 32-bit PE image!\n===============================\n\n");
    PrintTextColor(0x20 | 0x0F, "Image base 0x%p, BootContext 0x%p\n", &__ImageBase, BootContext);

    PrintTextColor(0x20 | 0x0F,
        "BootContext dump:\n"
        "Signature           : 0x%lx '%c%c%c%c'\n"
        "Size                : 0x%lx\n"
        "Flags               : 0x%lx\n"
        "BootDrive           : 0x%lx\n"
        "BootPartition       : 0x%lx\n"
        "MachineType         : %lu\n"
        "ImageBase           : 0x%p\n"
        "ImageSize           : 0x%lx\n"
        "ImageType           : 0x%lx\n"
        "BiosCallBuffer      : 0x%p\n"
        "BiosCallBufferSize  : 0x%lx\n"
        "ServicesTable       : 0x%p\n"
        "CommandLine         : 0x%p '%s'\n"
        "\n",
        BootContext->Signature,
            ((PCHAR)&BootContext->Signature)[0],
            ((PCHAR)&BootContext->Signature)[1],
            ((PCHAR)&BootContext->Signature)[2],
            ((PCHAR)&BootContext->Signature)[3],
        BootContext->Size,
        BootContext->Flags,
        BootContext->BootDrive,
        BootContext->BootPartition,
        BootContext->MachineType,
        BootContext->ImageBase,
        BootContext->ImageSize,
        BootContext->ImageType,
        BootContext->BiosCallBuffer,
        BootContext->BiosCallBufferSize,
        BootContext->ServicesTable,
        BootContext->CommandLine, BootContext->CommandLine);

    PrintTextColor(0xC0 | 0x0F, "Press any key to restart..."); // Background-intensity bit 0x80 triggers blinking.
    for (;;)
    {
        if (PcConsKbHit())
        {
            if (PcConsGetCh() == KEY_EXTENDED)
                PcConsGetCh();
            break;
        }
        PcHwIdle();
    }
    i386_ScreenPosX = 0;
    PrintTextColor(0x20 | 0x0F, "                           "); // Erase the prompt
    i386_ScreenPosX = 0;

    // Poor-man's wait.
    oldScreenPosX = i386_ScreenPosX;
    oldScreenPosY = i386_ScreenPosY;
    for (i = 255; i >= 0; --i)
    {
        PrintTextColor(0x30 | 0x0E, "i = %i  ", i);
        i386_ScreenPosX = oldScreenPosX;
        i386_ScreenPosY = oldScreenPosY;
        PcHwIdle();
    }

    PcVideoSetTextCursorPosition(i386_ScreenPosX, i386_ScreenPosY);
}
