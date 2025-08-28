/*
 * PROJECT:     ReactOS Tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Tests for _SEH2_* support when application is started by NTRTL.
 * COPYRIGHT:   Copyright 2025 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

/* CONFIG ********************************************************************/

/*
 * Enable this if you want an "interactive" GUI test.
 * This would provide an UI environment similar to what Winlogon has.
 */
// #define INTERACTIVE_TEST

/*
 * Enable this if you want to mark the test process as a critical process
 * (would BSoD the system if the process crashes while running).
 */
// #define CRITICAL_PROCESS_TEST

/* INCLUDES ******************************************************************/

/* PSDK/NDK Headers */
#define WIN32_NO_STATUS
#define WIN32_LEAN_AND_MEAN
#include <windef.h>
#include <winbase.h>

#ifdef INTERACTIVE_TEST
#include <wingdi.h>
#include <winuser.h>
#endif

#include <ndk/rtlfuncs.h> // For DbgPrint()

/* PSEH for SEH Support */
#include <pseh/pseh2.h>

#define ERR(fmt, ...)   DbgPrint(fmt, ##__VA_ARGS__)

#ifdef INTERACTIVE_TEST
BOOL
WINAPI
SetWindowStationUser(
    IN HWINSTA hWindowStation,
    IN PLUID pluid,
    IN PSID psid OPTIONAL,
    IN DWORD size);

typedef struct _WLSESSION
{
    HWINSTA InteractiveWindowStation;
    HDESK ApplicationDesktop;
} WLSESSION, *PWLSESSION;

static LUID LuidNone = {0, 0};
#endif

/* FUNCTIONS *****************************************************************/

#ifdef INTERACTIVE_TEST
BOOL
CreateWindowStationAndDesktops(
    _Inout_ PWLSESSION Session)
{
    BOOL ret = FALSE;

    /* Create the interactive window station */
    Session->InteractiveWindowStation =
        CreateWindowStationW(L"WinSta0",
                             0,
                             MAXIMUM_ALLOWED,
                             NULL);
    if (!Session->InteractiveWindowStation)
    {
        ERR("WL: Failed to create window station (%lu)\n", GetLastError());
        goto cleanup;
    }

    if (!SetProcessWindowStation(Session->InteractiveWindowStation))
    {
        ERR("WL: SetProcessWindowStation() failed (error %lu)\n", GetLastError());
        goto cleanup;
    }

    /* Create the application desktop */
    Session->ApplicationDesktop =
        CreateDesktopW(L"Default",
                       NULL,
                       NULL,
                       0, /* FIXME: Add DF_ALLOWOTHERACCOUNTHOOK flag? */
                       MAXIMUM_ALLOWED,
                       NULL);
    if (!Session->ApplicationDesktop)
    {
        ERR("WL: Failed to create Default desktop (%lu)\n", GetLastError());
        goto cleanup;
    }

    /* Switch to Default desktop */
    if (!SetThreadDesktop(Session->ApplicationDesktop) ||
        !SwitchDesktop(Session->ApplicationDesktop))
    {
        ERR("WL: Cannot switch to Default desktop (%lu)\n", GetLastError());
        goto cleanup;
    }

    SetWindowStationUser(Session->InteractiveWindowStation,
                         &LuidNone, NULL, 0);

    ret = TRUE;

cleanup:
    if (!ret)
    {
        if (Session->ApplicationDesktop)
        {
            CloseDesktop(Session->ApplicationDesktop);
            Session->ApplicationDesktop = NULL;
        }
        if (Session->InteractiveWindowStation)
        {
            CloseWindowStation(Session->InteractiveWindowStation);
            Session->InteractiveWindowStation = NULL;
        }
    }

    return ret;
}
#endif // INTERACTIVE_TEST

LONG
WLUnhandledExceptionFilter(
    _In_ PCSTR File,
    _In_ ULONG Line,
    _In_ ULONG ExceptionCode,
    _In_ PEXCEPTION_POINTERS ExceptionInfo)
{
    PEXCEPTION_RECORD Record = ExceptionInfo->ExceptionRecord;

    DbgPrint("(%s:%lu) **** WL: Got exception 0x%lx (0x%lx) flags 0x%lx, at 0x%p\n",
             File, Line,
             ExceptionCode, Record->ExceptionCode,
             Record->ExceptionFlags,
             Record->ExceptionAddress);
    for (DWORD n = 0; n < min(EXCEPTION_MAXIMUM_PARAMETERS, Record->NumberParameters); ++n)
    {
        DbgPrint("       Info[%u]: 0x%p\n", n, (PVOID)Record->ExceptionInformation[n]);
    }

    //return EXCEPTION_EXECUTE_HANDLER;
    //return UnhandledExceptionFilter(ExceptionInfo);
    return RtlUnhandledExceptionFilter(ExceptionInfo);
}

LONG
WINAPI
WLGlobalUnhandledExceptionFilter(
    _In_ PEXCEPTION_POINTERS ExceptionInfo)
{
    PEXCEPTION_RECORD Record = ExceptionInfo->ExceptionRecord;
    DbgPrint("(%s:%lu) **** WL: Got global unhandled exception 0x%lx - Invoking our filter\n",
             __RELFILE__, __LINE__);
    return WLUnhandledExceptionFilter(__RELFILE__, __LINE__, Record->ExceptionCode, ExceptionInfo);
}

/**
 * @brief
 * Mock OutputDebugStringA() to test its SEH usage pattern.
 **/
