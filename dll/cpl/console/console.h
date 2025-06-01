#ifndef _CONSOLE_H_
#define _CONSOLE_H_

//#include <stdio.h>
//#include <stdlib.h>
#include <wchar.h>
#include <tchar.h>

#define WIN32_NO_STATUS
//
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
//

#include <windef.h>
#include <winbase.h>
#include <wincon.h>
#include <wingdi.h>
// #include <winnls.h>
// #include <winreg.h>
// #include <winuser.h>

// #include <commctrl.h>
#include <cpl.h>

#include <strsafe.h>

#include "resource.h"

#define EnableDlgItem(hDlg, nID, bEnable)   \
    EnableWindow(GetDlgItem((hDlg), (nID)), (bEnable))

/* Shared header with the GUI Terminal Front-End from consrv.dll */
#include "concfg.h" // in /winsrv/concfg/

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _FONT_PREVIEW
{
    HFONT hFont;
    UINT  CharWidth;
    UINT  CharHeight;
} FONT_PREVIEW;

struct _CONSOLE_PROPS_CTX;
typedef BOOL
(*PAPPLY_INFO)(
    _In_opt_ HWND hDlg,
    _In_ struct _CONSOLE_PROPS_CTX* pConProps,
    _In_ BOOL bApplyNow);

typedef struct _CONSOLE_PROPS_CTX
{
    PCONSOLE_STATE_INFO ConInfo; ///< Local capture of the console information.
    FONT_PREVIEW FontPreview;    /**< Current active font, corresponding to the
                                  **  active console font, and used for painting
                                  **  the text samples.
                                  **/
    BOOL UpdateValues;           ///< TRUE if settings need to be saved, FALSE if not.
    PAPPLY_INFO ApplyInfo;
} CONSOLE_PROPS_CTX, *PCONSOLE_PROPS_CTX;

/* Globals */
extern HINSTANCE g_hModule; // TODO: Use HMODULE ?

BOOL
InitializeConsolePropsState(VOID);

VOID
UninitializeConsolePropsState(VOID);

VOID
ApplyConsoleInfo(
    _In_ HWND hDlg,
    _In_ PCONSOLE_PROPS_CTX pConProps,
    _In_ BOOL bApplyNow);

BOOL
PopulatePropSheetPageArray(
    _Out_writes_(cPsps) PROPSHEETPAGEW* psp,
    _In_ UINT cPsps,
    _In_opt_ LPARAM lParam,
    _In_opt_ LPFNPSPCALLBACKW pfnCallback);


VOID
RefreshFontPreview(
    IN FONT_PREVIEW* Preview,
    IN PCONSOLE_STATE_INFO pConInfo);

VOID
UpdateFontPreview(
    IN FONT_PREVIEW* Preview,
    IN HFONT hFont,
    IN UINT  CharWidth,
    IN UINT  CharHeight);

#define ResetFontPreview(Preview)   \
    UpdateFontPreview((Preview), NULL, 0, 0)


/* Preview Windows */
#define WPM_INIT    (WM_USER + 100)
BOOL
RegisterWinPrevClass(
    IN HINSTANCE hInstance);

BOOL
UnRegisterWinPrevClass(
    IN HINSTANCE hInstance);

// layout.c

typedef enum _TEXT_TYPE
{
    Screen,
    Popup
} TEXT_TYPE;

VOID
PaintText(
    IN LPDRAWITEMSTRUCT drawItem,
    IN PCONSOLE_PROPS_CTX pConProps,
    IN TEXT_TYPE TextMode);


// utils.c

struct _LIST_CTL;

typedef INT (*PLIST_GETCOUNT)(IN struct _LIST_CTL* ListCtl);
typedef ULONG_PTR (*PLIST_GETDATA)(IN struct _LIST_CTL* ListCtl, IN INT Index);

typedef struct _LIST_CTL
{
    HWND hWndList;
    PLIST_GETCOUNT GetCount;
    PLIST_GETDATA  GetData;
} LIST_CTL, *PLIST_CTL;

UINT
BisectListSortedByValueEx(
    IN PLIST_CTL ListCtl,
    IN ULONG_PTR Value,
    IN UINT itemStart,
    IN UINT itemEnd,
    OUT PUINT pValueItem OPTIONAL,
    IN BOOL BisectRightOrLeft);

UINT
BisectListSortedByValue(
    IN PLIST_CTL ListCtl,
    IN ULONG_PTR Value,
    OUT PUINT pValueItem OPTIONAL,
    IN BOOL BisectRightOrLeft);

#ifdef __cplusplus
}
#endif

#endif /* _CONSOLE_H_ */
