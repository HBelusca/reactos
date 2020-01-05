/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Console Server DLL
 * FILE:            win32ss/user/winsrv/consrv/frontends/gui/guiterm.c
 * PURPOSE:         GUI Terminal Front-End
 * PROGRAMMERS:     Gé van Geldorp
 *                  Johannes Anderwald
 *                  Jeffrey Morlan
 *                  Hermes Belusca-Maito (hermes.belusca@sfr.fr)
 */

/* INCLUDES *******************************************************************/

#include <consrv.h>

#define NDEBUG
#include <debug.h>

#include "concfg/font.h"
#include "guiterm.h"
#include "resource.h"

// HACK!! Remove it when the hack in GuiWriteStream is fixed
#define CONGUI_UPDATE_TIME    0
#define CONGUI_UPDATE_TIMER   1

#define PM_CREATE_CONSOLE     (WM_APP + 1)
#define PM_DESTROY_CONSOLE    (WM_APP + 2)


/* GLOBALS ********************************************************************/

typedef struct _GUI_INIT_INFO
{
    CONSOLE_INPUT_THREAD_INFO ThreadInfo;
    HICON hIcon;
    HICON hIconSm;
    BOOLEAN IsWindowVisible;
    GUI_CONSOLE_INFO TermInfo;
} GUI_INIT_INFO, *PGUI_INIT_INFO;

static BOOL ConsInitialized = FALSE;

extern HICON   ghDefaultIcon;
extern HICON   ghDefaultIconSm;
extern HCURSOR ghDefaultCursor;

VOID
SetConWndConsoleLeaderCID(IN PGUI_CONSOLE_DATA GuiData);
BOOLEAN
RegisterConWndClass(IN HINSTANCE hInstance);
BOOLEAN
UnRegisterConWndClass(HINSTANCE hInstance);

/* FUNCTIONS ******************************************************************/

