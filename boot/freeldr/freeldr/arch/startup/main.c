/*
cl /Zi /nologo /W4 /WX- /O2 /Oi /Oy- /GL /X /c /GF /Gm- /Zp4 /GS- /Gy- /fp:precise /fp:except- /Zc:wchar_t /Zc:forScope /GR- /openmp- /Gd /analyze- /Fd"main.pdb" /Fo"main.obj" main.c

link /MANIFEST:NO /ALLOWISOLATION:NO /DEBUG /OPT:REF /OPT:ICF



cl func.c main.c /Femain.exe /link /NODEFAULTLIB /SUBSYSTEM:NATIVE /DRIVER /ENTRY:NtProcessStartup /BASE:"0x00100000" /FIXED /MACHINE:X86 /ALIGN:4096 /SAFESEH:NO

obj2bin.exe c:\Temp\Essai\main.exe c:\temp\essai\main_exe.bin 0x10000
objcopy c:\Temp\Essai\main.exe -O binary c:\Temp\Essai\main_objcopy.bin

*/

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
#include <arch/pc/x86common.h>
// #include <arch/pc/pxe.h>

#if defined(_M_IX86)
#include <arch/i386/i386.h>
#include <internal/i386/intrin_i.h>
#endif


///////////////////////

#include <arch/pc/startup.h>

/*** See pcbios.h ***/
/***/ int __cdecl Int386(int ivec, REGS* in, REGS* out); /***/

extern BOOT_CONTEXT BootData;
extern ULONG _bss_end__;


#define ROUND_DOWN(n, align) \
    (((ULONG_PTR)(n)) & ~((align) - 1l))

#define ROUND_UP(n, align) \
    ROUND_DOWN(((ULONG_PTR)(n)) + (align) - 1, (align))

#define RVA(m, b) ((PVOID)((ULONG_PTR)(b) + (ULONG_PTR)(m)))

#define IMAGE_FILE_RELOCS_STRIPPED      0x0001
#define IMAGE_FILE_EXECUTABLE_IMAGE     0x0002


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


extern int myfunc(int i);

void _cdecl DiskStopFloppyMotor(void)
{
}




KIDTENTRY DECLSPEC_ALIGN(4) i386Idt[32];
KDESCRIPTOR i386IdtDescriptor = {0, 255, (ULONG)i386Idt};

static
void
InitIdtVector(
    UCHAR Vector,
    PVOID ServiceHandler,
    USHORT Access)
{
    i386Idt[Vector].Offset = (ULONG)ServiceHandler & 0xffff;
    i386Idt[Vector].ExtendedOffset = (ULONG)ServiceHandler >> 16;
    i386Idt[Vector].Selector = PMODE_CS;
    i386Idt[Vector].Access = Access;
}

