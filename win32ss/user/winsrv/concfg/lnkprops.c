/*
 * PROJECT:     ReactOS Console Configuration DLL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Shell Property Sheet Extension support
 * COPYRIGHT:   Copyright 2025 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

/* INCLUDES *******************************************************************/

#include "precomp.h"

/* This is for COM usage */
#define COBJMACROS
#include <shlobj.h>

#include "settings.h"

/* FUNCTIONS ******************************************************************/

VOID ErrOut(HRESULT hr, DWORD dwLastError, PCWSTR pszWhere)
{
    WCHAR szBuffer[260];
    StringCchPrintfW(szBuffer, _countof(szBuffer),
                     L"%s : HRESULT 0x%08x , LastError %lu",
                     pszWhere ? pszWhere : L"n/a", hr, dwLastError);
    MessageBoxW(NULL, szBuffer, L"lnkprops", MB_ICONERROR | MB_OK);
}


/**
 * @brief
 * Opens a shell shortcut link file and retrieves the interface pointers
 * for manipulating it.
 **/
HRESULT
ConLnkOpen(
    _In_ PCWSTR FilePath,
    _In_ BOOLEAN OpenForReadOnly,
    _COM_Outptr_ IShellLinkW** ppsl,
    _COM_Outptr_ IPersistFile** pppf)
{
    HRESULT hr;

    /* Get a pointer to the IShellLink interface. Use SHCoCreateInstance()
     * instead of CoCreateInstance() because we open a shell-implemented
     * object, and this avoids a Co(Un)initialize() call and depending
     * on ole32.dll. */
    *ppsl = NULL;
    hr = SHCoCreateInstance(NULL,
                            &CLSID_ShellLink,
                            NULL,
                            &IID_IShellLinkW,
                            (PVOID*)ppsl);
    if (!SUCCEEDED(hr)) { ErrOut(hr, GetLastError(), L"SHCoCreateInstance");
        return hr; }

    /* Get a pointer to the IPersistFile interface */
    *pppf = NULL;
    hr = IPersistFile_QueryInterface(*ppsl, &IID_IPersistFile, (PVOID*)pppf);
    if (!SUCCEEDED(hr))
    {
ErrOut(hr, GetLastError(), L"IPersistFile_QueryInterface");
        IShellLinkW_Release(*ppsl);
        *ppsl = NULL;
    }

    /* Load the shortcut */
    hr = IPersistFile_Load(*pppf,
                           FilePath,
                           OpenForReadOnly ? STGM_READ
                                           : STGM_READWRITE | STGM_SHARE_EXCLUSIVE /*STGM_SHARE_DENY_WRITE*/);
    if (!SUCCEEDED(hr))
    {
ErrOut(hr, GetLastError(), L"IPersistFile_Load");
        IPersistFile_Release(*pppf);
        *pppf = NULL;
        IShellLinkW_Release(*ppsl);
        *ppsl = NULL;
    }

    return hr;
}

HRESULT
ConLnkClose(
    _In_ IShellLinkW* psl,
    _In_ IPersistFile* ppf)
{
    HRESULT hr = S_OK;

    /* Release all the pointers we have */
    IPersistFile_Release(ppf);
    // *pppf = NULL;
    IShellLinkW_Release(psl);
    // *ppsl = NULL;

    return hr;
}

/**
 * @brief
 * Retrieves the console-specific properties from the given
 * shell link shortcut file.
 *
 * @param[in]   psl
 * An IShellLinkW interface pointer to the shell link shortcut file,
 * opened for read access.
 *
#ifdef ConLnkGetConsoleProperties_USE_OUTPTR
 * @param[out]  ppConProps
 * On output, receives a pointer to an allocated @p NT_CONSOLE_PROPS buffer
 * holding the main console properties.
 * When it is no longer needed, the caller must free the buffer by calling @p LocalFree.
 *
 * @param[out]  ppFeConProps
 * On output, receives a pointer to an allocated @p NT_FE_CONSOLE_PROPS buffer
 * holding the international console properties (code page).
 * When it is no longer needed, the caller must free the buffer by calling @p LocalFree.
#else
 * @param[out]  conProps
 * A pointer to a @p NT_CONSOLE_PROPS buffer holding the main console properties.
 *
 * @param[out]  feConProps
 * A pointer to a @p NT_FE_CONSOLE_PROPS buffer holding the international
 * console properties (code page).
#endif
 **/
