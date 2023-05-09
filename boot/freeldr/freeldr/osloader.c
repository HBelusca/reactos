
#include <freeldr.h>

MACHVTBL MachVtbl;
PUIVTBL UiTable;

#define KEY_EXTENDED    0x00

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

        MachVideoPutChar(chr, Attr, i386_ScreenPosX, i386_ScreenPosY);
        i386_ScreenPosX++;
    }
}

void
PrintTextColorV(UCHAR Attr, const char *format, va_list argptr)
{
    char buffer[1024];

    _vsnprintf(buffer, sizeof(buffer), format, argptr);
    buffer[sizeof(buffer) - 1] = 0;
    i386PrintText(Attr, buffer);
}

void
PrintTextColor(UCHAR Attr, const char *format, ...)
{
    va_list argptr;

    va_start(argptr, format);
    PrintTextColorV(Attr, format, argptr);
    va_end(argptr);
}

void
PrintText(const char *format, ...)
{
    va_list argptr;

    va_start(argptr, format);
    PrintTextColorV(SCREEN_ATTR, format, argptr);
    va_end(argptr);
}


#if 0

INT
PrintTextColor(
    _In_ UCHAR Attributes,
    _In_ PCSTR Format,
    ...)
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

#endif


VOID
NTAPI
NtProcessStartup(
    _In_ PBOOT_CONTEXT BootContext)
{
    INT i;
    ULONG oldScreenPosX;
    ULONG oldScreenPosY;
    CHAR Buffer[256];

    BootContext->ExitStatus = ESUCCESS;

    /* Check the validity of the BootContext structure */
    if (!IS_BOOT_CONTEXT_VALID(BootContext))
        return; // Not valid, bail out quickly.

    /* Initialize globals using the BootContext structure */
    MachVtbl = *(BootContext->MachVtbl);
    UiTable = BootContext->UiTable;

    PrintTextColor(0x20 | 0x0F, "\nHello from the 32-bit PE image!\n===============================\n\n");

    MachVideoHideShowTextCursor(FALSE);
    MachVideoSetTextCursorPosition(0, 0);
    //MachVideoGetTextCursorPosition(&i386_ScreenPosX, &i386_ScreenPosY);
    i386_ScreenPosX = i386_ScreenPosY = 0;

    MachVideoClearScreen(0x20 | 0x0F); // Background green, foreground white
    PrintTextColor(0x20 | 0x0F, "\nHello from the 32-bit PE image!\n===============================\n\n");
    PrintTextColor(0x20 | 0x0F, "Image base 0x%p, BootContext 0x%p\n", &__ImageBase, BootContext);

    PrintTextColor(0x20 | 0x0F,
        "BootContext dump:\n"
        "Signature           : 0x%lx '%c%c%c%c'\n"
        "Size                : 0x%lx\n"
        "MemoryTranslation   : 0x%lx\n"
        //"CommandLine         : 0x%p '%s'\n"
        "CommandLine         : 0x%p '%Z'\n"
        "Envp                : 0x%p\n"
        "MachVtbl            : 0x%p\n"
        "UiTable             : 0x%p\n"
        "\n",
        BootContext->Signature,
            ((PCHAR)&BootContext->Signature)[0],
            ((PCHAR)&BootContext->Signature)[1],
            ((PCHAR)&BootContext->Signature)[2],
            ((PCHAR)&BootContext->Signature)[3],
        BootContext->Size,
        BootContext->MemoryTranslation,
        &BootContext->CommandLine, &BootContext->CommandLine,
        BootContext->Envp,
        BootContext->MachVtbl,
        BootContext->UiTable);

    PrintTextColor(0xC0 | 0x0F, "Press any key to restart..."); // Background-intensity bit 0x80 triggers blinking.
    for (;;)
    {
        if (MachConsKbHit())
        {
            if (MachConsGetCh() == KEY_EXTENDED)
                MachConsGetCh();
            break;
        }
        MachHwIdle();
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
        MachHwIdle();
    }

    MachVideoSetTextCursorPosition(i386_ScreenPosX, i386_ScreenPosY);

    UiTable->Initialize();
    UiTable->MessageBox("This is a message box!");
    UiTable->EditBox("This is an editbox!", Buffer, sizeof(Buffer));
    PrintTextColor(0x40 | 0x0F, "You have entered: '%s'", Buffer);

    /* Wait for keypress... */
    for (;;)
    {
        if (MachConsKbHit())
        {
            if (MachConsGetCh() == KEY_EXTENDED)
                MachConsGetCh();
            break;
        }
        MachHwIdle();
    }
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

#ifdef _MSC_VER
#pragma warning(disable:4164)
#pragma function(pow)
#pragma function(log)
#pragma function(log10)
#endif

// Stubs to avoid pulling in data from CRT
double pow(double x, double y)
{
    __debugbreak();
    return 0.0;
}

double log(double x)
{
    __debugbreak();
    return 0.0;
}

double log10(double x)
{
    __debugbreak();
    return 0.0;
}
