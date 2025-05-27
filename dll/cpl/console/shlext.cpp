/*
 * PROJECT:     ReactOS Console Configuration DLL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Shell Property Sheet Extension support
 * COPYRIGHT:   Copyright 2025 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

// #include "console.h"

// #include <stdio.h>
// #include <stdlib.h>
#include <wchar.h>

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H

#include <windef.h>
#include <winbase.h>
#include <wincon.h>
#include <wingdi.h>
#include <shellapi.h>
#include <shlobj.h>

/* Shared header with the GUI Terminal Front-End from consrv.dll */
#include "concfg.h" // in /winsrv/concfg/

#include "resource.h"


/*
 * Reduced ATL support for COM Inproc server
 */
#define _WINDLL
#define _ATL_STATIC_REGISTRY

EXTERN_C
HRESULT
WINAPI
LoadTypeLib(
    LPCOLESTR szFile,
    ITypeLib **pptlib)
{
    return E_FAIL; // E_NOTIMPL;
}

EXTERN_C
HRESULT
WINAPI
RegisterTypeLib(
    ITypeLib  *ptlib,
    LPCOLESTR szFullPath,
    LPCOLESTR szHelpDir)
{
    return S_OK;
}

EXTERN_C
HRESULT
WINAPI
UnRegisterTypeLib(
    REFGUID libID,
    WORD    wVerMajor,
    WORD    wVerMinor,
    LCID    lcid,
    SYSKIND syskind)
{
    return S_OK;
}

// Forward to oleaut32.
#define SysAllocString  PropSysAllocString
#define SysFreeString   PropSysFreeString

EXTERN_C
BSTR WINAPI
PropSysAllocString(LPCOLESTR str);  // return SysAllocString(str);

EXTERN_C
void WINAPI
PropSysFreeString(LPOLESTR str);    // SysFreeString(str);

#include <atlbase.h>
#include <atlcom.h>
#include <atlstr.h> // For CString
// #ifdef _ATL_STATIC_REGISTRY
// #include <statreg.h>
// #endif

#include <strsafe.h>


/********
 ** TODO: This helper should be used throughout the whole console cpl
 **/
void ACDBG_FN(PCSTR FunctionName, PCWSTR Format, ...)
{
    WCHAR Buffer[512];
    WCHAR* Current = Buffer;
    size_t Length = _countof(Buffer);

    StringCchPrintfExW(Current, Length, &Current, &Length, STRSAFE_NULL_ON_FAILURE, L"[%-20S] ", FunctionName);
    va_list ArgList;
    va_start(ArgList, Format);
    StringCchVPrintfExW(Current, Length, &Current, &Length, STRSAFE_NULL_ON_FAILURE, Format, ArgList);
    va_end(ArgList);
    OutputDebugStringW(Buffer);
}

#define ACDBG(fmt, ...)  ACDBG_FN(__FUNCTION__, fmt, ##__VA_ARGS__ )

/********/

EXTERN_C VOID ErrOut(HRESULT hr, DWORD dwLastError, PCWSTR pszWhere);
EXTERN_C BOOL
PopulatePropSheetPageArray(
    _Out_writes_(cPsps) PROPSHEETPAGEW* psp,
    _In_ UINT cPsps,
    _In_opt_ LPFNPSPCALLBACKW pfnCallback);

EXTERN_C BOOL
RegisterWinPrevClass(
    IN HINSTANCE hInstance);

EXTERN_C BOOL
UnRegisterWinPrevClass(
    IN HINSTANCE hInstance);



EXTERN_C HINSTANCE g_hModule; // TODO: Use HMODULE ?
EXTERN_C PCONSOLE_STATE_INFO ConInfo;
LONG g_ModuleRefCnt = 0;

// DEFINE_GUID(CLSID_ConsoleProperties, ...);
const GUID CLSID_ConsoleProperties = { 0xD2942F8E, 0x478E, 0x41D3, { 0x87, 0x0A, 0x35, 0xA1, 0x62, 0x38, 0xF4, 0xEE } };
// ROS-specific: {86FFDF58-CF11-482F-96D5-0B72BEC65890} = s 'Console Properties'

