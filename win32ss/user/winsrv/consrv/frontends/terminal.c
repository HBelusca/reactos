/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Console Server DLL
 * FILE:            win32ss/user/winsrv/consrv/frontends/terminal.c
 * PURPOSE:         ConSrv terminal.
 * PROGRAMMERS:     Hermes Belusca-Maito (hermes.belusca@sfr.fr)
 */

/* INCLUDES *******************************************************************/

#include <consrv.h>
// #include "concfg/font.h"

// #include "frontends/gui/guiterm.h"
#ifdef TUITERM_COMPILE
#include "frontends/tui/tuiterm.h"
#endif

#define NDEBUG
#include <debug.h>


/* GLOBALS ********************************************************************/

/* PRIVATE FUNCTIONS **********************************************************/


/* CONSRV TERMINAL FRONTENDS INTERFACE ****************************************/

/***************/
#ifdef TUITERM_COMPILE
NTSTATUS NTAPI
TuiLoadFrontEnd(IN OUT PFRONTEND FrontEnd,
                IN OUT PCONSOLE_STATE_INFO ConsoleInfo,
                IN OUT PCONSOLE_INIT_INFO ConsoleInitInfo,
                IN HANDLE ConsoleLeaderProcessHandle);
NTSTATUS NTAPI
TuiUnloadFrontEnd(IN OUT PFRONTEND FrontEnd);
#endif

NTSTATUS NTAPI
GuiLoadFrontEnd(IN OUT PFRONTEND FrontEnd,
                IN OUT PCONSOLE_STATE_INFO ConsoleInfo,
                IN OUT PCONSOLE_INIT_INFO ConsoleInitInfo,
                IN HANDLE ConsoleLeaderProcessHandle);
NTSTATUS NTAPI
GuiUnloadFrontEnd(IN OUT PFRONTEND FrontEnd);
/***************/

typedef
NTSTATUS (NTAPI *FRONTEND_LOAD)(IN OUT PFRONTEND FrontEnd,
                                IN OUT PCONSOLE_STATE_INFO ConsoleInfo,
                                IN OUT PCONSOLE_INIT_INFO ConsoleInitInfo,
                                IN HANDLE ConsoleLeaderProcessHandle);

typedef
NTSTATUS (NTAPI *FRONTEND_UNLOAD)(IN OUT PFRONTEND FrontEnd);

/*
 * If we are not in GUI-mode, start the text-mode terminal emulator.
 * If we fail, try to start the GUI-mode terminal emulator.
 *
 * Try to open the GUI-mode terminal emulator. Two cases are possible:
 * - We are in GUI-mode, therefore GuiMode == TRUE, the previous test-case
 *   failed and we start GUI-mode terminal emulator.
 * - We are in text-mode, therefore GuiMode == FALSE, the previous test-case
 *   succeeded BUT we failed at starting text-mode terminal emulator.
 *   Then GuiMode was switched to TRUE in order to try to open the GUI-mode
 *   terminal emulator (Win32k will automatically switch to graphical mode,
 *   therefore no additional code is needed).
 */

/*
 * NOTE: Each entry of the table should be retrieved when loading a front-end
 *       (examples of the CSR servers which register some data for CSRSS).
 */
static struct
{
    CHAR            FrontEndName[80];
    FRONTEND_LOAD   FrontEndLoad;
    FRONTEND_UNLOAD FrontEndUnload;
} FrontEndLoadingMethods[] =
{
#ifdef TUITERM_COMPILE
    {"TUI", TuiLoadFrontEnd,    TuiUnloadFrontEnd},
#endif
    {"GUI", GuiLoadFrontEnd,    GuiUnloadFrontEnd},

//  {"Not found", 0, NULL}
};

static NTSTATUS
ConSrvLoadFrontEnd(IN OUT PFRONTEND FrontEnd,
                   IN OUT PCONSOLE_STATE_INFO ConsoleInfo,
                   IN OUT PCONSOLE_INIT_INFO ConsoleInitInfo,
                   IN HANDLE ConsoleLeaderProcessHandle)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG i;

    /*
     * Choose an adequate terminal front-end to load, and load it
     */
    for (i = 0; i < ARRAYSIZE(FrontEndLoadingMethods); ++i)
    {
        DPRINT("CONSRV: Trying to load %s frontend...\n",
               FrontEndLoadingMethods[i].FrontEndName);
        Status = FrontEndLoadingMethods[i].FrontEndLoad(FrontEnd,
                                                        ConsoleInfo,
                                                        ConsoleInitInfo,
                                                        ConsoleLeaderProcessHandle);
        if (NT_SUCCESS(Status))
        {
            /* Save the unload callback */
            FrontEnd->UnloadFrontEnd = FrontEndLoadingMethods[i].FrontEndUnload;

            DPRINT("CONSRV: %s frontend loaded successfully\n",
                   FrontEndLoadingMethods[i].FrontEndName);
            break;
        }
        else
        {
            DPRINT1("CONSRV: Loading %s frontend failed, Status = 0x%08lx , continuing...\n",
                    FrontEndLoadingMethods[i].FrontEndName, Status);
        }
    }

    return Status;
}

