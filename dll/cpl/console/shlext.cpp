/*
 * PROJECT:     ReactOS Console Configuration DLL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Shell Property Sheet Extension support
 * COPYRIGHT:   Copyright 2025 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#include "console.h"

#include <shellapi.h>
#include <shlobj.h>

/*
 * Reduced ATL support for COM Inproc server:
 * Try to disable as much as possible typelib support that is not used here.
 * Also, avoid importing from oleaut32 by stubplementing the unused functions
 * and using ole32->oleaut32 forwarders.
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

/* Use the ole32->oleaut32 forwarders instead of the direct oleaut32 functions */
#define SysAllocString  PropSysAllocString
#define SysFreeString   PropSysFreeString

EXTERN_C
BSTR WINAPI
PropSysAllocString(LPCOLESTR str);

EXTERN_C
void WINAPI
PropSysFreeString(LPOLESTR str);

#include <atlbase.h>
#include <atlcom.h>
#include <atlstr.h> // For CString
// #ifdef _ATL_STATIC_REGISTRY
// #include <statreg.h>
// #endif


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
/**
 ********/

EXTERN_C VOID ErrOut(HRESULT hr, DWORD dwLastError, PCWSTR pszWhere);

#include <initguid.h>
DEFINE_GUID(CLSID_ConsoleProperties, 0xD2942F8E, 0x478E, 0x41D3, 0x87, 0x0A, 0x35, 0xA1, 0x62, 0x38, 0xF4, 0xEE);
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
LONG g_ModuleRefCnt = 0;


EXTERN_C HRESULT
ConLnkReadSettings2(
    _Inout_ PCONSOLE_STATE_INFO ConsoleInfo,
    _In_ IShellLinkW* psl);

EXTERN_C HRESULT
ConLnkWriteSettings2(
    _In_ PCONSOLE_STATE_INFO ConsoleInfo,
    _Inout_ IShellLinkW* psl,
    _Inout_ IPersistFile* ppf);


// LinkConsolePagesSave(*linkdata)
HRESULT
LinkConsolePagesSave(
    _In_ PCONSOLE_PROPS_CTX pConProps,
    _In_ IShellLinkW* psl)
{
    // TODO: Put the UpdateValues stuff there too?
    CComPtr<IPersistFile> ppf;
    HRESULT hr = psl->QueryInterface(IID_IPersistFile, (PVOID*)&ppf);
    if (SUCCEEDED(hr))
        hr = ConLnkWriteSettings2(pConProps->ConInfo, psl, ppf);
    return hr;
}

/*
 * References:
 * https://learn.microsoft.com/en-us/windows/win32/shell/how-to-register-and-implement-a-property-sheet-handler-for-a-file-type
 * https://www.codeproject.com/KB/shell/shellextguide5.aspx
 */
class ATL_NO_VTABLE // DECLSPEC_NOVTABLE
DECLSPEC_UUID("D2942F8E-478E-41D3-870A-35A16238F4EE")
CConsoleProps :
    public CComCoClass<CConsoleProps, &CLSID_ConsoleProperties>,
    public CComObjectRootEx</*CComMultiThreadModelNoCS*/CComSingleThreadModel>,
    public IShellExtInit,
    public IShellPropSheetExt
{
    BEGIN_COM_MAP(CConsoleProps)
        COM_INTERFACE_ENTRY_IID(IID_IShellExtInit, IShellExtInit)
        COM_INTERFACE_ENTRY_IID(IID_IShellPropSheetExt, IShellPropSheetExt)
    END_COM_MAP()

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
                ACDBG(L"Couldn't delete CPL value 'Console', error %d\n", lRes);
        }

        return ATL::_pAtlModule->UpdateRegistryFromResource(IDR_CONSOLE, bRegister, regMapEntries);
    }

protected:
    CONSOLE_PROPS_CTX m_ConProps = {0};
    CString m_LinkPath;
    CComPtr<IShellLinkW> m_psl;
    LONG m_cRefs = 0;

