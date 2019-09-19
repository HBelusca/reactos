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
#include <arch/pc/pxe.h>
// #include <arch/i386/drivemap.h>

#if defined(_M_IX86)
#include <arch/i386/i386.h>
#include <internal/i386/intrin_i.h>
#endif


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



VOID _cdecl BootMain(PVOID ptr)
{
    INT i;
    ULONG oldScreenPosX;
    ULONG oldScreenPosY;

    PcVideoHideShowTextCursor(FALSE);
    PcVideoSetTextCursorPosition(0, 0);

    // PcConsPutChar('H');
    PcVideoClearScreen(0x50 | 0x0E); // Background purple, foreground yellow
    PrintTextColor(0x50 | 0x0E, "\nHello from StartROM!\n====================\n");
    PrintTextColor(0x90 | 0x0F, "\n\nPress any key to quit..."); // Background-intensity bit 0x80 triggers blinking.
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

    PrintTextColor(0x10 | 0x0F, "\n\nWe quit!\n\n");
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

    //
    // TODO:
    //
    // 1. Determine the offset of the last part of our binary.
    //
    // 2. After it should be the PE code of OSLOADER.EXE / BOOTMGR.EXE .
    //    We might as well try to loop for a little while in memory in order
    //    to search for a valid MZ / PE header, but not too far otherwise
    //    we need to fail and show an error message (+ prompting for restart).
    //
    // 3. If everything succeeded so far, we have to load the image in memory,
    //    see e.g. obj2bin code for PE, then (OPTIONAL?) apply relocations
    //    and jump to the entry point of the PE.
    //
    // 4. Any return from the entry point should trigger an immediate reboot.
    //
#if 0

    /* Address the image with es segment */
    mov ax, FREELDR_PE_BASE / 16
    mov es, ax

#define IMAGE_DOS_HEADER_e_lfanew 60
#define IMAGE_FILE_HEADER_SIZE 20
#define IMAGE_OPTIONAL_HEADER_AddressOfEntryPoint 16

    /* Get address of optional header */
    mov eax, dword ptr es:[IMAGE_DOS_HEADER_e_lfanew]
    add eax, 4 + IMAGE_FILE_HEADER_SIZE

    /* Get address of entry point */
    mov eax, dword ptr es:[eax + IMAGE_OPTIONAL_HEADER_AddressOfEntryPoint]
    add eax, FREELDR_PE_BASE

#endif
}