HRESULT
ConLnkGetConsoleProperties2(
    _In_ IShellLinkW* psl,
#ifdef ConLnkGetConsoleProperties_USE_OUTPTR
    _Outptr_ /*__drv_allocatesMem(Mem)*/ NT_CONSOLE_PROPS** ppConProps,
    _Outptr_ /*__drv_allocatesMem(Mem)*/ NT_FE_CONSOLE_PROPS** ppFeConProps
#else
    _Out_ NT_CONSOLE_PROPS* conProps,
    _Out_ NT_FE_CONSOLE_PROPS* feConProps
#endif
    )
{
    HRESULT hr;
    IShellLinkDataList* psldl = NULL;
#ifndef ConLnkGetConsoleProperties_USE_OUTPTR
    NT_CONSOLE_PROPS *pConProps, **const ppConProps = &pConProps;
    NT_FE_CONSOLE_PROPS *pFeConProps, **const ppFeConProps = &pFeConProps;
#endif

    hr = IShellLinkW_QueryInterface(psl, &IID_IShellLinkDataList, (PVOID*)&psldl);
    if (!SUCCEEDED(hr)) { ErrOut(hr, GetLastError(), L"IID_IShellLinkDataList");
        return hr; }

#ifdef ConLnkGetConsoleProperties_USE_OUTPTR
    *ppConProps = NULL;
    *ppFeConProps = NULL;
#else
    conProps->dbh.cbSize = 0;
    feConProps->dbh.cbSize = 0;
#endif

    hr = IShellLinkDataList_CopyDataBlock(psldl, NT_CONSOLE_PROPS_SIG, (PVOID*)ppConProps);
ErrOut(hr, GetLastError(), L"NT_CONSOLE_PROPS_SIG");
if (!*ppConProps) MessageBoxW(NULL, L"pConProps NULL", NULL, MB_OK);
    if (SUCCEEDED(hr) && *ppConProps)
    {
        ASSERT((*ppConProps)->dbh.cbSize == sizeof(**ppConProps) &&
               (*ppConProps)->dbh.dwSignature == NT_CONSOLE_PROPS_SIG);

#ifndef ConLnkGetConsoleProperties_USE_OUTPTR
        /* pConProps == *ppConProps */
        RtlCopyMemory(conProps, pConProps, sizeof(*pConProps));
        LocalFree(pConProps);
#endif
    }

    /* Retrieve the optional NT_FE_CONSOLE_PROPS block only if
     * a valid NT_CONSOLE_PROPS block has been previously found */
    if (SUCCEEDED(hr))
    {
        /* Don't propagate the errors out, since
         * the NT_FE_CONSOLE_PROPS are optional */
        HRESULT hr2;
        hr2 = IShellLinkDataList_CopyDataBlock(psldl, NT_FE_CONSOLE_PROPS_SIG, (PVOID*)ppFeConProps);
ErrOut(hr, GetLastError(), L"NT_FE_CONSOLE_PROPS_SIG");
        if (SUCCEEDED(hr2) && *ppFeConProps)
        {
            ASSERT((*ppFeConProps)->dbh.cbSize == sizeof(**ppFeConProps) &&
                   (*ppFeConProps)->dbh.dwSignature == NT_FE_CONSOLE_PROPS_SIG);

#ifndef ConLnkGetConsoleProperties_USE_OUTPTR
            RtlCopyMemory(feConProps, pFeConProps, sizeof(*pFeConProps));
            LocalFree(pFeConProps);
#endif
        }
    }

    IShellLinkDataList_Release(psldl);
    return hr;
}

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
    )
{
    HRESULT hr;
    IShellLinkW* psl;
    IPersistFile* ppf;

    hr = ConLnkOpen(FilePath, TRUE, &psl, &ppf);
    if (!SUCCEEDED(hr))
        return hr;

    hr = ConLnkGetConsoleProperties2(psl,
#ifdef ConLnkGetConsoleProperties_USE_OUTPTR
                                     ppConProps,
                                     ppFeConProps
#else
                                     conProps,
                                     feConProps
#endif
        );

    ConLnkClose(psl, ppf);
    return hr;
}