public:
    HRESULT
    InitLink(
        _In_ PCWSTR Filename);

    CConsoleProps()
    {
MessageBoxW(NULL, L"CConsoleProps()", L"Info", MB_OK);
    }

    ~CConsoleProps()
    {
        /* Cleanup */
MessageBoxW(NULL, L"~CConsoleProps()", L"Info", MB_OK);
        HeapFree(GetProcessHeap(), 0, m_ConProps.ConInfo);
        m_ConProps.ConInfo = NULL;
    }

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
        _Inout_ PROPSHEETPAGEW* ppsp) // LPPROPSHEETPAGE
    {
        CConsoleProps* This =
            (CConsoleProps*)CONTAINING_RECORD((PVOID)ppsp->lParam, CConsoleProps, m_ConProps);
        ATLASSERT(&This->m_ConProps == ppsp->lParam);

        switch (uMsg)
        {
            case PSPCB_ADDREF:
            {
                LONG oldModRefCnt = g_ModuleRefCnt;
                LONG oldCRefs = This->m_cRefs;

                LONG newModRefCnt = InterlockedIncrement(&g_ModuleRefCnt);
                LONG newCRefs     = InterlockedIncrement(&This->m_cRefs);
CString msg;
msg.Format(L"old ModRefCnt: %d ; new ModRefCnt: %d\nold m_cRefs: %d ; new m_cRefs: %d",
           oldModRefCnt, newModRefCnt, oldCRefs, newCRefs);
MessageBoxW(hWnd, (PCWSTR)msg, L"Info", MB_OK);
                break;
            }

            case PSPCB_RELEASE:
            {
                if (InterlockedDecrement(&This->m_cRefs) == 0)
                {
                    /* Only persist settings if they have changed */
                    PCONSOLE_PROPS_CTX pConProps = &This->m_ConProps;
                    if (pConProps->UpdateValues)
                    {
MessageBoxW(hWnd, L"SaveConsoleSettings", L"Info", MB_OK);
                        SaveConsoleSettings(pConProps, hWnd);
                    }

                    /* Disable the font preview */
                    ResetFontPreview(&pConProps->FontPreview);

                    /* Finally, release and destroy the CConsoleProps object */
                    This->Release();
                }
                if (InterlockedDecrement(&g_ModuleRefCnt) == 0)
                {
MessageBoxW(hWnd, L"UninitializeConsoleState", L"Info", MB_OK);
                    UninitializeConsolePropsState();
                }
                break;
            }

            case PSPCB_CREATE:
                break;
        }

        return 1;
    }

    static BOOL
    SaveConsoleSettings(
        _In_ PCONSOLE_PROPS_CTX pConProps,
        _In_opt_ HWND hWndParent)
    {
        CConsoleProps* This =
            (CConsoleProps*)CONTAINING_RECORD(pConProps, CConsoleProps, m_ConProps);
        ATLASSERT(&This->m_ConProps == ppsp->lParam);

        if (!pConProps->UpdateValues) // !SaveConsoleInfo
            return TRUE;

        HRESULT hr = LinkConsolePagesSave(pConProps, This->m_psl);
        if (!SUCCEEDED(hr))
        {
            WCHAR szTitle[260];
            WCHAR szMessage[260];
            /// IDS_LINKCANTSAVE 4217,   "Unable to save changes to '%2!ls!'.\n\n%1!ls!"
            StringCchCopyW(szTitle, _countof(szTitle), L"Unable to save changes to '%2!ls!'.\n\n%1!ls!");
            // LoadStringW(g_hModule, IDS_ERROR_SHORTCUT, szTitle, _countof(szTitle));
            StringCchPrintfW(szMessage, _countof(szMessage), szTitle, hr, This->m_LinkPath);
            LoadStringW(g_hModule, IDS_ERROR_SHORTCUT_TITLE, szTitle, _countof(szTitle));
            MessageBoxW(hWndParent, szMessage, szTitle, MB_ICONERROR | MB_OK | MB_SETFOREGROUND);
        }
        pConProps->UpdateValues = FALSE; // SaveConsoleInfo
        return SUCCEEDED(hr);
    }

    /**
     * @brief
     * Save the modified console properties into the link.
     *
     * @return
     * TRUE if changes should be saved, FALSE if not.
     **/
    static BOOL
    ApplyLinkConsoleInfo(
        _In_opt_ HWND hDlg,
        _In_ PCONSOLE_PROPS_CTX pConProps,
        _In_ BOOL bApplyNow)
    {
        UNREFERENCED_PARAMETER(bApplyNow);
        if (!bApplyNow)
            return TRUE; // Defer saving to when the whole property page is unloaded.
        pConProps->UpdateValues = TRUE;
        return SaveConsoleSettings(pConProps, hDlg);
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

        if (!PopulatePropSheetPageArray(psp, _countof(psp), (LPARAM)&m_ConProps, PropSheetPageProc))
            return E_FAIL;

        for (UINT i = 0; i < _countof(psp); ++i)
        {
            /* Don't use pcRefParent, because we want to have control
             * of what to do when unloading the property pages.
             * REACTOS-specific: Wine's comctl32 still doesn't implement
             * support for it! */
            // psp[i].dwFlags    |= PSP_USEREFPARENT;
            // psp[i].pcRefParent = (UINT*)&g_ModuleRefCnt;

            HPROPSHEETPAGE hPage = CreatePropertySheetPage(&psp[i]);
            if (!hPage)
                return E_FAIL;
            if (!pfnAddPage(hPage, lParam))
            {
                DestroyPropertySheetPage(hPage);
                return E_FAIL;
            }
        }

        /* Initialize the current font preview */
        RefreshFontPreview(&m_ConProps.FontPreview, m_ConProps.ConInfo);
        return S_OK;
    }

    STDMETHODIMP ReplacePage(
        _In_ EXPPS uPageID,
        _In_ LPFNSVADDPROPSHEETPAGE pfnReplaceWith,
        _In_ LPARAM lParam) override
    {
        return E_NOTIMPL;
    }
};