void
__cdecl
InitIdt(void)
{
    InitIdtVector(0, i386DivideByZero, 0x8e00);
    InitIdtVector(1, i386DebugException, 0x8e00);
    InitIdtVector(2, i386NMIException, 0x8e00);
    InitIdtVector(3, i386Breakpoint, 0x8e00);
    InitIdtVector(4, i386Overflow, 0x8e00);
    InitIdtVector(5, i386BoundException, 0x8e00);
    InitIdtVector(6, i386InvalidOpcode, 0x8e00);
    InitIdtVector(7, i386FPUNotAvailable, 0x8e00);
    InitIdtVector(8, i386DoubleFault, 0x8e00);
    InitIdtVector(9, i386CoprocessorSegment, 0x8e00);
    InitIdtVector(10, i386InvalidTSS, 0x8e00);
    InitIdtVector(11, i386SegmentNotPresent, 0x8e00);
    InitIdtVector(12, i386StackException, 0x8e00);
    InitIdtVector(13, i386GeneralProtectionFault, 0x8e00);
    InitIdtVector(14, i386PageFault, 0x8e00);
    InitIdtVector(16, i386CoprocessorError, 0x8e00);
    InitIdtVector(17, i386AlignmentCheck, 0x8e00);
    InitIdtVector(18, i386MachineCheck, 0x8e00);
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
    char buffer[256];

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

static const char *i386ExceptionDescriptionText[] =
{
    "Exception 00: DIVIDE BY ZERO\n\n",
    "Exception 01: DEBUG EXCEPTION\n\n",
    "Exception 02: NON-MASKABLE INTERRUPT EXCEPTION\n\n",
    "Exception 03: BREAKPOINT (INT 3)\n\n",
    "Exception 04: OVERFLOW\n\n",
    "Exception 05: BOUND EXCEPTION\n\n",
    "Exception 06: INVALID OPCODE\n\n",
    "Exception 07: FPU NOT AVAILABLE\n\n",
    "Exception 08: DOUBLE FAULT\n\n",
    "Exception 09: COPROCESSOR SEGMENT OVERRUN\n\n",
    "Exception 0A: INVALID TSS\n\n",
    "Exception 0B: SEGMENT NOT PRESENT\n\n",
    "Exception 0C: STACK EXCEPTION\n\n",
    "Exception 0D: GENERAL PROTECTION FAULT\n\n",
    "Exception 0E: PAGE FAULT\n\n",
    "Exception 0F: Reserved\n\n",
    "Exception 10: COPROCESSOR ERROR\n\n",
    "Exception 11: ALIGNMENT CHECK\n\n",
    "Exception 12: MACHINE CHECK\n\n"
};

void
NTAPI
i386PrintExceptionText(ULONG TrapIndex, PKTRAP_FRAME TrapFrame, PKSPECIAL_REGISTERS Special)
{
    PUCHAR InstructionPointer;

    PcVideoHideShowTextCursor(FALSE);
    PcVideoClearScreen(SCREEN_ATTR);

    i386_ScreenPosX = 0;
    i386_ScreenPosY = 0;

    PrintText("An error occured in " "STARTROM" "\n"
              "Report this error to the ReactOS Development mailing list <ros-dev@reactos.org>\n\n"
              "0x%02lx: %s\n", TrapIndex, i386ExceptionDescriptionText[TrapIndex]);
#ifdef _M_IX86
    PrintText("EAX: %.8lx        ESP: %.8lx        CR0: %.8lx        DR0: %.8lx\n",
              TrapFrame->Eax, TrapFrame->HardwareEsp, Special->Cr0, TrapFrame->Dr0);
    PrintText("EBX: %.8lx        EBP: %.8lx        CR1: ????????        DR1: %.8lx\n",
              TrapFrame->Ebx, TrapFrame->Ebp, TrapFrame->Dr1);
    PrintText("ECX: %.8lx        ESI: %.8lx        CR2: %.8lx        DR2: %.8lx\n",
              TrapFrame->Ecx, TrapFrame->Esi, Special->Cr2, TrapFrame->Dr2);
    PrintText("EDX: %.8lx        EDI: %.8lx        CR3: %.8lx        DR3: %.8lx\n",
              TrapFrame->Edx, TrapFrame->Edi, Special->Cr3, TrapFrame->Dr3);
    PrintText("                                                               DR6: %.8lx\n",
              TrapFrame->Dr6);
    PrintText("                                                               DR7: %.8lx\n\n",
              TrapFrame->Dr7);
    PrintText("CS: %.4lx        EIP: %.8lx\n",
              TrapFrame->SegCs, TrapFrame->Eip);
    PrintText("DS: %.4lx        ERROR CODE: %.8lx\n",
              TrapFrame->SegDs, TrapFrame->ErrCode);
    PrintText("ES: %.4lx        EFLAGS: %.8lx\n",
              TrapFrame->SegEs, TrapFrame->EFlags);
    PrintText("FS: %.4lx        GDTR Base: %.8lx Limit: %.4x\n",
              TrapFrame->SegFs, Special->Gdtr.Base, Special->Gdtr.Limit);
    PrintText("GS: %.4lx        IDTR Base: %.8lx Limit: %.4x\n",
              TrapFrame->SegGs, Special->Idtr.Base, Special->Idtr.Limit);
    PrintText("SS: %.4lx        LDTR: %.4lx TR: %.4lx\n\n",
              TrapFrame->HardwareSegSs, Special->Ldtr, Special->Idtr.Limit);

    // i386PrintFrames(TrapFrame);                        // Display frames
    InstructionPointer = (PUCHAR)TrapFrame->Eip;
#else
    PrintText("RAX: %.8lx        R8:  %.8lx        R12: %.8lx        RSI: %.8lx\n",
              TrapFrame->Rax, TrapFrame->R8, 0, TrapFrame->Rsi);
    PrintText("RBX: %.8lx        R9:  %.8lx        R13: %.8lx        RDI: %.8lx\n",
              TrapFrame->Rbx, TrapFrame->R9, 0, TrapFrame->Rdi);
    PrintText("RCX: %.8lx        R10: %.8lx        R14: %.8lx        RBP: %.8lx\n",
              TrapFrame->Rcx, TrapFrame->R10, 0, TrapFrame->Rbp);
    PrintText("RDX: %.8lx        R11: %.8lx        R15: %.8lx        RSP: %.8lx\n",
              TrapFrame->Rdx, TrapFrame->R11, 0, TrapFrame->Rsp);

    PrintText("CS: %.4lx        RIP: %.8lx\n",
              TrapFrame->SegCs, TrapFrame->Rip);
    PrintText("DS: %.4lx        ERROR CODE: %.8lx\n",
              TrapFrame->SegDs, TrapFrame->ErrorCode);
    PrintText("ES: %.4lx        EFLAGS: %.8lx\n",
              TrapFrame->SegEs, TrapFrame->EFlags);
    PrintText("FS: %.4lx        GDTR Base: %.8lx Limit: %.4x\n",
              TrapFrame->SegFs, Special->Gdtr.Base, Special->Gdtr.Limit);
    PrintText("GS: %.4lx        IDTR Base: %.8lx Limit: %.4x\n",
              TrapFrame->SegGs, Special->Idtr.Base, Special->Idtr.Limit);
    PrintText("SS: %.4lx        LDTR: %.4lx TR: %.4lx\n\n",
              TrapFrame->SegSs, Special->Ldtr, Special->Idtr.Limit);
    InstructionPointer = (PUCHAR)TrapFrame->Rip;
#endif
    PrintText("\nInstruction stream: %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x \n",
              InstructionPointer[0], InstructionPointer[1],
              InstructionPointer[2], InstructionPointer[3],
              InstructionPointer[4], InstructionPointer[5],
              InstructionPointer[6], InstructionPointer[7]);
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


static BOOLEAN
ValidatePEHeader( // ParsePEImage
    IN PVOID ImageBuffer,
    // IN ULONG nImageSize,
    OUT PIMAGE_NT_HEADERS* pNtHeaders,
    OUT PIMAGE_FILE_HEADER* pFileHeader)
{
    PIMAGE_DOS_HEADER DosHeader = (PIMAGE_DOS_HEADER)ImageBuffer;
    /*
     * NOTE: The correct structure that we support (IMAGE_OPTIONAL_HEADER32 or
     * IMAGE_OPTIONAL_HEADER64) is automatically selected at compilation time.
     */
    PIMAGE_OPTIONAL_HEADER OptionalHeader;
    ULONG NtHeaderOffset;
    // ULONG TotalHeadersSize = 0;

    /* Ensure it's a PE image */
    if (!(/* nImageSize >= sizeof(IMAGE_DOS_HEADER) && */ DosHeader->e_magic == IMAGE_DOS_SIGNATURE))
    {
        /* Fail */
        PrintText("0x%p - Not a valid PE image!\n", ImageBuffer);
        return FALSE;
    }

    /* Get the offset to the NT headers */
    NtHeaderOffset = DosHeader->e_lfanew;

#if 0
    /* Make sure the file header fits into the size */
    TotalHeadersSize += NtHeaderOffset +
                        FIELD_OFFSET(IMAGE_NT_HEADERS, FileHeader) + (sizeof(((IMAGE_NT_HEADERS *)0)->FileHeader));
    if (TotalHeadersSize >= nImageSize)
    {
        /* Fail */
        PrintText("0x%p - NT headers beyond image size!\n", ImageBuffer);
        return FALSE;
    }
#endif

    /* Now get a pointer to the NT Headers */
    *pNtHeaders = (PIMAGE_NT_HEADERS)RVA(ImageBuffer, NtHeaderOffset);

    /* Verify the PE Signature */
    if ((*pNtHeaders)->Signature != IMAGE_NT_SIGNATURE)
    {
        /* Fail */
        PrintText("0x%p - Invalid image NT signature!\n", ImageBuffer);
        return FALSE;
    }

    /* Ensure this is an executable image */
    if (((*pNtHeaders)->FileHeader.Characteristics & IMAGE_FILE_EXECUTABLE_IMAGE) == 0)
    {
        /* Fail */
        PrintText("0x%p - Invalid executable image!\n", ImageBuffer);
        return FALSE;
    }

    /* Get the COFF header */
    *pFileHeader = &(*pNtHeaders)->FileHeader;

    /* Check for the presence of the optional header */
    if ((*pFileHeader)->SizeOfOptionalHeader == 0)
    {
        /* Fail */
        PrintText("0x%p - Unsupported PE image (no optional header)!\n", ImageBuffer);
        return FALSE;
    }

#if 0
    /* Make sure the optional file header fits into the size */
    TotalHeadersSize += (*pFileHeader)->SizeOfOptionalHeader;
    if (TotalHeadersSize >= nImageSize)
    {
        /* Fail */
        PrintText("0x%p - NT optional header beyond image size!\n", ImageBuffer);
        return FALSE;
    }
#endif

    /*
     * Retrieve the optional header and be sure that its size corresponds
     * to its signature. Note that we only support either x86 or x64 images.
     */
    OptionalHeader = (PVOID)(*pFileHeader + 1);
    if (!((*pFileHeader)->SizeOfOptionalHeader == sizeof(IMAGE_OPTIONAL_HEADER) &&
          OptionalHeader->Magic == IMAGE_NT_OPTIONAL_HDR_MAGIC))
    {
        /* Fail */
        PrintText("0x%p - Invalid or unrecognized NT optional header!\n", ImageBuffer);
        return FALSE;
    }

    return TRUE;
}

static PVOID
LoadPEImage(
    IN PVOID ImageBuffer,
    IN PIMAGE_NT_HEADERS NtHeaders,
    IN PIMAGE_FILE_HEADER FileHeader)
{
    PVOID ImageBase;
    /*
     * NOTE: The correct structure that we support (IMAGE_OPTIONAL_HEADER32 or
     * IMAGE_OPTIONAL_HEADER64) is automatically selected at compilation time.
     */
    PIMAGE_OPTIONAL_HEADER OptionalHeader;
    PIMAGE_SECTION_HEADER pSection;
    ULONG SizeOfImage, SizeOfHeaders, SectionAlignment;
    ULONG SectionSize, RawSize;
    ULONG i;

    /* Be sure the user didn't pass inconsistent information */
    ASSERT(&NtHeaders->FileHeader == FileHeader);

    /* Retrieve the optional header, its validity has been checked in ParsePEImage() */
    OptionalHeader = (PVOID)(FileHeader + 1);
    ASSERT(OptionalHeader == &NtHeaders->OptionalHeader);

    if (!(FileHeader->SizeOfOptionalHeader == sizeof(IMAGE_OPTIONAL_HEADER) &&
          OptionalHeader->Magic == IMAGE_NT_OPTIONAL_HDR_MAGIC))
    {
        /* Fail */
        PrintText("0x%p - Invalid or unrecognized NT optional header!\n", ImageBuffer);
        return NULL;
    }

    /* Check for supported architecture - We only support either x86 or x64 images */
#if defined(_M_IX86)
    if (FileHeader->Machine != IMAGE_FILE_MACHINE_I386)
#elif defined(_M_AMD64) || defined(_M_X64)
    if (FileHeader->Machine != IMAGE_FILE_MACHINE_AMD64)
#else
#error Invalid architecture. StartROM is designed only for x86 or x64 architectures!
#endif
    {
        PrintText("0x%p - Unsupported machine type 0x%04x!\n", ImageBuffer, FileHeader->Machine);
        return NULL;
    }

    /* We can only load native boot images */
    if (OptionalHeader->Subsystem != IMAGE_SUBSYSTEM_NATIVE)
    {
        PrintText("0x%p - Unsupported subsystem type 0x%04x!\n", ImageBuffer, OptionalHeader->Subsystem);
        return NULL;
    }

    /* Find the actual size of the image in memory, and the size of the headers in the file */
    SizeOfImage = OptionalHeader->SizeOfImage;
    SizeOfHeaders = OptionalHeader->SizeOfHeaders;
    SectionAlignment = OptionalHeader->SectionAlignment;

    // TODO: We could check this in the ValidatePEImage() function above...
    if (SizeOfImage < SizeOfHeaders)
    {
        /* Bail out if they're bigger than the image! */
        // STATUS_INVALID_IMAGE_FORMAT;
        PrintText("0x%p - This PE image has an invalid size of headers (0x%lu, expected less than 0x%lu)!\n", ImageBuffer, SizeOfHeaders, SizeOfImage);
        return NULL;
    }

    /* If there are actually no sections in this image, fail */
    if (FileHeader->NumberOfSections == 0)
    {
        PrintText("0x%p - This PE image does not have any sections!\n", ImageBuffer);
        return NULL;
    }

    /* Retrieve the image preferred load address */
    ImageBase = (PVOID)OptionalHeader->ImageBase;
    if (!ImageBase)
    {
        PrintText("0x%p - This PE image has an invalid image base (0x%p)!\n", ImageBuffer, ImageBase);
        return NULL;
    }
    //
    // TODO: We should check whether the memory area between
    // ImageBase and (ULONG_PTR)ImageBase + SizeOfImage is free.
    // If not, complain and bail out.
    //

    /* Load the PE image headers to destination */
    // FIXME: They are not obligatorily continuous!!
    RtlMoveMemory(ImageBase, ImageBuffer, NtHeaders->OptionalHeader.SizeOfHeaders);

    /* Iterate through the sections and load them at their respective address */
    pSection = RVA(FileHeader + 1, FileHeader->SizeOfOptionalHeader);
    for (i = 0; i < FileHeader->NumberOfSections; ++i, ++pSection)
    {
        /* Make sure that the section fits within the image */
        if ((pSection->VirtualAddress > SizeOfImage) ||
            (RVA(ImageBase, pSection->VirtualAddress) < ImageBase))
        {
            PrintText("0x%p - Section '%s' outside of the image.\n", ImageBuffer, (char*)pSection->Name);
            return NULL;
        }

        /* Get the section virtual size and the raw size */
        SectionSize = pSection->Misc.VirtualSize;
        RawSize = pSection->SizeOfRawData;

        /* Handle a case when VirtualSize equals 0 */
        if (SectionSize == 0)
            SectionSize = RawSize;

        /* If PointerToRawData is 0, then force its size to be also 0 */
        if (pSection->PointerToRawData == 0)
            RawSize = 0;
        /* Truncate the loaded size to the VirtualSize extents */
        else if (RawSize > SectionSize)
            RawSize = SectionSize;

        /* Actually read the section (if its size is not 0) */
        if (RawSize != 0)
        {
            /* Read this section from the file, size = SizeOfRawData */
            RtlMoveMemory(RVA(ImageBase, pSection->VirtualAddress),
                          RVA(ImageBuffer, pSection->PointerToRawData),
                          RawSize);
        }

        /* Size of data is less than the virtual size - fill up the remainder with zeroes */
        if (RawSize < SectionSize)
        {
            RtlZeroMemory(RVA(ImageBase, pSection->VirtualAddress + RawSize),
                          SectionSize - RawSize);
        }
    }

#if 0 // BOOTMGR.EXE / OSLOADER.EXE does not have relocations (yet...)
    /* Relocate the image */
    if (!(BOOLEAN)LdrRelocateImage(ImageBase,
                                   // NtHeaders,
                                   TRUE,
                                   TRUE, /* In case of conflict still return success */
                                   FALSE))
    {
        PrintText("0x%p - Failed to relocate image!\n", ImageBuffer);
        return NULL;
    }
#endif

    /* We succeeded, return the (physical address of the) entry point */
    return RVA(ImageBase, OptionalHeader->AddressOfEntryPoint);
}

// NOTE: Same prototype as NtProcessStartup().
typedef VOID (NTAPI *BOOTMGR_ENTRY_POINT) (PVOID ptr /*PLOADER_PARAMETER_BLOCK LoaderBlock*/);

VOID _cdecl BootMain(PVOID ptr)
{
    INT i;
    ULONG oldScreenPosX;
    ULONG oldScreenPosY;
    ULONG_PTR ImageBase;
    PIMAGE_NT_HEADERS NtHeaders;
    PIMAGE_FILE_HEADER FileHeader;
    BOOTMGR_ENTRY_POINT EntryPoint;

    PcVideoHideShowTextCursor(FALSE);
    PcVideoSetTextCursorPosition(0, 0);

    // PcConsPutChar('H');
    PcVideoClearScreen(0x50 | 0x0E); // Background purple, foreground yellow
    PrintTextColor(0x50 | 0x0E, "\nHello from StartROM!\n====================\n");
    PrintTextColor(0x90 | 0x0F, "\n\nPress any key to load..."); // Background-intensity bit 0x80 triggers blinking.
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
    PrintTextColor(0x50 | 0x0F, "                        "); // Erase the prompt
    PrintText("\n\n");


    /*
     * Search for a valid PE image (BOOTMGR.EXE / OSLOADER.EXE) that is
     * appended after us, aligned on file sector boundary 0x200.
     */
    ImageBase = ROUND_UP((ULONG_PTR)&_bss_end__, 0x200);
    if (!ValidatePEHeader((PVOID)ImageBase, &NtHeaders, &FileHeader))
    {
        /* Try again one sector after; if we fail, bail out */
        ImageBase += 0x200;
        if (!ValidatePEHeader((PVOID)ImageBase, &NtHeaders, &FileHeader))
        {
            PrintTextColor(0x10 | 0x0F, "\nCould not find a valid OSLOADER.EXE! Rebooting...\n");
            goto Fail;
        }
    }

    /* Load it at its preferred base address */
    EntryPoint = (BOOTMGR_ENTRY_POINT)LoadPEImage((PVOID)ImageBase /*ImageBuffer*/,
                                                  NtHeaders, FileHeader);
    if (!EntryPoint)
    {
        PrintTextColor(0x10 | 0x0F, "\nCould not load OSLOADER.EXE! Rebooting...\n");
        goto Fail;
    }

    /* TODO: Prepare some extra environment ??? (IDT, GDT, Stack) */

    PcVideoSetTextCursorPosition(i386_ScreenPosX, i386_ScreenPosY);

    /* Pass control to it */
    //
    // FIXME: We shall send instead a boot block record
    // containing different type of information and a table
    // to services exported by StartROM.
    //
    /* Most of the BootContext structure is statically initialized */
    // BootData.MachineType = ;
    BootData.ImageBase = (PVOID)NtHeaders->OptionalHeader.ImageBase; // FIXME HACK 1
    BootData.ImageSize = NtHeaders->OptionalHeader.SizeOfImage; // FIXME HACK 2
    BootData.ImageType = FileHeader->Machine; // FIXME HACK 3
    (*EntryPoint)(&BootData);

    /* If we reached there, we somehow failed to start, just reboot */

    PcVideoGetTextCursorPosition(&i386_ScreenPosX, &i386_ScreenPosY);

Fail:
    PrintTextColor(0x10 | 0x0F, "\n\nPress any key to restart...");
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
    PrintText("\n\n");
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
}