static NTSTATUS
ConSrvUnloadFrontEnd(IN PFRONTEND FrontEnd)
{
    if (FrontEnd == NULL) return STATUS_INVALID_PARAMETER;
    // return FrontEnd->Vtbl->UnloadFrontEnd(FrontEnd);
    return FrontEnd->UnloadFrontEnd(FrontEnd);
}

// See after...
static TERMINAL_VTBL ConSrvTermVtbl;

NTSTATUS NTAPI
ConSrvInitTerminal(IN OUT PTERMINAL Terminal,
                   IN OUT PCONSOLE_STATE_INFO ConsoleInfo,
                   IN OUT PCONSOLE_INIT_INFO ConsoleInitInfo,
                   IN HANDLE ConsoleLeaderProcessHandle)
{
    NTSTATUS Status;
    PFRONTEND FrontEnd;

    /* Load a suitable frontend for the ConSrv terminal */
    FrontEnd = ConsoleAllocHeap(HEAP_ZERO_MEMORY, sizeof(*FrontEnd));
    if (!FrontEnd) return STATUS_NO_MEMORY;

    Status = ConSrvLoadFrontEnd(FrontEnd,
                                ConsoleInfo,
                                ConsoleInitInfo,
                                ConsoleLeaderProcessHandle);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("CONSRV: Failed to initialize a frontend, Status = 0x%08lx\n", Status);
        ConsoleFreeHeap(FrontEnd);
        return Status;
    }
    DPRINT("CONSRV: Frontend initialized\n");

    /* Initialize the ConSrv terminal */
    Terminal->Vtbl = &ConSrvTermVtbl;
    // Terminal->Console will be initialized by ConDrvAttachTerminal
    Terminal->Context = FrontEnd; /* We store the frontend pointer in the terminal private context */

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
ConSrvDeinitTerminal(IN OUT PTERMINAL Terminal)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PFRONTEND FrontEnd = Terminal->Context;

    /* Reset the ConSrv terminal */
    Terminal->Context = NULL;
    Terminal->Vtbl = NULL;

    /* Unload the frontend */
    if (FrontEnd != NULL)
    {
        Status = ConSrvUnloadFrontEnd(FrontEnd);
        ConsoleFreeHeap(FrontEnd);
    }

    return Status;
}


/* CONSRV TERMINAL INTERFACE **************************************************/

static NTSTATUS NTAPI
ConSrvTermInitTerminal(IN OUT PTERMINAL This,
                       IN PCONSOLE Console)
{
    NTSTATUS Status;
    PFRONTEND FrontEnd = This->Context;
    PCONSRV_CONSOLE ConSrvConsole = (PCONSRV_CONSOLE)Console;

    /* Initialize the console pointer for our frontend */
    FrontEnd->Console = ConSrvConsole;

    /** HACK HACK!! Copy FrontEnd into the console!! **/
    DPRINT("Using FrontEndIFace HACK(1), should be removed after proper implementation!\n");
    ConSrvConsole->FrontEndIFace = *FrontEnd;

    Status = FrontEnd->Vtbl->InitFrontEnd(FrontEnd, ConSrvConsole);
    if (!NT_SUCCESS(Status))
        DPRINT1("InitFrontEnd failed, Status = 0x%08lx\n", Status);

    /** HACK HACK!! Be sure FrontEndIFace is correctly updated in the console!! **/
    DPRINT("Using FrontEndIFace HACK(2), should be removed after proper implementation!\n");
    ConSrvConsole->FrontEndIFace = *FrontEnd;

    return Status;
}

static VOID NTAPI
ConSrvTermDeinitTerminal(IN OUT PTERMINAL This)
{
    PFRONTEND FrontEnd = This->Context;
    FrontEnd->Vtbl->DeinitFrontEnd(FrontEnd);
}