OBJECT_ENTRY_AUTO(CLSID_ConsoleProperties, CConsoleProps)


#include <shellutils.h>
static HRESULT
GetLnkAndExe(
    _In_ PCWSTR pszLnk,
    _Out_ PWSTR pszExe,
    _In_ size_t cchSize,
    _COM_Outptr_ IShellLinkW** ppsl)
{
    HRESULT hr;

    CComPtr<IShellLinkW> psl;
    hr = SHCoCreateInstance(NULL, &CLSID_ShellLink, NULL, IID_PPV_ARG(IShellLinkW, &psl));
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    CComPtr<IPersistFile> ppf;
    hr = psl->QueryInterface(IID_PPV_ARG(IPersistFile, &ppf));
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    hr = ppf->Load(pszLnk, STGM_READ);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;
    hr = psl->Resolve(NULL, SLR_NO_UI | SLR_NOUPDATE | SLR_NOSEARCH /*| SLR_NOTRACK*/);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    hr = psl->GetPath(pszExe, cchSize, NULL, 0);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    *ppsl = psl;
    (*ppsl)->AddRef();
    return hr;
}

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
            ACDBG(L"Expanded '%s' => '%s'\n", String, (PCWSTR)ExpandedString);
        }
        else
        {
            ExpandedString.ReleaseBufferSetLength(0);
            ExpandedString = String;
            ACDBG(L"Failed during expansion '%s'\n", String);
        }
    }
    else
    {
        ACDBG(L"Failed to expand '%s'\n", String);
        ExpandedString = String;
    }

    return ExpandedString;
}