class CConsoleModule : public CAtlDllModuleT<CConsoleModule>
{
    /* See for example, https://stackoverflow.com/a/42878450 */
    HRESULT AddCommonRGSReplacements(IRegistrarBase* pRegistrar)
    {
        // __super::AddCommonRGSReplacements(pRegistrar);
        // pRegistrar->AddReplacement(L"LIBID", _PersistHelper::StringFromIdentifier(m_libid));
        // pRegistrar->AddReplacement(L"FILENAME", CStringW(PathFindFileName(GetModulePath())));
        // pRegistrar->AddReplacement(L"DESCRIPTION", CStringW(AtlLoadString(IDS_PROJNAME)));
        return S_OK;
    }
    HRESULT RegisterTypeLib()
    {
        return S_OK;
    }
    HRESULT RegisterTypeLib(_In_opt_z_ LPCTSTR lpszIndex)
    {
        return S_OK;
    }
    HRESULT UnRegisterTypeLib()
    {
        return S_OK;
    }
    HRESULT UnRegisterTypeLib(_In_opt_z_ LPCTSTR lpszIndex)
    {
        return S_OK;
    }
};

CConsoleModule gModule;

class ATL_NO_VTABLE CConsoleProps :
    public CComCoClass<CConsoleProps, &CLSID_ConsoleProperties>,
    public CComObjectRootEx</*CComMultiThreadModelNoCS*/CComSingleThreadModel>,
    public IShellExtInit,
    public IShellPropSheetExt
{
public:
    /*
     * For use by property sheets when added to shell file properties dialog.
     * Maintain refcount of each page and release things we've registered
     * when we hit 0. Needed because the lifetime of the property sheets isn't
     * tied to the lifetime of our IShellPropSheetExt object.
     */
    static UINT
    CALLBACK
    PropSheetPageProc(
        _In_ HWND hWnd,
        _In_ UINT uMsg,
        _Inout_ PROPSHEETPAGEW* /*ppsp*/)
    {
        // static UINT cRefs = 0;
        switch (uMsg)
        {
            case PSPCB_ADDREF:
                InterlockedIncrement(&g_ModuleRefCnt);
                break;

            case PSPCB_RELEASE:
                if (InterlockedDecrement(&g_ModuleRefCnt) == 0)
                {
                    /* Only persist settings if they've changed */
                    // if (gpStateInfo->UpdateValues)
                    //     SaveConsoleSettingsIfNeeded(hWnd);

                    /// UninitializeConsoleState();
                    UnRegisterWinPrevClass(g_hModule);
                    ClearTTFontCache();

                    /* Cleanup */
                    HeapFree(GetProcessHeap(), 0, ConInfo);
                    ConInfo = NULL;
                }
                break;
        }

        return 1;
    }

public:

    // IShellExtInit
    STDMETHODIMP Initialize(
        _In_ PCIDLIST_ABSOLUTE pidlFolder,
        _In_ LPDATAOBJECT pdtobj,
        _In_ HKEY hkeyProgID) override;


    // IShellPropSheetExt
    STDMETHODIMP AddPages(
        _In_ LPFNSVADDPROPSHEETPAGE pfnAddPage,
        _In_ LPARAM lParam) override
    {
        PROPSHEETPAGEW psp[4];
        // HRESULT hr = S_OK;
        UINT i = 0;

        if (!PopulatePropSheetPageArray(psp, _countof(psp), PropSheetPageProc))
            return E_FAIL;

        for (i = 0; i < _countof(psp); ++i)
        {
            HPROPSHEETPAGE hPage = CreatePropertySheetPage(&psp[i]);
            // hr = (hPage != NULL) ? S_OK : E_FAIL;
            if (!hPage)
                return E_FAIL;
            if (!pfnAddPage(hPage, lParam))
            {
                DestroyPropertySheetPage(hPage);
                return E_FAIL;
            }
        }

        return S_OK;
    }

    STDMETHODIMP ReplacePage(
        _In_ EXPPS uPageID,
        _In_ LPFNSVADDPROPSHEETPAGE pfnReplaceWith,
        _In_ LPARAM lParam)
    {
        return E_NOTIMPL;
    }

public:

    DECLARE_NOT_AGGREGATABLE(CConsoleProps)
    DECLARE_PROTECT_FINAL_CONSTRUCT()

    // DECLARE_REGISTRY_RESOURCEID(IDR_CONSOLE)
    /*
     * See also https://www.codeproject.com/Articles/6319/Registry-Map-for-RGS-files
     * as well as:
     * https://github.com/firebreath/FireBreath/blob/master/src/ActiveXCore/registrymap.hpp
     * https://github.com/google/omaha/blob/main/omaha/base/ATLRegMapEx.h
     * https://github.com/google/omaha/blob/main/omaha/goopdate/cocreate_async.h
     * https://github.com/microsoft/SampleNativeCOMAddin/blob/main/connect.h
     */
    static HRESULT WINAPI UpdateRegistry(BOOL bRegister) throw()
    {
        CComBSTR CLSID_ConsoleProperties_STR(CLSID_ConsoleProperties);
        _ATL_REGMAP_ENTRY regMapEntries[] =
        {
            { OLESTR("CLSID"), CLSID_ConsoleProperties_STR },
            { NULL, NULL }
        };

CString msg = (bRegister ? L"Register " : L"Unregister ") + CString(regMapEntries[0].szData);
MessageBoxW(NULL, (LPCWSTR)msg, L"Info", MB_OK);

        /*
         * BUGBUG Wine/ReactOS ATL:
         * In unregistration mode, we cannot delete the single value we've added
         * via the .RGS script in the "Control Panel" subkey, so we'll manually
         * delete it there.
         */
        if (!bRegister)
        {
            CRegKey rkDel;
            LONG lRes = rkDel.Open(HKEY_LOCAL_MACHINE,
                                   L"Software\\Microsoft\\Windows\\CurrentVersion\\Control Panel\\Cpls",
                                   KEY_WRITE);
            if (lRes == ERROR_SUCCESS)
                lRes = rkDel.DeleteValue(L"Console");
            if (lRes != ERROR_SUCCESS)
                ACDBG(L"Couldn't delete CPL value 'Console', error %d\r\n", lRes);
            DBG_UNREFERENCED_LOCAL_VARIABLE(lRes);
        }

        return ATL::_pAtlModule->UpdateRegistryFromResource(IDR_CONSOLE, bRegister, regMapEntries);
    }

    BEGIN_COM_MAP(CConsoleProps)
        COM_INTERFACE_ENTRY_IID(IID_IShellExtInit, IShellExtInit)
        COM_INTERFACE_ENTRY_IID(IID_IShellPropSheetExt, IShellPropSheetExt)
    END_COM_MAP()
};