VOID
ConioDrawConsole(PCONSRV_CONSOLE Console)
{
    SMALL_RECT Region;
    PCONSOLE_SCREEN_BUFFER ActiveBuffer = Console->ActiveBuffer;

    if (!ActiveBuffer) return;

    ConioInitRect(&Region, 0, 0,
                  ActiveBuffer->ViewSize.Y - 1,
                  ActiveBuffer->ViewSize.X - 1);
    TermDrawRegion(Console, &Region);
    // Console->FrontEndIFace.Vtbl->DrawRegion(&Console->FrontEndIFace, &Region);
}

static VOID NTAPI
ConSrvTermDrawRegion(IN OUT PTERMINAL This,
                SMALL_RECT* Region)
{
    PFRONTEND FrontEnd = This->Context;
    FrontEnd->Vtbl->DrawRegion(FrontEnd, Region);
}

static BOOL NTAPI
ConSrvTermSetCursorInfo(IN OUT PTERMINAL This,
                   PCONSOLE_SCREEN_BUFFER ScreenBuffer)
{
    PFRONTEND FrontEnd = This->Context;
    return FrontEnd->Vtbl->SetCursorInfo(FrontEnd, ScreenBuffer);
}

static BOOL NTAPI
ConSrvTermSetScreenInfo(IN OUT PTERMINAL This,
                   PCONSOLE_SCREEN_BUFFER ScreenBuffer,
                   SHORT OldCursorX,
                   SHORT OldCursorY)
{
    PFRONTEND FrontEnd = This->Context;
    return FrontEnd->Vtbl->SetScreenInfo(FrontEnd,
                                         ScreenBuffer,
                                         OldCursorX,
                                         OldCursorY);
}

static VOID NTAPI
ConSrvTermResizeTerminal(IN OUT PTERMINAL This)
{
    PFRONTEND FrontEnd = This->Context;
    FrontEnd->Vtbl->ResizeTerminal(FrontEnd);
}

static VOID NTAPI
ConSrvTermSetActiveScreenBuffer(IN OUT PTERMINAL This)
{
    PFRONTEND FrontEnd = This->Context;
    FrontEnd->Vtbl->SetActiveScreenBuffer(FrontEnd);
}

static VOID NTAPI
ConSrvTermReleaseScreenBuffer(IN OUT PTERMINAL This,
                         IN PCONSOLE_SCREEN_BUFFER ScreenBuffer)
{
    PFRONTEND FrontEnd = This->Context;
    FrontEnd->Vtbl->ReleaseScreenBuffer(FrontEnd, ScreenBuffer);
}

static VOID NTAPI
ConSrvTermGetLargestConsoleWindowSize(IN OUT PTERMINAL This,
                                 PCOORD pSize)
{
    PFRONTEND FrontEnd = This->Context;
    FrontEnd->Vtbl->GetLargestConsoleWindowSize(FrontEnd, pSize);
}

static BOOL NTAPI
ConSrvTermSetPalette(IN OUT PTERMINAL This,
                HPALETTE PaletteHandle,
                UINT PaletteUsage)
{
    PFRONTEND FrontEnd = This->Context;
    return FrontEnd->Vtbl->SetPalette(FrontEnd, PaletteHandle, PaletteUsage);
}

static INT NTAPI
ConSrvTermShowMouseCursor(IN OUT PTERMINAL This,
                     BOOL Show)
{
    PFRONTEND FrontEnd = This->Context;
    return FrontEnd->Vtbl->ShowMouseCursor(FrontEnd, Show);
}

static TERMINAL_VTBL ConSrvTermVtbl =
{
    ConSrvTermInitTerminal,
    ConSrvTermDeinitTerminal,

    // ConSrvTermReadStream,
    // ConSrvTermWriteStream,

    ConSrvTermDrawRegion,
    ConSrvTermSetCursorInfo,
    ConSrvTermSetScreenInfo,
    ConSrvTermResizeTerminal,
    ConSrvTermSetActiveScreenBuffer,
    ConSrvTermReleaseScreenBuffer,
    ConSrvTermGetLargestConsoleWindowSize,
    ConSrvTermSetPalette,
    ConSrvTermShowMouseCursor,
};

#if 0
VOID
ResetFrontEnd(IN PCONSOLE Console)
{
    PCONSRV_CONSOLE ConSrvConsole = (PCONSRV_CONSOLE)Console;
    if (!Console) return;

    /* Reinitialize the frontend interface */
    RtlZeroMemory(&ConSrvConsole->FrontEndIFace, sizeof(ConSrvConsole->FrontEndIFace));
    ConSrvConsole->FrontEndIFace.Vtbl = &ConSrvTermVtbl;
}
#endif

/* EOF */