HRESULT
CConsoleProps::InitLink(
    _In_ PCWSTR Filename)
{
    CString ExpandedFilename = ExpandEnvironmentStrings(Filename);

    // PCWSTR pwszExt = PathFindExtensionW(ExpandedFilename);
    PCWSTR pwszExt = wcsrchr(ExpandedFilename, L'.');
    if (!pwszExt || (_wcsicmp(pwszExt, L".lnk") != 0))
    {
        /* Not a link file, fail */
        ACDBG(L"Not a link file: '%s'\n", (PCWSTR)ExpandedFilename);
        return E_FAIL;
    }

    CComPtr<IShellLinkW> psl;
    WCHAR Buffer[MAX_PATH]; // Link target
    HRESULT hr = GetLnkAndExe(ExpandedFilename, Buffer, _countof(Buffer), &psl);
    if (!SUCCEEDED(hr))
    {
        ACDBG(L"Failed to read link target from: '%s'\n", (PCWSTR)ExpandedFilename);
        return hr;
    }
    if (!_wcsicmp(Buffer, ExpandedFilename))
    {
        ACDBG(L"Link redirects to itself: '%s'\n", (PCWSTR)ExpandedFilename);
        return E_FAIL;
    }

CString msg;
msg = L"Link target: '" + CString(Buffer);
MessageBoxW(NULL, (LPCWSTR)msg, L"Info", MB_OK);

    /* Check whether the target is a Win32 console program,
     * or a .bat or .cmd (that open with CMD.EXE) */
    DWORD dwExeType;
    dwExeType = (DWORD)::SHGetFileInfoW(Buffer, 0, NULL, 0, SHGFI_EXETYPE);
    // MAKELONG('EP', 0)
    if (dwExeType != MAKELONG(MAKEWORD('P','E'), 0)) // If it's not Win32 .exe console or .bat or .cmd
    {
        //// MAKELONG(MAKEWORD('M','Z'), 0)
        // Note: MAKELONG('ZM', 0) would be for an MS-DOS .exe or .com file.
        ACDBG(L"Target not recognized as Win32 exe or bat or cmd: '%s', %lu\n",
              Buffer, dwExeType);
        if (!::GetBinaryTypeW(Buffer, &dwExeType) ||
            ((dwExeType != SCS_DOS_BINARY  ) && (dwExeType != SCS_PIF_BINARY) &&
             (dwExeType != SCS_POSIX_BINARY) && (dwExeType != SCS_OS216_BINARY)))
        {
            ACDBG(L"Target not recognized as supported binary: '%s', %lu\n",
                  Buffer, dwExeType);
            return E_FAIL;
        }
    }

///// We've ended shell-specific initialization.
///// Now continue with standard console stuff.

    /* Allocate a local buffer to hold console information */
    m_ConProps.ConInfo =
        (PCONSOLE_STATE_INFO)HeapAlloc(GetProcessHeap(),
                                       HEAP_ZERO_MEMORY,
                                       sizeof(CONSOLE_STATE_INFO));
    if (!m_ConProps.ConInfo)
        return E_OUTOFMEMORY;

    /* Load the default settings */
    ConCfgGetDefaultSettings(m_ConProps.ConInfo);

    /* And override them with those in the link */
msg = L"Loading from: " + ExpandedFilename;
MessageBoxW(NULL, (LPCWSTR)msg, L"Info", MB_OK);
    hr = ConLnkReadSettings2(m_ConProps.ConInfo, psl/*ExpandedFilename*/);
    if (!SUCCEEDED(hr))
    {
msg.Format(L"Failed to load from '%s', error 0x%p", ExpandedFilename, hr);
MessageBoxW(NULL, (LPCWSTR)msg, L"Info", MB_OK);
        // TODO: Load instead the registry parameters for the target.
        // If they don't exist, then load the default settings.
        // FIXME: Specify the shortcut path in ConInfo->ConsoleTitle ??
        // ConCfgReadUserSettings(m_ConProps.ConInfo, FALSE);
    }
    m_ConProps.UpdateValues = FALSE;
    m_ConProps.ApplyInfo = ApplyLinkConsoleInfo;
    m_LinkPath = ExpandedFilename;
    m_psl = psl;

    /*
     * Add a reference to ourselves, so that the CConsoleProps object
     * does not go away once the shell has finished Initialize()'d and
     * AddPages()'d us.
     * NOTE: The Release() call is in PropSheetPageProc(), PSPCB_RELEASE case.
     */
    AddRef();

    if (g_ModuleRefCnt == 0)
        InitializeConsolePropsState();

    return S_OK;
}