#if 0 // If using CComModule
BEGIN_OBJECT_MAP(ObjectMap)
    OBJECT_ENTRY(CLSID_ConsoleProperties, CConsoleProps)
END_OBJECT_MAP()
#else
OBJECT_ENTRY_AUTO(CLSID_ConsoleProperties, CConsoleProps)
#endif

#if 1
#include <shellutils.h>
// EXTERN_C
BOOL WINAPI GetExeFromLnk(PCWSTR pszLnk, PWSTR pszExe, size_t cchSize)
{
    CCoInit init;
    if (FAILED_UNEXPECTEDLY(init.hr))
        return FALSE;

    CComPtr<IShellLinkW> spShellLink;
    if (FAILED_UNEXPECTEDLY(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARG(IShellLinkW, &spShellLink))))
        return FALSE;

    CComPtr<IPersistFile> spPersistFile;
    if (FAILED_UNEXPECTEDLY(spShellLink->QueryInterface(IID_PPV_ARG(IPersistFile, &spPersistFile))))
        return FALSE;

    if (FAILED_UNEXPECTEDLY(spPersistFile->Load(pszLnk, STGM_READ)) || FAILED_UNEXPECTEDLY(spShellLink->Resolve(NULL, SLR_NO_UI | SLR_NOUPDATE | SLR_NOSEARCH)))
        return FALSE;

    return !FAILED_UNEXPECTEDLY(spShellLink->GetPath(pszExe, cchSize, NULL, 0/*SLGP_RAWPATH*/));
}
#endif