/**
 * @brief
 * Retrieves generic and console properties from the given
 * shell link shortcut file.
 **/
HRESULT
ConLnkReadSettingsEx2(
    _In_ IShellLinkW* psl,
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
    )
{
    HRESULT hr;

    /* Get the window show command */
    if (piShowCmd)
        hr = IShellLinkW_GetShowCmd(psl, piShowCmd);
    // if (!SUCCEEDED(hr))
    //     return hr;

    /* Get the hotkey */
    if (pwHotkey)
        hr = IShellLinkW_GetHotkey(psl, pwHotkey);
    // if (!SUCCEEDED(hr))
    //     return hr;

    /* Get the icon location, if any */
    if (pszIconLocation && (cchIconLocation > 0) && piIcon)
    {
        // INT IconIndex;
        hr = IShellLinkW_GetIconLocation(psl,
                                         pszIconLocation,
                                         cchIconLocation,
                                         piIcon/*&IconIndex*/);
        if (!SUCCEEDED(hr))
        {
            *pszIconLocation = UNICODE_NULL;
            *piIcon = 0;
        }
    }

    /*
     * Load the optional console-specific shortcut property blocks:
     * NT_CONSOLE_PROPS and NT_FE_CONSOLE_PROPS.
     * Note that we don't want to propagate errors out here, because
     * we want to allow reading the generic shortcut properties (above),
     * and then these more specific ones. If the specific ones fail,
     * we still treat it like a success so that we can continue to load.
     */
    if (SUCCEEDED(hr))
    {
        HRESULT hr2;

#ifdef ConLnkGetConsoleProperties_USE_OUTPTR
        *ppConProps = NULL;
        *ppFeConProps = NULL;
#else
        conProps->dbh.cbSize = 0;
        feConProps->dbh.cbSize = 0;
#endif

        hr2 = ConLnkGetConsoleProperties2(psl,
#ifdef ConLnkGetConsoleProperties_USE_OUTPTR
                                          ppConProps,
                                          ppFeConProps
#else
                                          conProps,
                                          feConProps
#endif
            );
        DBG_UNREFERENCED_LOCAL_VARIABLE(hr2);
    }

    return hr;
}

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
    )
{
    HRESULT hr;
    IShellLinkW* psl;
    IPersistFile* ppf;

    hr = ConLnkOpen(FilePath, TRUE, &psl, &ppf);
    if (!SUCCEEDED(hr))
        return hr;

    hr = ConLnkReadSettingsEx2(psl,
                               pszName,
                               cchName,
                               pszIconLocation,
                               cchIconLocation,
                               piIcon,
                               piShowCmd,
                               pwHotkey,
#ifdef ConLnkGetConsoleProperties_USE_OUTPTR
                               ppConProps,
                               ppFeConProps
#else
                               conProps,
                               feConProps
#endif
        );

    ConLnkClose(psl, ppf);
    return hr;
}