VOID
WINAPI
MyOutputDebugStringA(
    _In_ LPCSTR OutputString)
{
DbgPrint("ODS(0x%p) -> '%s'\n", OutputString, OutputString);
    _SEH2_TRY
    {
        /* Throw a random exception to jump to the _SEH2_EXCEPT block below */
        RaiseException(0xDEADBEEF, 0, 0, NULL);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
DbgPrint("%s - Entering _SEH2_TRY(1)\n", __FUNCTION__);
        _SEH2_TRY
        {
DbgPrint("%s - Inside _SEH2_TRY(1)\n", __FUNCTION__);

DbgPrint("%s - Entering _SEH2_TRY(2)\n", __FUNCTION__);
            _SEH2_TRY
            {
DbgPrint("%s - Inside _SEH2_TRY(2)\n", __FUNCTION__);
                /* Send the first max 512 bytes to the kernel debugger */
                DbgPrint("%s", OutputString);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
DbgPrint("%s - Inside and exiting _SEH2_EXCEPT(2), got exception 0x%lx\n", __FUNCTION__, _SEH2_GetExceptionCode());
            }
            _SEH2_END;
DbgPrint("%s - Exited _SEH2_EXCEPT(2)\n", __FUNCTION__);
        }
        _SEH2_FINALLY
        {
DbgPrint("%s - Inside and exiting _SEH2_FINALLY(1)\n", __FUNCTION__);
        }
        _SEH2_END;
DbgPrint("%s - Exited _SEH2_TRY(1)/_SEH2_FINALLY(1)\n", __FUNCTION__);
    }
    _SEH2_END;
}

int
WINAPI
WinMain(
    IN HINSTANCE hInstance,
    IN HINSTANCE hPrevInstance,
    IN LPSTR lpCmdLine,
    IN int nShowCmd)
{
#ifdef INTERACTIVE_TEST
    WLSESSION Session;
#endif
#ifdef CRITICAL_PROCESS_TEST
    BOOLEAN OldCritProcess, OldCritThread;
#endif

    LPTOP_LEVEL_EXCEPTION_FILTER pOrgTopLevelExceptionFilter;

    UNREFERENCED_PARAMETER(hInstance);
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nShowCmd);

_SEH2_TRY {
    /* Install a custom global exception filter */
    ///*pOrgTopLevelExceptionFilter =*/ RtlSetUnhandledExceptionFilter((PRTLP_UNHANDLED_EXCEPTION_FILTER)WLGlobalUnhandledExceptionFilter);
    /*pOrgTopLevelExceptionFilter =*/ RtlSetUnhandledExceptionFilter((PRTLP_UNHANDLED_EXCEPTION_FILTER)UnhandledExceptionFilter);
    pOrgTopLevelExceptionFilter = SetUnhandledExceptionFilter(WLGlobalUnhandledExceptionFilter);
    //pOrgTopLevelExceptionFilter = SetUnhandledExceptionFilter(UnhandledExceptionFilter);
    DbgPrint("Replaced Original unhandled exception filter: 0x%p\n", pOrgTopLevelExceptionFilter);

__debugbreak();

#ifdef CRITICAL_PROCESS_TEST
    /* Make us critical -- Allows also getting the stack trace to where
     * the code crashes, when the program is killed by an exception. */
    RtlSetProcessIsCritical(TRUE, &OldCritProcess, FALSE);
    RtlSetThreadIsCritical(TRUE, &OldCritThread, FALSE);
#endif

#ifdef INTERACTIVE_TEST
    if (!RegisterLogonProcess(GetCurrentProcessId(), TRUE))
        DbgPrint("WL: Could not register logon process\n");

    if (!CreateWindowStationAndDesktops(&Session))
        DbgPrint("WL: Could not create window station and desktops\n");

    MessageBoxW(NULL, L"OutputDebugStringA with SEH test", L"Test", MB_OK);
#else
    DbgPrint("OutputDebugStringA with SEH test\n");
#endif

    /*** This call will work... ***/
    _SEH2_TRY
    {
    MyOutputDebugStringA("WL: WinMain(1)\n");
    }
    _SEH2_EXCEPT(WLUnhandledExceptionFilter(__RELFILE__, __LINE__, _SEH2_GetExceptionCode(), _SEH2_GetExceptionInformation()))
    {
    DbgPrint("(%s:%lu) **** WL: Exception 0x%lx handled!\n", __RELFILE__, __LINE__, _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    /*** But this call won't!! ***/
    MyOutputDebugStringA("WL: WinMain(2)\n");

    /*** This call would work... but we won't get there because previous one crashed ***/
    _SEH2_TRY
    {
    MyOutputDebugStringA("WL: WinMain(3)\n");
    }
    _SEH2_EXCEPT(WLUnhandledExceptionFilter(__RELFILE__, __LINE__, _SEH2_GetExceptionCode(), _SEH2_GetExceptionInformation()))
    {
    DbgPrint("(%s:%lu) **** WL: Exception 0x%lx handled!\n", __RELFILE__, __LINE__, _SEH2_GetExceptionCode());
    }
    _SEH2_END;

#ifdef INTERACTIVE_TEST
    MessageBoxW(NULL, L"Test succeeded!", L"Test", MB_OK);
    if (Session.ApplicationDesktop)
        CloseDesktop(Session.ApplicationDesktop);
    if (Session.InteractiveWindowStation)
        CloseWindowStation(Session.InteractiveWindowStation);
#else
    DbgPrint("Test succeeded!\n");
#endif

#ifdef CRITICAL_PROCESS_TEST
    /* Not critical anymore */
    RtlSetThreadIsCritical(OldCritThread, NULL, FALSE);
    RtlSetProcessIsCritical(OldCritProcess, NULL, FALSE);
#endif
} _SEH2_EXCEPT(WLGlobalUnhandledExceptionFilter(_SEH2_GetExceptionInformation())) {} _SEH2_END;
    return 0;
}
