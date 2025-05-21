/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Console Server DLL
 * FILE:            win32ss/user/winsrv/concfg/settings.h
 * PURPOSE:         Public Console Settings Management Interface
 * PROGRAMMERS:     Johannes Anderwald
 *                  Hermes Belusca-Maito (hermes.belusca@sfr.fr)
 */

#pragma once

/* STRUCTURES *****************************************************************/

/*
 * Undocumented message and structure used by Windows' console.dll
 * for setting console info.
 * See https://web.archive.org/web/20160307053337/https://www.catch22.net/sites/default/source/files/setconsoleinfo.c
 * and https://dl.packetstormsecurity.net/papers/win/MSBugPaper.pdf
 * for more information.
 */
#define WM_SETCONSOLEINFO   (WM_USER + 201)

// This shared structure has alignment requirements
// in order to be compatible with the Windows one.
#pragma pack(push, 4)

typedef struct _CONSOLE_STATE_INFO
{
    ULONG       cbSize; // Real length of this structure, at least sizeof(_CONSOLE_STATE_INFO).
                        // The real length takes into account for the real size of the console title.

    COORD       ScreenBufferSize;
    COORD       WindowSize;
    POINT       WindowPosition;

    COORD       FontSize;
    ULONG       FontFamily;
    ULONG       FontWeight;
    WCHAR       FaceName[LF_FACESIZE];

    ULONG       CursorSize;
    BOOL        FullScreen;
    BOOL        QuickEdit;
    BOOL        AutoPosition;
    BOOL        InsertMode;

    USHORT      ScreenAttributes;
    USHORT      PopupAttributes;
    BOOL        HistoryNoDup;
    ULONG       HistoryBufferSize;
    ULONG       NumberOfHistoryBuffers;

    COLORREF    ColorTable[16];

    ULONG       CodePage;
    HWND        hWnd;

    WCHAR       ConsoleTitle[ANYSIZE_ARRAY];
} CONSOLE_STATE_INFO, *PCONSOLE_STATE_INFO;

#ifdef _M_IX86
C_ASSERT(sizeof(CONSOLE_STATE_INFO) == 0xD0);
#endif

#pragma pack(pop)

/*
 * BYTE Foreground = LOBYTE(Attributes) & 0x0F;
 * BYTE Background = (LOBYTE(Attributes) & 0xF0) >> 4;
 */
#define RGBFromAttrib(Console, Attribute)   ((Console)->Colors[(Attribute) & 0xF])
#define TextAttribFromAttrib(Attribute)     ( !((Attribute) & COMMON_LVB_REVERSE_VIDEO) ? (Attribute) & 0xF : ((Attribute) >> 4) & 0xF )
#define BkgdAttribFromAttrib(Attribute)     ( !((Attribute) & COMMON_LVB_REVERSE_VIDEO) ? ((Attribute) >> 4) & 0xF : (Attribute) & 0xF )
#define MakeAttrib(TextAttrib, BkgdAttrib)  (USHORT)((((BkgdAttrib) & 0xF) << 4) | ((TextAttrib) & 0xF))

/* FUNCTIONS ******************************************************************/

#define ConLnkGetConsoleProperties_USE_OUTPTR

// FIXME: TEMP include for NT_(FE_)CONSOLE_PROPS; find an alternative solution!
#include <shlobj.h>

BOOLEAN
ConCfgReadUserSettings(
    IN OUT PCONSOLE_STATE_INFO ConsoleInfo,
    IN BOOLEAN DefaultSettings);

BOOLEAN
ConCfgWriteUserSettings(
    IN PCONSOLE_STATE_INFO ConsoleInfo,
    IN BOOLEAN DefaultSettings);

VOID
ConCfgInitDefaultSettings(
    IN OUT PCONSOLE_STATE_INFO ConsoleInfo);

VOID
ConCfgGetDefaultSettings(
    IN OUT PCONSOLE_STATE_INFO ConsoleInfo);

HRESULT
ConLnkGetConsoleProperties(
    _In_ PCWSTR FilePath,
#ifdef ConLnkGetConsoleProperties_USE_OUTPTR
    _Outptr_ /*__drv_allocatesMem(Mem)*/ NT_CONSOLE_PROPS** ppConProps,
    _Outptr_ /*__drv_allocatesMem(Mem)*/ NT_FE_CONSOLE_PROPS** ppFeConProps
#else
    _Out_ NT_CONSOLE_PROPS* conProps,
    _Out_ NT_FE_CONSOLE_PROPS* feConProps
#endif
    );

HRESULT
ConLnkReadSettingsEx(
    _In_ PCWSTR FilePath,
    _Out_writes_opt_z_(cchName) PWSTR pszName,
    _In_ SIZE_T cchName,
    _Out_writes_opt_z_(cchIconLocation) PWSTR pszIconLocation,
    _In_ SIZE_T cchIconLocation,
    _Out_opt_ PINT piIcon,
    _Out_opt_ PINT piShowCmd,
    _Out_opt_ PWORD pwHotkey,
#ifdef ConLnkGetConsoleProperties_USE_OUTPTR
    _Outptr_ /*__drv_allocatesMem(Mem)*/ NT_CONSOLE_PROPS** ppConProps,
    _Outptr_ /*__drv_allocatesMem(Mem)*/ NT_FE_CONSOLE_PROPS** ppFeConProps
#else
    _Out_ NT_CONSOLE_PROPS* conProps,
    _Out_ NT_FE_CONSOLE_PROPS* feConProps
#endif
    );

HRESULT
ConLnkReadSettings(
    _Inout_ PCONSOLE_STATE_INFO ConsoleInfo,
    _In_ PCWSTR FilePath);

HRESULT
ConLnkWriteSettings(
    _In_ PCONSOLE_STATE_INFO ConsoleInfo,
    _In_ PCWSTR FilePath);

/* EOF */