HRESULT
ConLnkReadSettings2(
    _Inout_ PCONSOLE_STATE_INFO ConsoleInfo,
    _In_ IShellLinkW* psl)
{
    HRESULT hr;
#ifdef ConLnkGetConsoleProperties_USE_OUTPTR
    NT_CONSOLE_PROPS *pConProps;
    NT_FE_CONSOLE_PROPS *pFeConProps;
#else
    NT_CONSOLE_PROPS conProps, *const pConProps = &conProps;
    NT_FE_CONSOLE_PROPS feConProps, *const pFeConProps = &feConProps;
#endif

    hr = ConLnkGetConsoleProperties2(psl,
#ifdef ConLnkGetConsoleProperties_USE_OUTPTR
                                     &pConProps,
                                     &pFeConProps
#else
                                     &conProps,
                                     &feConProps
#endif
        );
    if (!SUCCEEDED(hr))
        return hr;

#ifdef ConLnkGetConsoleProperties_USE_OUTPTR
    if (pConProps)
#endif
    if (pConProps->dbh.cbSize == sizeof(*pConProps))
    {
        /* Now the link is loaded, generate new console settings section
         * to replace the one in the link */
        ConsoleInfo->ScreenAttributes = pConProps->wFillAttribute;
        ConsoleInfo->PopupAttributes = pConProps->wPopupFillAttribute;
        ConsoleInfo->ScreenBufferSize = pConProps->dwScreenBufferSize;
        ConsoleInfo->WindowSize = pConProps->dwWindowSize;
        ConsoleInfo->WindowPosition.x = (LONG)(ULONG)pConProps->dwWindowOrigin.X;
        ConsoleInfo->WindowPosition.y = (LONG)(ULONG)pConProps->dwWindowOrigin.Y;
        // pConProps->nFont;             // FIXME?
        // pConProps->nInputBufferSize;  // FIXME?
        ConsoleInfo->FontSize = pConProps->dwFontSize;
        ConsoleInfo->FontFamily = pConProps->uFontFamily;
        ConsoleInfo->FontWeight = pConProps->uFontWeight;
        RtlCopyMemory(ConsoleInfo->FaceName, pConProps->FaceName, sizeof(ConsoleInfo->FaceName));
        ConsoleInfo->CursorSize = pConProps->uCursorSize;
        ConsoleInfo->FullScreen = pConProps->bFullScreen;
        ConsoleInfo->QuickEdit = pConProps->bQuickEdit;
        ConsoleInfo->InsertMode = pConProps->bInsertMode;
        ConsoleInfo->AutoPosition = pConProps->bAutoPosition;
        ConsoleInfo->HistoryBufferSize = pConProps->uHistoryBufferSize;
        ConsoleInfo->NumberOfHistoryBuffers = pConProps->uNumberOfHistoryBuffers;
        ConsoleInfo->HistoryNoDup = pConProps->bHistoryNoDup;
        RtlCopyMemory(ConsoleInfo->ColorTable, pConProps->ColorTable, sizeof(ConsoleInfo->ColorTable));
    }

#ifdef ConLnkGetConsoleProperties_USE_OUTPTR
    if (pFeConProps)
#endif
    if (pFeConProps->dbh.cbSize == sizeof(*pFeConProps))
    {
        ConsoleInfo->CodePage = pFeConProps->uCodePage;
    }

#ifdef ConLnkGetConsoleProperties_USE_OUTPTR
    if (pConProps)
        LocalFree(pConProps);
    if (pFeConProps)
        LocalFree(pFeConProps);
#endif

    return hr;
}

HRESULT
ConLnkReadSettings(
    _Inout_ PCONSOLE_STATE_INFO ConsoleInfo,
    _In_ PCWSTR FilePath)
{
    HRESULT hr;
    IShellLinkW* psl;
    IPersistFile* ppf;

    hr = ConLnkOpen(FilePath, TRUE, &psl, &ppf);
    if (!SUCCEEDED(hr))
        return hr;

    hr = ConLnkReadSettings2(ConsoleInfo, psl);
    ConLnkClose(psl, ppf);
    return hr;
}