CString
ExpandEnvironmentStrings(
    _In_ PCWSTR String)
{
    CString ExpandedString;
    DWORD dwRequired = ::ExpandEnvironmentStringsW(String, NULL, 0);
    if (dwRequired > 0)
    {
        LPWSTR Buffer = ExpandedString.GetBuffer(dwRequired);
        DWORD dwReturned = ::ExpandEnvironmentStringsW(String, Buffer, dwRequired);
        if (dwRequired == dwReturned)
        {
            ExpandedString.ReleaseBufferSetLength(dwReturned - 1);
            ACDBG(L"Expanded '%s' => '%s'\r\n", String, (PCWSTR)ExpandedString);
        }
        else
        {
            ExpandedString.ReleaseBufferSetLength(0);
            ExpandedString = String;
            ACDBG(L"Failed during expansion '%s'\r\n", String);
        }
    }
    else
    {
        ACDBG(L"Failed to expand '%s'\r\n", String);
        ExpandedString = String;
    }

    return ExpandedString;
}

HRESULT
/*CConsoleProps::*/InitFile(
    _In_ PCWSTR Filename)
{
    CString ExpandedFilename = ExpandEnvironmentStrings(Filename);

CString msg = L"Opening file '" + CString(Filename) + L"' , expanded: '" + ExpandedFilename + L"'";
MessageBoxW(NULL, (LPCWSTR)msg, L"Info", MB_OK);

    // PCWSTR pwszExt = PathFindExtensionW(ExpandedFilename);
    PCWSTR pwszExt = wcsrchr(ExpandedFilename, L'.');
    if (!pwszExt)
    {
        ACDBG(L"Failed to find an extension: '%s'\r\n", (PCWSTR)ExpandedFilename);
        return E_FAIL;
    }
#if 1
    WCHAR Buffer[MAX_PATH]; // Target of link
    if (!_wcsicmp(pwszExt, L".lnk"))
    {
        if (!GetExeFromLnk(ExpandedFilename, Buffer, _countof(Buffer)))
        {
            ACDBG(L"Failed to read link target from: '%s'\r\n", (PCWSTR)ExpandedFilename);
            return E_FAIL;
        }
        if (!_wcsicmp(Buffer, ExpandedFilename))
        {
            ACDBG(L"Link redirects to itself: '%s'\r\n", (PCWSTR)ExpandedFilename);
            return E_FAIL;
        }
        //// return InitFile(Buffer);
    }
    else
    {
        /* Not a link file, fail */
        return E_FAIL;
    }
#else
    /// if (!_wcsicmp(pwszExt, L".bat") || !_wcsicmp(pwszExt, L".cmd"))
    ///     return S_OK; // Fast path that replaces the function call below.
#endif

msg = L"Link target file: '" + CString(Buffer);
MessageBoxW(NULL, (LPCWSTR)msg, L"Info", MB_OK);
// FIXME: Buffer (target) needs to be expanded before doing the type check below.

    /* Check whether the target is a Win32 console program
     * or a .bat or .cmd (that open with CMD.EXE) */
    DWORD dwExeType;
    dwExeType = (DWORD)::SHGetFileInfoW((PCWSTR)Buffer, 0, NULL, 0, SHGFI_EXETYPE);
    if (dwExeType != MAKELONG('EP', 0)) // If it's not Win32 .exe console or .bat or .cmd
    {
        // Note: MAKELONG('ZM', 0) would be for an MS-DOS .exe or .com file.
        ACDBG(L"Target not recognized as Win32 exe or bat or cmd: '%s', %lu\r\n",
              (PCWSTR)Buffer, dwExeType);
        if (!::GetBinaryTypeW((PCWSTR)Buffer, &dwExeType) ||
            ((dwExeType != SCS_DOS_BINARY  ) && (dwExeType != SCS_PIF_BINARY) &&
             (dwExeType != SCS_POSIX_BINARY) && (dwExeType != SCS_OS216_BINARY)))
        {
            ACDBG(L"Target not recognized as supported binary: '%s', %lu\r\n",
                  (PCWSTR)Buffer, dwExeType);
            return E_FAIL;
        }
    }

///// We've ended shell-specific initialization.
///// Now continue with standard console stuff.

msg.Format(L"g_hModule = 0x%p", g_hModule);
MessageBoxW(NULL, (LPCWSTR)msg, L"Info", MB_OK);

    /* Allocate a local buffer to hold console information */
    ConInfo = (PCONSOLE_STATE_INFO)HeapAlloc(GetProcessHeap(),
                        HEAP_ZERO_MEMORY,
                        sizeof(CONSOLE_STATE_INFO));
    if (!ConInfo)
        return E_OUTOFMEMORY;

msg = L"Loading from: " + ExpandedFilename;
MessageBoxW(NULL, (LPCWSTR)msg, L"Info", MB_OK);
    HRESULT hr = ConLnkReadSettings(ConInfo, ExpandedFilename);
    if (!SUCCEEDED(hr))
    {
msg.Format(L"Failed to load from '%s', error 0x%p", ExpandedFilename, hr);
MessageBoxW(NULL, (LPCWSTR)msg, L"Info", MB_OK);
        // HeapFree(GetProcessHeap(), 0, ConInfo);
        // return hr;
        ConCfgGetDefaultSettings(ConInfo);
    }

    InitTTFontCache();
    RegisterWinPrevClass(g_hModule);

    return S_OK;
}