VOID
GuiConsoleMoveWindow(PGUI_CONSOLE_DATA GuiData)
{
    /* Move the window if needed (not positioned by the system) */
    if (!GuiData->GuiInfo.AutoPosition)
    {
        SetWindowPos(GuiData->hWindow,
                     NULL,
                     GuiData->GuiInfo.WindowOrigin.x,
                     GuiData->GuiInfo.WindowOrigin.y,
                     0, 0,
                     SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

static VOID
DrawRegion(PGUI_CONSOLE_DATA GuiData,
           SMALL_RECT* Region)
{
    RECT RegionRect;

    SmallRectToRect(GuiData, &RegionRect, Region);
    /* Do not erase the background: it speeds up redrawing and reduce flickering */
    InvalidateRect(GuiData->hWindow, &RegionRect, FALSE);
    /**UpdateWindow(GuiData->hWindow);**/
}

VOID
InvalidateCell(PGUI_CONSOLE_DATA GuiData,
               SHORT x, SHORT y)
{
    SMALL_RECT CellRect = { x, y, x, y };
    DrawRegion(GuiData, &CellRect);
}


/******************************************************************************
 *                        GUI Terminal Initialization                         *
 ******************************************************************************/

// FIXME: HACK: Potential HACK for CORE-8129; see revision 63595.
VOID
CreateSysMenu(HWND hWnd);

static ULONG NTAPI
GuiConsoleInputThread(PVOID Param)
{
    LONG WindowCount = 0;
    MSG Msg;

    while (GetMessageW(&Msg, NULL, 0, 0))
    {
        switch (Msg.message)
        {
            case PM_CREATE_CONSOLE:
            {
                PGUI_CONSOLE_DATA GuiData = (PGUI_CONSOLE_DATA)Msg.lParam;
                PCONSRV_CONSOLE Console = GuiData->Console;
                HWND NewWindow;
                RECT rcWnd;

                DPRINT("PM_CREATE_CONSOLE -- creating window\n");

                NewWindow = CreateWindowExW(WS_EX_CLIENTEDGE,
                                            GUI_CONWND_CLASS,
                                            Console->Title.Buffer,
                                            WS_OVERLAPPEDWINDOW,
                                            CW_USEDEFAULT,
                                            CW_USEDEFAULT,
                                            CW_USEDEFAULT,
                                            CW_USEDEFAULT,
                                            GuiData->IsWindowVisible ? HWND_DESKTOP : HWND_MESSAGE,
                                            NULL,
                                            ConSrvDllInstance,
                                            (PVOID)GuiData);
                if (NewWindow == NULL)
                {
                    DPRINT1("Failed to create a new console window\n");
                    continue;
                }

                ASSERT(NewWindow == GuiData->hWindow);

                _InterlockedIncrement(&WindowCount);

                //
                // FIXME: TODO: Move everything there into conwnd.c!OnNcCreate()
                //

                /* Retrieve our real position */
                // See conwnd.c!OnMove()
                GetWindowRect(GuiData->hWindow, &rcWnd);
                GuiData->GuiInfo.WindowOrigin.x = rcWnd.left;
                GuiData->GuiInfo.WindowOrigin.y = rcWnd.top;

                if (GuiData->IsWindowVisible)
                {
                    /* Move and resize the window to the user's values */
                    /* CAN WE DEADLOCK ?? */
                    GuiConsoleMoveWindow(GuiData); // FIXME: This MUST be done via the CreateWindowExW call.
                    SendMessageW(GuiData->hWindow, PM_RESIZE_TERMINAL, 0, 0);
                }

                // FIXME: HACK: Potential HACK for CORE-8129; see revision 63595.
                CreateSysMenu(GuiData->hWindow);

                if (GuiData->IsWindowVisible)
                {
                    /* Switch to full-screen mode if necessary */
                    // FIXME: Move elsewhere, it cause misdrawings of the window.
                    if (GuiData->GuiInfo.FullScreen) SwitchFullScreen(GuiData, TRUE);

                    DPRINT("PM_CREATE_CONSOLE -- showing window\n");
                    ShowWindowAsync(NewWindow, (int)GuiData->GuiInfo.ShowWindow);
                }
                else
                {
                    DPRINT("PM_CREATE_CONSOLE -- hidden window\n");
                    ShowWindowAsync(NewWindow, SW_HIDE);
                }

                continue;
            }

            case PM_DESTROY_CONSOLE:
            {
                PGUI_CONSOLE_DATA GuiData = (PGUI_CONSOLE_DATA)Msg.lParam;
                MSG TempMsg;

                DPRINT("PM_DESTROY_CONSOLE -- destroying window\n");

                /* Exit the full screen mode if it was already set */
                // LeaveFullScreen(GuiData);

                /*
                 * Window creation is done using a PostMessage(), so it's possible
                 * that the window that we want to destroy doesn't exist yet.
                 * So first empty the message queue.
                 */
                while (PeekMessageW(&TempMsg, NULL, 0, 0, PM_REMOVE))
                {
                    DispatchMessageW(&TempMsg);
                }

                if (GuiData->hWindow == NULL) continue;

                DestroyWindow(GuiData->hWindow);

                NtSetEvent(GuiData->hGuiTermEvent, NULL);

                if (_InterlockedDecrement(&WindowCount) == 0)
                {
                    DPRINT("CONSRV: Going to quit the Input Thread 0x%p\n",
                           NtCurrentTeb()->ClientId.UniqueThread);
                    goto Quit;
                }

                continue;
            }
        }

        TranslateMessage(&Msg);
        DispatchMessageW(&Msg);
    }

Quit:
    return 0;
}

// FIXME: Maybe return a NTSTATUS
static BOOL
GuiInit(IN PCONSOLE_INIT_INFO ConsoleInitInfo,
        IN HANDLE ConsoleLeaderProcessHandle,
        IN OUT PGUI_INIT_INFO GuiInitInfo)
{
    NTSTATUS Status;
    UNICODE_STRING DesktopPath;
    CONSOLE_INPUT_THREAD_INFO ThreadInfo;

    /* Perform one-time initialization */
    if (!ConsInitialized)
    {
        /* Initialize and register the console window class */
        if (!RegisterConWndClass(ConSrvDllInstance)) return FALSE;

        /* Initialize the font support -- additional TrueType fonts cache */
        InitTTFontCache();

        ConsInitialized = TRUE;
    }

    DesktopPath.Buffer = ConsoleInitInfo->Desktop;
    DesktopPath.MaximumLength = ConsoleInitInfo->DesktopLength;
    DesktopPath.Length = DesktopPath.MaximumLength - sizeof(UNICODE_NULL);
    Status = StartConsoleInputThread(ConsoleLeaderProcessHandle,
                                     &DesktopPath,
                                     TRUE,
                                     GuiConsoleInputThread,
                                     NULL,
                                     &ThreadInfo);
    if (!NT_SUCCESS(Status))
        return FALSE;

    /*
     * Save the opened window station and desktop handles in the initialization
     * structure. They will be used later on, and released, by the GUI frontend.
     */
    /*
     * Save the input thread ID for later use, and restore the original handles.
     * The copies are held by the console input thread.
     */
    GuiInitInfo->ThreadInfo = ThreadInfo;

    return TRUE;
}


/******************************************************************************
 *                             GUI Console Driver                             *
 ******************************************************************************/

static VOID NTAPI
GuiDeinitFrontEnd(IN OUT PFRONTEND This);

static NTSTATUS NTAPI
GuiInitFrontEnd(IN OUT PFRONTEND This,
                IN PCONSRV_CONSOLE Console)
{
    PGUI_INIT_INFO GuiInitInfo;
    PGUI_CONSOLE_DATA GuiData;

    if (This == NULL || Console == NULL || This->Context2 == NULL)
        return STATUS_INVALID_PARAMETER;

    ASSERT(This->Console == Console);

    GuiInitInfo = This->Context2;

    /* Terminal data allocation */
    GuiData = ConsoleAllocHeap(HEAP_ZERO_MEMORY, sizeof(*GuiData));
    if (!GuiData)
    {
        DPRINT1("CONSRV: Failed to create GUI_CONSOLE_DATA\n");
        return STATUS_NO_MEMORY;
    }
    /// /* HACK */ Console->FrontEndIFace.Context = (PVOID)GuiData; /* HACK */
    GuiData->Console      = Console;
    GuiData->ActiveBuffer = Console->ActiveBuffer;
    GuiData->hWindow = NULL;
    GuiData->IsWindowVisible = GuiInitInfo->IsWindowVisible;

    /* The console can be resized */
    Console->FixedSize = FALSE;

    InitializeCriticalSection(&GuiData->Lock);

    /*
     * Set up GUI data
     */
    RtlCopyMemory(&GuiData->GuiInfo, &GuiInitInfo->TermInfo, sizeof(GuiInitInfo->TermInfo));

    /* Initialize the icon handles */
    if (GuiInitInfo->hIcon != NULL)
        GuiData->hIcon = GuiInitInfo->hIcon;
    else
        GuiData->hIcon = ghDefaultIcon;

    if (GuiInitInfo->hIconSm != NULL)
        GuiData->hIconSm = GuiInitInfo->hIconSm;
    else
        GuiData->hIconSm = ghDefaultIconSm;

    ASSERT(GuiData->hIcon && GuiData->hIconSm);

    /* Mouse is shown by default with its default cursor shape */
    GuiData->hCursor = ghDefaultCursor;
    GuiData->MouseCursorRefCount = 0;

    /* A priori don't ignore mouse signals */
    GuiData->IgnoreNextMouseSignal = FALSE;
    /* Initialize HACK FOR CORE-8394. See conwnd.c!OnMouse for more details. */
    GuiData->HackCORE8394IgnoreNextMove = FALSE;

    /* Close button and the corresponding system menu item are enabled by default */
    GuiData->IsCloseButtonEnabled = TRUE;

    /* There is no user-reserved menu id range by default */
    GuiData->CmdIdLow = GuiData->CmdIdHigh = 0;

    /* Initialize the selection */
    RtlZeroMemory(&GuiData->Selection, sizeof(GuiData->Selection));
    GuiData->Selection.dwFlags = CONSOLE_NO_SELECTION;
    RtlZeroMemory(&GuiData->dwSelectionCursor, sizeof(GuiData->dwSelectionCursor));
    GuiData->LineSelection = FALSE; // Default to block selection
    // TODO: Retrieve the selection mode via the registry.

    GuiData->ThreadInfo = GuiInitInfo->ThreadInfo;

    /* Finally, finish to initialize the frontend structure */
    This->Context  = GuiData;
    ConsoleFreeHeap(This->Context2);
    This->Context2 = NULL;

    /*
     * We need to wait until the GUI has been fully initialized
     * to retrieve custom settings i.e. WindowSize etc...
     * Ideally we could use SendNotifyMessage for this but its not
     * yet implemented.
     */
    NtCreateEvent(&GuiData->hGuiInitEvent, EVENT_ALL_ACCESS,
                  NULL, SynchronizationEvent, FALSE);
    NtCreateEvent(&GuiData->hGuiTermEvent, EVENT_ALL_ACCESS,
                  NULL, SynchronizationEvent, FALSE);

    DPRINT("GUI - Checkpoint\n");

    /* Create the terminal window */
    PostThreadMessageW(GuiData->ThreadInfo.ThreadId, PM_CREATE_CONSOLE, 0, (LPARAM)GuiData);

    /* Wait until initialization has finished */
    NtWaitForSingleObject(GuiData->hGuiInitEvent, FALSE, NULL);
    DPRINT("OK we created the console window\n");
    NtClose(GuiData->hGuiInitEvent);
    GuiData->hGuiInitEvent = NULL;

    /* Check whether we really succeeded in initializing the terminal window */
    if (GuiData->hWindow == NULL)
    {
        DPRINT("GuiInitConsole - We failed at creating a new terminal window\n");
        GuiDeinitFrontEnd(This);
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

static VOID NTAPI
GuiDeinitFrontEnd(IN OUT PFRONTEND This)
{
    PGUI_CONSOLE_DATA GuiData = This->Context;

    DPRINT("Send PM_DESTROY_CONSOLE message and wait on hGuiTermEvent...\n");
    PostThreadMessageW(GuiData->ThreadInfo.ThreadId, PM_DESTROY_CONSOLE, 0, (LPARAM)GuiData);
    NtWaitForSingleObject(GuiData->hGuiTermEvent, FALSE, NULL);
    DPRINT("hGuiTermEvent set\n");
    NtClose(GuiData->hGuiTermEvent);
    GuiData->hGuiTermEvent = NULL;

    CloseDesktop(GuiData->ThreadInfo.Desktop); // NtUserCloseDesktop
    CloseWindowStation(GuiData->ThreadInfo.WinSta); // NtUserCloseWindowStation

    DPRINT("Destroying icons !! - GuiData->hIcon = 0x%p ; ghDefaultIcon = 0x%p ; GuiData->hIconSm = 0x%p ; ghDefaultIconSm = 0x%p\n",
            GuiData->hIcon, ghDefaultIcon, GuiData->hIconSm, ghDefaultIconSm);
    if (GuiData->hIcon != NULL && GuiData->hIcon != ghDefaultIcon)
    {
        DPRINT("Destroy hIcon\n");
        DestroyIcon(GuiData->hIcon);
    }
    if (GuiData->hIconSm != NULL && GuiData->hIconSm != ghDefaultIconSm)
    {
        DPRINT("Destroy hIconSm\n");
        DestroyIcon(GuiData->hIconSm);
    }

    This->Context = NULL;
    DeleteCriticalSection(&GuiData->Lock);
    ConsoleFreeHeap(GuiData);

    DPRINT("Quit GuiDeinitFrontEnd\n");
}

static VOID NTAPI
GuiDrawRegion(IN OUT PFRONTEND This,
              SMALL_RECT* Region)
{
    PGUI_CONSOLE_DATA GuiData = This->Context;

    /* Do nothing if the window is hidden */
    if (!GuiData->IsWindowVisible) return;

    DrawRegion(GuiData, Region);
}

static VOID NTAPI
GuiWriteStream(IN OUT PFRONTEND This,
               SMALL_RECT* Region,
               SHORT CursorStartX,
               SHORT CursorStartY,
               UINT ScrolledLines,
               PWCHAR Buffer,
               UINT Length)
{
    PGUI_CONSOLE_DATA GuiData = This->Context;
    PCONSOLE_SCREEN_BUFFER Buff;
    SHORT CursorEndX, CursorEndY;
    RECT ScrollRect;

    if (NULL == GuiData || NULL == GuiData->hWindow) return;

    /* Do nothing if the window is hidden */
    if (!GuiData->IsWindowVisible) return;

    Buff = GuiData->ActiveBuffer;
    if (GetType(Buff) != TEXTMODE_BUFFER) return;

    if (0 != ScrolledLines)
    {
        ScrollRect.left = 0;
        ScrollRect.top = 0;
        ScrollRect.right = Buff->ViewSize.X * GuiData->CharWidth;
        ScrollRect.bottom = Region->Top * GuiData->CharHeight;

        ScrollWindowEx(GuiData->hWindow,
                       0,
                       -(int)(ScrolledLines * GuiData->CharHeight),
                       &ScrollRect,
                       NULL,
                       NULL,
                       NULL,
                       SW_INVALIDATE);
    }

    DrawRegion(GuiData, Region);

    if (CursorStartX < Region->Left || Region->Right < CursorStartX
            || CursorStartY < Region->Top || Region->Bottom < CursorStartY)
    {
        InvalidateCell(GuiData, CursorStartX, CursorStartY);
    }

    CursorEndX = Buff->CursorPosition.X;
    CursorEndY = Buff->CursorPosition.Y;
    if ((CursorEndX < Region->Left || Region->Right < CursorEndX
            || CursorEndY < Region->Top || Region->Bottom < CursorEndY)
            && (CursorEndX != CursorStartX || CursorEndY != CursorStartY))
    {
        InvalidateCell(GuiData, CursorEndX, CursorEndY);
    }

    // HACK!!
    // Set up the update timer (very short interval) - this is a "hack" for getting the OS to
    // repaint the window without having it just freeze up and stay on the screen permanently.
    Buff->CursorBlinkOn = TRUE;
    SetTimer(GuiData->hWindow, CONGUI_UPDATE_TIMER, CONGUI_UPDATE_TIME, NULL);
}

/* static */ VOID NTAPI
GuiRingBell(IN OUT PFRONTEND This)
{
    PGUI_CONSOLE_DATA GuiData = This->Context;

    /* Emit an error beep sound */
    SendNotifyMessage(GuiData->hWindow, PM_CONSOLE_BEEP, 0, 0);
}

static BOOL NTAPI
GuiSetCursorInfo(IN OUT PFRONTEND This,
                 PCONSOLE_SCREEN_BUFFER Buff)
{
    PGUI_CONSOLE_DATA GuiData = This->Context;

    /* Do nothing if the window is hidden */
    if (!GuiData->IsWindowVisible) return TRUE;

    if (GuiData->ActiveBuffer == Buff)
    {
        InvalidateCell(GuiData, Buff->CursorPosition.X, Buff->CursorPosition.Y);
    }

    return TRUE;
}

static BOOL NTAPI
GuiSetScreenInfo(IN OUT PFRONTEND This,
                 PCONSOLE_SCREEN_BUFFER Buff,
                 SHORT OldCursorX,
                 SHORT OldCursorY)
{
    PGUI_CONSOLE_DATA GuiData = This->Context;

    /* Do nothing if the window is hidden */
    if (!GuiData->IsWindowVisible) return TRUE;

    if (GuiData->ActiveBuffer == Buff)
    {
        /* Redraw char at old position (remove cursor) */
        InvalidateCell(GuiData, OldCursorX, OldCursorY);
        /* Redraw char at new position (show cursor) */
        InvalidateCell(GuiData, Buff->CursorPosition.X, Buff->CursorPosition.Y);
    }

    return TRUE;
}

static VOID NTAPI
GuiResizeTerminal(IN OUT PFRONTEND This)
{
    PGUI_CONSOLE_DATA GuiData = This->Context;

    /* Resize the window to the user's values */
    PostMessageW(GuiData->hWindow, PM_RESIZE_TERMINAL, 0, 0);
}

static VOID NTAPI
GuiSetActiveScreenBuffer(IN OUT PFRONTEND This)
{
    PGUI_CONSOLE_DATA GuiData = This->Context;
    PCONSOLE_SCREEN_BUFFER ActiveBuffer;
    HPALETTE hPalette;

    EnterCriticalSection(&GuiData->Lock);
    GuiData->WindowSizeLock = TRUE;

    InterlockedExchangePointer((PVOID*)&GuiData->ActiveBuffer,
                               ConDrvGetActiveScreenBuffer((PCONSOLE)GuiData->Console));

    GuiData->WindowSizeLock = FALSE;
    LeaveCriticalSection(&GuiData->Lock);

    ActiveBuffer = GuiData->ActiveBuffer;

    /* Change the current palette */
    if (ActiveBuffer->PaletteHandle == NULL)
        hPalette = GuiData->hSysPalette;
    else
        hPalette = ActiveBuffer->PaletteHandle;

    DPRINT("GuiSetActiveScreenBuffer using palette 0x%p\n", hPalette);

    /* Set the new palette for the framebuffer */
    SelectPalette(GuiData->hMemDC, hPalette, FALSE);

    /* Specify the use of the system palette for the framebuffer */
    SetSystemPaletteUse(GuiData->hMemDC, ActiveBuffer->PaletteUsage);

    /* Realize the (logical) palette */
    RealizePalette(GuiData->hMemDC);

    GuiResizeTerminal(This);
    // ConioDrawConsole(Console);
}

static VOID NTAPI
GuiReleaseScreenBuffer(IN OUT PFRONTEND This,
                       IN PCONSOLE_SCREEN_BUFFER ScreenBuffer)
{
    PGUI_CONSOLE_DATA GuiData = This->Context;

    /*
     * If we were notified to release a screen buffer that is not actually
     * ours, then just ignore the notification...
     */
    if (ScreenBuffer != GuiData->ActiveBuffer) return;

    /*
     * ... else, we must release our active buffer. Two cases are present:
     * - If ScreenBuffer (== GuiData->ActiveBuffer) IS NOT the console
     *   active screen buffer, then we can safely switch to it.
     * - If ScreenBuffer IS the console active screen buffer, we must release
     *   it ONLY.
     */

    /* Release the old active palette and set the default one */
    if (GetCurrentObject(GuiData->hMemDC, OBJ_PAL) == ScreenBuffer->PaletteHandle)
    {
        /* Set the new palette */
        SelectPalette(GuiData->hMemDC, GuiData->hSysPalette, FALSE);
    }

    /* Set the adequate active screen buffer */
    if (ScreenBuffer != GuiData->Console->ActiveBuffer)
    {
        GuiSetActiveScreenBuffer(This);
    }
    else
    {
        EnterCriticalSection(&GuiData->Lock);
        GuiData->WindowSizeLock = TRUE;

        InterlockedExchangePointer((PVOID*)&GuiData->ActiveBuffer, NULL);

        GuiData->WindowSizeLock = FALSE;
        LeaveCriticalSection(&GuiData->Lock);
    }
}

static BOOL NTAPI
GuiSetMouseCursor(IN OUT PFRONTEND This,
                  HCURSOR CursorHandle);

static VOID NTAPI
GuiRefreshInternalInfo(IN OUT PFRONTEND This)
{
    PGUI_CONSOLE_DATA GuiData = This->Context;

    /* Update the console leader information held by the window */
    SetConWndConsoleLeaderCID(GuiData);

    /*
     * HACK:
     * We reset the cursor here so that, when a console app quits, we reset
     * the cursor to the default one. It's quite a hack since it doesn't proceed
     * per - console process... This must be fixed.
     *
     * See GuiInitConsole(...) for more information.
     */

    /* Mouse is shown by default with its default cursor shape */
    GuiData->MouseCursorRefCount = 0; // Reinitialize the reference counter
    GuiSetMouseCursor(This, NULL);
}

static VOID NTAPI
GuiChangeTitle(IN OUT PFRONTEND This)
{
    PGUI_CONSOLE_DATA GuiData = This->Context;
    PostMessageW(GuiData->hWindow, PM_CONSOLE_SET_TITLE, 0, 0);
}

static BOOL NTAPI
GuiChangeIcon(IN OUT PFRONTEND This,
              HICON IconHandle)
{
    PGUI_CONSOLE_DATA GuiData = This->Context;
    HICON hIcon, hIconSm;

    if (IconHandle == NULL)
    {
        hIcon   = ghDefaultIcon;
        hIconSm = ghDefaultIconSm;
    }
    else
    {
        hIcon   = CopyIcon(IconHandle);
        hIconSm = CopyIcon(IconHandle);
    }

    if (hIcon == NULL)
        return FALSE;

    if (hIcon != GuiData->hIcon)
    {
        if (GuiData->hIcon != NULL && GuiData->hIcon != ghDefaultIcon)
        {
            DestroyIcon(GuiData->hIcon);
        }
        if (GuiData->hIconSm != NULL && GuiData->hIconSm != ghDefaultIconSm)
        {
            DestroyIcon(GuiData->hIconSm);
        }

        GuiData->hIcon   = hIcon;
        GuiData->hIconSm = hIconSm;

        DPRINT("Set icons in GuiChangeIcon\n");
        PostMessageW(GuiData->hWindow, WM_SETICON, ICON_BIG  , (LPARAM)GuiData->hIcon  );
        PostMessageW(GuiData->hWindow, WM_SETICON, ICON_SMALL, (LPARAM)GuiData->hIconSm);
    }

    return TRUE;
}

static HDESK NTAPI
GuiGetThreadConsoleDesktop(IN OUT PFRONTEND This)
{
    PGUI_CONSOLE_DATA GuiData = This->Context;
    return GuiData->ThreadInfo.Desktop;
}

static HWND NTAPI
GuiGetConsoleWindowHandle(IN OUT PFRONTEND This)
{
    PGUI_CONSOLE_DATA GuiData = This->Context;
    return GuiData->hWindow;
}

static VOID NTAPI
GuiGetLargestConsoleWindowSize(IN OUT PFRONTEND This,
                               PCOORD pSize)
{
    PGUI_CONSOLE_DATA GuiData = This->Context;
    PCONSOLE_SCREEN_BUFFER ActiveBuffer;
    HMONITOR hMonitor;
    MONITORINFO MonitorInfo;
    LONG Width, Height;
    UINT WidthUnit, HeightUnit;

    if (!pSize) return;

    /*
     * Retrieve the monitor that is mostly covered by the current console window;
     * default to primary monitor otherwise.
     */
    MonitorInfo.cbSize = sizeof(MonitorInfo);
    hMonitor = MonitorFromWindow(GuiData->hWindow, MONITOR_DEFAULTTOPRIMARY);
    if (hMonitor && GetMonitorInfoW(hMonitor, &MonitorInfo))
    {
        /* Retrieve the width and height of the client area of this monitor */
        Width  = MonitorInfo.rcWork.right - MonitorInfo.rcWork.left;
        Height = MonitorInfo.rcWork.bottom - MonitorInfo.rcWork.top;
    }
    else
    {
        /*
         * Retrieve the width and height of the client area for a full-screen
         * window on the primary display monitor.
         */
        Width  = GetSystemMetrics(SM_CXFULLSCREEN);
        Height = GetSystemMetrics(SM_CYFULLSCREEN);

        // RECT WorkArea;
        // SystemParametersInfoW(SPI_GETWORKAREA, 0, &WorkArea, 0);
        // Width  = WorkArea.right;
        // Height = WorkArea.bottom;
    }

    ActiveBuffer = GuiData->ActiveBuffer;
#if 0
    // NOTE: This would be surprising if we wouldn't have an associated buffer...
    if (ActiveBuffer)
#endif
        GetScreenBufferSizeUnits(ActiveBuffer, GuiData, &WidthUnit, &HeightUnit);
#if 0
    else
        /* Default: graphics mode */
        WidthUnit = HeightUnit = 1;
#endif

    Width  -= (2 * (GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXEDGE)));
    Height -= (2 * (GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CYEDGE)) + GetSystemMetrics(SM_CYCAPTION));

    if (Width  < 0) Width  = 0;
    if (Height < 0) Height = 0;

    pSize->X = (SHORT)(Width  / (int)WidthUnit ) /* HACK */ + 2;
    pSize->Y = (SHORT)(Height / (int)HeightUnit) /* HACK */ + 1;
}

static BOOL NTAPI
GuiGetSelectionInfo(IN OUT PFRONTEND This,
                    PCONSOLE_SELECTION_INFO pSelectionInfo)
{
    PGUI_CONSOLE_DATA GuiData = This->Context;

    if (pSelectionInfo == NULL) return FALSE;

    ZeroMemory(pSelectionInfo, sizeof(*pSelectionInfo));
    if (GuiData->Selection.dwFlags != CONSOLE_NO_SELECTION)
        RtlCopyMemory(pSelectionInfo, &GuiData->Selection, sizeof(*pSelectionInfo));

    return TRUE;
}

static BOOL NTAPI
GuiSetPalette(IN OUT PFRONTEND This,
              HPALETTE PaletteHandle,
              UINT PaletteUsage)
{
    PGUI_CONSOLE_DATA GuiData = This->Context;
    HPALETTE OldPalette;

    // if (GetType(GuiData->ActiveBuffer) != GRAPHICS_BUFFER) return FALSE;
    if (PaletteHandle == NULL) return FALSE;

    /* Set the new palette for the framebuffer */
    OldPalette = SelectPalette(GuiData->hMemDC, PaletteHandle, FALSE);
    if (OldPalette == NULL) return FALSE;

    /* Specify the use of the system palette for the framebuffer */
    SetSystemPaletteUse(GuiData->hMemDC, PaletteUsage);

    /* Realize the (logical) palette */
    RealizePalette(GuiData->hMemDC);

    /* Save the original system palette handle */
    if (GuiData->hSysPalette == NULL) GuiData->hSysPalette = OldPalette;

    return TRUE;
}

static ULONG NTAPI
GuiGetDisplayMode(IN OUT PFRONTEND This)
{
    PGUI_CONSOLE_DATA GuiData = This->Context;
    ULONG DisplayMode = 0;

    if (GuiData->GuiInfo.FullScreen)
        DisplayMode |= CONSOLE_FULLSCREEN_HARDWARE; // CONSOLE_FULLSCREEN
    else
        DisplayMode |= CONSOLE_WINDOWED;

    return DisplayMode;
}

static BOOL NTAPI
GuiSetDisplayMode(IN OUT PFRONTEND This,
                  ULONG NewMode)
{
    PGUI_CONSOLE_DATA GuiData = This->Context;
    BOOL FullScreen;

    if (NewMode & ~(CONSOLE_FULLSCREEN_MODE | CONSOLE_WINDOWED_MODE))
        return FALSE;

    /* Do nothing if the window is hidden */
    if (!GuiData->IsWindowVisible) return TRUE;

    FullScreen = ((NewMode & CONSOLE_FULLSCREEN_MODE) != 0);

    if (FullScreen != GuiData->GuiInfo.FullScreen)
    {
        SwitchFullScreen(GuiData, FullScreen);
    }

    return TRUE;
}

static INT NTAPI
GuiShowMouseCursor(IN OUT PFRONTEND This,
                   BOOL Show)
{
    PGUI_CONSOLE_DATA GuiData = This->Context;

    if (GuiData->IsWindowVisible)
    {
        /* Set the reference count */
        if (Show) ++GuiData->MouseCursorRefCount;
        else      --GuiData->MouseCursorRefCount;

        /* Effectively show (or hide) the cursor (use special values for (w|l)Param) */
        PostMessageW(GuiData->hWindow, WM_SETCURSOR, -1, -1);
    }

    return GuiData->MouseCursorRefCount;
}

static BOOL NTAPI
GuiSetMouseCursor(IN OUT PFRONTEND This,
                  HCURSOR CursorHandle)
{
    PGUI_CONSOLE_DATA GuiData = This->Context;

    /* Do nothing if the window is hidden */
    if (!GuiData->IsWindowVisible) return TRUE;

    /*
     * Set the cursor's handle. If the given handle is NULL,
     * then restore the default cursor.
     */
    GuiData->hCursor = (CursorHandle ? CursorHandle : ghDefaultCursor);

    /* Effectively modify the shape of the cursor (use special values for (w|l)Param) */
    PostMessageW(GuiData->hWindow, WM_SETCURSOR, -1, -1);

    return TRUE;
}

static HMENU NTAPI
GuiMenuControl(IN OUT PFRONTEND This,
               UINT CmdIdLow,
               UINT CmdIdHigh)
{
    PGUI_CONSOLE_DATA GuiData = This->Context;

    GuiData->CmdIdLow  = CmdIdLow ;
    GuiData->CmdIdHigh = CmdIdHigh;

    return GuiData->hSysMenu;
}

static BOOL NTAPI
GuiSetMenuClose(IN OUT PFRONTEND This,
                BOOL Enable)
{
    /*
     * NOTE: See http://www.mail-archive.com/harbour@harbour-project.org/msg27509.html
     * or http://harbour-devel.1590103.n2.nabble.com/Question-about-hb-gt-win-CtrlHandler-usage-td4670862i20.html
     * for more information.
     */

    PGUI_CONSOLE_DATA GuiData = This->Context;

    if (GuiData->hSysMenu == NULL) return FALSE;

    GuiData->IsCloseButtonEnabled = Enable;
    EnableMenuItem(GuiData->hSysMenu, SC_CLOSE, MF_BYCOMMAND | (Enable ? MF_ENABLED : MF_GRAYED));

    return TRUE;
}

static FRONTEND_VTBL GuiVtbl =
{
    GuiInitFrontEnd,
    GuiDeinitFrontEnd,
    GuiDrawRegion,
    GuiWriteStream,
    GuiRingBell,
    GuiSetCursorInfo,
    GuiSetScreenInfo,
    GuiResizeTerminal,
    GuiSetActiveScreenBuffer,
    GuiReleaseScreenBuffer,
    GuiRefreshInternalInfo,
    GuiChangeTitle,
    GuiChangeIcon,
    GuiGetThreadConsoleDesktop,
    GuiGetConsoleWindowHandle,
    GuiGetLargestConsoleWindowSize,
    GuiGetSelectionInfo,
    GuiSetPalette,
    GuiGetDisplayMode,
    GuiSetDisplayMode,
    GuiShowMouseCursor,
    GuiSetMouseCursor,
    GuiMenuControl,
    GuiSetMenuClose,
};


NTSTATUS NTAPI
GuiLoadFrontEnd(IN OUT PFRONTEND FrontEnd,
                IN OUT PCONSOLE_STATE_INFO ConsoleInfo,
                IN OUT PCONSOLE_INIT_INFO ConsoleInitInfo,
                IN HANDLE ConsoleLeaderProcessHandle)
{
    PCONSOLE_START_INFO ConsoleStartInfo;
    PGUI_INIT_INFO GuiInitInfo;
    USEROBJECTFLAGS UserObjectFlags;

    if (FrontEnd == NULL || ConsoleInfo == NULL || ConsoleInitInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    ConsoleStartInfo = ConsoleInitInfo->ConsoleStartInfo;

    /*
     * Initialize a private initialization info structure for later use.
     * It must be freed by a call to GuiUnloadFrontEnd or GuiInitFrontEnd.
     */
    GuiInitInfo = ConsoleAllocHeap(HEAP_ZERO_MEMORY, sizeof(*GuiInitInfo));
    if (GuiInitInfo == NULL) return STATUS_NO_MEMORY;

    /* Initialize GUI terminal emulator common functionalities */
    if (!GuiInit(ConsoleInitInfo, ConsoleLeaderProcessHandle, GuiInitInfo))
    {
        ConsoleFreeHeap(GuiInitInfo);
        return STATUS_UNSUCCESSFUL;
    }

    GuiInitInfo->IsWindowVisible = ConsoleInitInfo->IsWindowVisible;
    if (GuiInitInfo->IsWindowVisible)
    {
        /* Don't show the console if the window station is not interactive */
        if (GetUserObjectInformationW(GuiInitInfo->ThreadInfo.WinSta,
                                      UOI_FLAGS,
                                      &UserObjectFlags,
                                      sizeof(UserObjectFlags),
                                      NULL))
        {
            if (!(UserObjectFlags.dwFlags & WSF_VISIBLE))
                GuiInitInfo->IsWindowVisible = FALSE;
        }
    }

    /*
     * Load terminal settings
     */
#if 0
    /* Impersonate the caller in order to retrieve settings in its context */
    // if (!CsrImpersonateClient(NULL))
        // return STATUS_UNSUCCESSFUL;
    CsrImpersonateClient(NULL);

    /* 1. Load the default settings */
    GuiConsoleGetDefaultSettings(&GuiInitInfo->TermInfo);
#endif

    GuiInitInfo->TermInfo.ShowWindow = SW_SHOWNORMAL;

    if (GuiInitInfo->IsWindowVisible)
    {
        /* 2. Load the remaining console settings via the registry */
        if ((ConsoleStartInfo->dwStartupFlags & STARTF_TITLEISLINKNAME) == 0)
        {
#if 0
            /* Load the terminal information from the registry */
            GuiConsoleReadUserSettings(&GuiInitInfo->TermInfo);
#endif

            /*
             * Now, update them with the properties the user might gave to us
             * via the STARTUPINFO structure before calling CreateProcess
             * (and which was transmitted via the ConsoleStartInfo structure).
             * We therefore overwrite the values read in the registry.
             */
            if (ConsoleStartInfo->dwStartupFlags & STARTF_USESHOWWINDOW)
            {
                GuiInitInfo->TermInfo.ShowWindow = ConsoleStartInfo->wShowWindow;
            }
            if (ConsoleStartInfo->dwStartupFlags & STARTF_USEPOSITION)
            {
                ConsoleInfo->AutoPosition = FALSE;
                ConsoleInfo->WindowPosition.x = ConsoleStartInfo->dwWindowOrigin.X;
                ConsoleInfo->WindowPosition.y = ConsoleStartInfo->dwWindowOrigin.Y;
            }
            if (ConsoleStartInfo->dwStartupFlags & STARTF_RUNFULLSCREEN)
            {
                ConsoleInfo->FullScreen = TRUE;
            }
        }
    }

#if 0
    /* Revert impersonation */
    CsrRevertToSelf();
#endif

    // Font data
    StringCchCopyNW(GuiInitInfo->TermInfo.FaceName, ARRAYSIZE(GuiInitInfo->TermInfo.FaceName),
                    ConsoleInfo->FaceName, ARRAYSIZE(ConsoleInfo->FaceName));
    GuiInitInfo->TermInfo.FontFamily = ConsoleInfo->FontFamily;
    GuiInitInfo->TermInfo.FontSize   = ConsoleInfo->FontSize;
    GuiInitInfo->TermInfo.FontWeight = ConsoleInfo->FontWeight;

    // Display
    GuiInitInfo->TermInfo.FullScreen   = ConsoleInfo->FullScreen;
    GuiInitInfo->TermInfo.AutoPosition = ConsoleInfo->AutoPosition;
    GuiInitInfo->TermInfo.WindowOrigin = ConsoleInfo->WindowPosition;

    /* Initialize the icon handles */
    // if (ConsoleStartInfo->hIcon != NULL)
        GuiInitInfo->hIcon = ConsoleStartInfo->hIcon;
    // else
        // GuiInitInfo->hIcon = ghDefaultIcon;

    // if (ConsoleStartInfo->hIconSm != NULL)
        GuiInitInfo->hIconSm = ConsoleStartInfo->hIconSm;
    // else
        // GuiInitInfo->hIconSm = ghDefaultIconSm;

    // ASSERT(GuiInitInfo->hIcon && GuiInitInfo->hIconSm);

    /* Finally, initialize the frontend structure */
    FrontEnd->Vtbl     = &GuiVtbl;
    FrontEnd->Context  = NULL;
    FrontEnd->Context2 = GuiInitInfo;

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
GuiUnloadFrontEnd(IN OUT PFRONTEND FrontEnd)
{
    if (FrontEnd == NULL) return STATUS_INVALID_PARAMETER;

    if (FrontEnd->Context ) GuiDeinitFrontEnd(FrontEnd);
    if (FrontEnd->Context2) ConsoleFreeHeap(FrontEnd->Context2);

    return STATUS_SUCCESS;
}

/* EOF */