HRESULT
ConLnkWriteSettings2(
    _In_ PCONSOLE_STATE_INFO ConsoleInfo,
    _Inout_ IShellLinkW* psl,
    _Inout_ IPersistFile* ppf)
{
    HRESULT hr;
    IShellLinkDataList* psldl = NULL;
    NT_CONSOLE_PROPS conProps; // props;

    hr = IShellLinkW_QueryInterface(psl, &IID_IShellLinkDataList, (PVOID*)&psldl);
    if (!SUCCEEDED(hr))
        return hr; // FALSE;

    /* Now the link is loaded, generate new console settings section
     * to replace the one in the link */
    conProps.dbh.cbSize = sizeof(conProps);
    conProps.dbh.dwSignature = NT_CONSOLE_PROPS_SIG;
    conProps.wFillAttribute = ConsoleInfo->ScreenAttributes;
    conProps.wPopupFillAttribute = ConsoleInfo->PopupAttributes;
    conProps.dwScreenBufferSize = ConsoleInfo->ScreenBufferSize;
    conProps.dwWindowSize = ConsoleInfo->WindowSize;
    conProps.dwWindowOrigin.X = (SHORT)ConsoleInfo->WindowPosition.x;
    conProps.dwWindowOrigin.Y = (SHORT)ConsoleInfo->WindowPosition.y;
    conProps.nFont = 0;             // FIXME?
    conProps.nInputBufferSize = 0;  // FIXME?
    conProps.dwFontSize = ConsoleInfo->FontSize;
    conProps.uFontFamily = ConsoleInfo->FontFamily;
    conProps.uFontWeight = ConsoleInfo->FontWeight;
    RtlCopyMemory(conProps.FaceName, ConsoleInfo->FaceName, sizeof(conProps.FaceName));
    conProps.uCursorSize = ConsoleInfo->CursorSize;
    conProps.bFullScreen = ConsoleInfo->FullScreen;
    conProps.bQuickEdit = ConsoleInfo->QuickEdit;
    conProps.bInsertMode = ConsoleInfo->InsertMode;
    conProps.bAutoPosition = ConsoleInfo->AutoPosition;
    conProps.uHistoryBufferSize = ConsoleInfo->HistoryBufferSize;
    conProps.uNumberOfHistoryBuffers = ConsoleInfo->NumberOfHistoryBuffers;
    conProps.bHistoryNoDup = ConsoleInfo->HistoryNoDup;
    RtlCopyMemory(conProps.ColorTable, ConsoleInfo->ColorTable, sizeof(conProps.ColorTable));

    /* Store the changes back into the link */
    hr = IShellLinkDataList_RemoveDataBlock(psldl, NT_CONSOLE_PROPS_SIG);
    if (SUCCEEDED(hr))
        hr = IShellLinkDataList_AddDataBlock(psldl, &conProps);

    /* Save also the specific code-page if it is
     * different from the ambient system one. */
    if (SUCCEEDED(hr) /*&& fEastAsianSystem*/ && (ConsoleInfo->CodePage != GetOEMCP()))
    {
        NT_FE_CONSOLE_PROPS feConProps; // fe_props
        feConProps.dbh.cbSize = sizeof(feConProps);
        feConProps.dbh.dwSignature = NT_FE_CONSOLE_PROPS_SIG;
        feConProps.uCodePage = ConsoleInfo->CodePage;

        hr = IShellLinkDataList_RemoveDataBlock(psldl, NT_FE_CONSOLE_PROPS_SIG);
        if (SUCCEEDED(hr))
            hr = IShellLinkDataList_AddDataBlock(psldl, &feConProps);
    }

    /* Only persist changes if we've successfully made them */
    if (SUCCEEDED(hr))
        hr = IPersistFile_Save(ppf, NULL, TRUE);

    IShellLinkDataList_Release(psldl);
    // IPersistFile_Release(ppf);
    // IShellLinkW_Release(psl);

    return hr; // SUCCEEDED(hr);
}

HRESULT
ConLnkWriteSettings(
    _In_ PCONSOLE_STATE_INFO ConsoleInfo,
    _In_ PCWSTR FilePath)
{
    HRESULT hr;
    IShellLinkW* psl;
    IPersistFile* ppf;

    hr = ConLnkOpen(FilePath, FALSE, &psl, &ppf);
    if (!SUCCEEDED(hr))
        return hr;

    hr = ConLnkWriteSettings2(ConsoleInfo, psl, ppf);
    ConLnkClose(psl, ppf);
    return hr;
}

/* EOF */