STDMETHODIMP
CConsoleProps::Initialize(
    _In_ PCIDLIST_ABSOLUTE pidlFolder,  // LPCITEMIDLIST
    _In_ LPDATAOBJECT pdtobj,           // pDataObj
    _In_ HKEY hkeyProgID)               // hProgID
{
    UNREFERENCED_PARAMETER(pidlFolder);
    UNREFERENCED_PARAMETER(hkeyProgID);

    FORMATETC etc = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM stg;

    HRESULT hr = pdtobj->GetData(&etc, &stg);
    if (FAILED(hr))
    {
        ACDBG(L"Failed to retrieve Data from pdtobj.\r\n");
        return E_INVALIDARG;
    }
    hr = E_FAIL;
    HDROP hdrop = (HDROP)::GlobalLock(stg.hGlobal);
    if (hdrop)
    {
        UINT uNumFiles = ::DragQueryFileW(hdrop, 0xFFFFFFFF, NULL, 0);
        /* The properties sheet is for one file only */
        if (uNumFiles == 1)
        {
            WCHAR szFile[MAX_PATH * 2];
            if (::DragQueryFileW(hdrop, 0, szFile, _countof(szFile)))
            {
                /****this->AddRef();****/
                // TODO: It's here that we should check the exe type, and accept
                // whether we should continue to proceed with the property sheet.
                hr = InitFile(szFile);
            }
            else
            {
                ACDBG(L"Failed to query the file.\r\n");
            }
        }
        else
        {
            ACDBG(L"Invalid number of files: %d\r\n", uNumFiles);
        }
        ::GlobalUnlock(stg.hGlobal);
    }
    else
    {
        ACDBG(L"Could not lock stg.hGlobal\r\n");
    }
    ::ReleaseStgMedium(&stg);
    return hr;
}


STDAPI DllCanUnloadNow(VOID)
{
CString msg;

msg.Format(L"g_ModuleRefCnt = %d", g_ModuleRefCnt);
MessageBoxW(NULL, (LPCWSTR)msg, L"DllCanUnloadNow", MB_OK);
    if (g_ModuleRefCnt)
        return S_FALSE;
    HRESULT hr = gModule.DllCanUnloadNow();
msg.Format(L"gModule.DllCanUnloadNow() returned 0x%lx", hr);
MessageBoxW(NULL, (LPCWSTR)msg, L"DllCanUnloadNow", MB_OK);
    return hr;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID *ppv)
{
    return gModule.DllGetClassObject(rclsid, riid, ppv);
}

STDAPI DllRegisterServer(VOID)
{
    return gModule.DllRegisterServer(FALSE);
}

STDAPI DllUnregisterServer(VOID)
{
    return gModule.DllUnregisterServer(FALSE);
}

/* EOF */