STDMETHODIMP
CConsoleProps::Initialize(
    _In_ PCIDLIST_ABSOLUTE pidlFolder,  // LPCITEMIDLIST
    _In_ LPDATAOBJECT pdtobj,           // pDataObj
    _In_ HKEY hkeyProgID)
{
    UNREFERENCED_PARAMETER(pidlFolder);
    UNREFERENCED_PARAMETER(hkeyProgID);

    CString szFile;

    FORMATETC etc = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM stg;

    if (IsDebuggerPresent())
        __debugbreak();

    HRESULT hr = pdtobj->GetData(&etc, &stg);
    if (FAILED(hr))
        return E_INVALIDARG;

    hr = E_FAIL;
    HDROP hdrop = (HDROP)::GlobalLock(stg.hGlobal);
    if (!hdrop)
        goto Quit;

    /* The properties sheet is for one file only */
    UINT uNumFiles = ::DragQueryFileW(hdrop, 0xFFFFFFFF, NULL, 0);
    if (uNumFiles != 1)
    {
        ACDBG(L"Invalid number of files: %d\n", uNumFiles);
        goto Quit;
    }

    DWORD dwRequired = ::DragQueryFileW(hdrop, 0, NULL, 0);
    if (dwRequired > 0) // Doesn't count terminating NUL.
    {
        ++dwRequired; // Count the terminating NUL.
        LPWSTR Buffer = szFile.GetBuffer(dwRequired);
        DWORD dwReturned = ::DragQueryFileW(hdrop, 0, Buffer, dwRequired);
        if (dwRequired == ++dwReturned)
        {
            szFile.ReleaseBufferSetLength(dwReturned - 1);
            hr = S_OK;
        }
    }
    if (!SUCCEEDED(hr))
        ACDBG(L"Failed to query the file.\n");

Quit:
    if (hdrop)
        ::GlobalUnlock(stg.hGlobal);
    ::ReleaseStgMedium(&stg);
    if (!SUCCEEDED(hr))
        return hr;

    /* Initialize with the link file and determine
     * whether the property pages should be created. */
    return InitLink(szFile);
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

const LPCWSTR REG_SHELLEXT_APPROVED =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved";

STDAPI DllRegisterServer(VOID)
{
    HRESULT hr = gModule.DllRegisterServer(FALSE);
    if (!SUCCEEDED(hr))
        return hr;

    /*
     * Optionally register the extension in the list of "approved" shell
     * extensions (NT-specific). It is advisable to do it, because the
     * REST_ENFORCESHELLEXTSECURITY system policy can be set to prevent
     * extensions from being loaded if they are not on the approved list.
     */
    CRegKey rk;
    LONG lRes = rk.Open(HKEY_LOCAL_MACHINE, REG_SHELLEXT_APPROVED, KEY_WRITE);
    if (lRes == ERROR_SUCCESS)
    {
        CComBSTR CLSID_ConsoleProperties_STR(CLSID_ConsoleProperties);
        lRes = rk.SetStringValue(CLSID_ConsoleProperties_STR, L"Console");
    }
    if (lRes != ERROR_SUCCESS)
        ACDBG(L"Couldn't add extension to Approved Shell Extensions list, error %d\n", lRes);

    // Ignore the error and hope for the best...
    return S_OK; // HRESULT_FROM_WIN32(lRes);
}

STDAPI DllUnregisterServer(VOID)
{
    /* Unconditionally unregister from the list of "approved" shell extensions */
    CRegKey rk;
    LONG lRes = rk.Open(HKEY_LOCAL_MACHINE, REG_SHELLEXT_APPROVED, KEY_WRITE);
    if (lRes == ERROR_SUCCESS)
    {
        CComBSTR CLSID_ConsoleProperties_STR(CLSID_ConsoleProperties);
        lRes = rk.DeleteValue(CLSID_ConsoleProperties_STR);
    }
    if (lRes != ERROR_SUCCESS)
        ACDBG(L"Couldn't delete extension from Approved Shell Extensions list, error %d\n", lRes);

    /* Do the COM un-registration proper */
    return gModule.DllUnregisterServer(FALSE);
}

/* EOF */
