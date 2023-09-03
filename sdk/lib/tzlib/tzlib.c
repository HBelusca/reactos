/*
 * PROJECT:     ReactOS TimeZone Utilities Library
 * LICENSE:     GPL-2.0 (https://spdx.org/licenses/GPL-2.0)
 * PURPOSE:     Time-zone utility wrappers around Win32 functions.
 * COPYRIGHT:   Copyright 2004-2005 Eric Kohl
 *              Copyright 2016 Carlo Bramini
 *              Copyright 2020-2023 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

/**
 * @file    tzlib.c
 * @ingroup TzLib
 *
 * @defgroup TzLib  ReactOS TimeZone Utilities Library
 *
 * @brief   Provides time-zone utility wrappers around Win32 functions,
 *          used by different ReactOS modules (timedate.cpl, syssetup.dll)
 **/

#include <stdlib.h>
#include <windef.h>
#include <winbase.h>
#include <winreg.h>

#include "tzlib.h"

/**
 * @brief
 * XXXX
 *
 * @param[in,out]   pIndex
 * xxxxx
 *
 * @return
 * TRUE in case of success, FALSE otherwise.
 **/
BOOL
GetTimeZoneListIndex(
    _Inout_ PULONG pIndex)
{
    LONG lError;
    HKEY hKey;
    DWORD dwType;
    DWORD dwValueSize;
    DWORD Length;
    PWSTR Buffer;
    PWSTR Ptr, End;
    BOOL bFound = FALSE;
    ULONG iLanguageID;
    WCHAR szLanguageIdString[9];

    if (*pIndex == -1)
    {
        *pIndex = 85; /* fallback to GMT time zone */

        lError = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                               L"SYSTEM\\CurrentControlSet\\Control\\NLS\\Language",
                               0,
                               KEY_QUERY_VALUE,
                               &hKey);
        if (lError != ERROR_SUCCESS)
        {
            return FALSE;
        }

        dwValueSize = sizeof(szLanguageIdString);
        lError = RegQueryValueExW(hKey,
                                  L"Default",
                                  NULL,
                                  NULL,
                                  (PBYTE)szLanguageIdString,
                                  &dwValueSize);
        if (lError != ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
            return FALSE;
        }

        iLanguageID = wcstoul(szLanguageIdString, NULL, 16);
        RegCloseKey(hKey);
    }
    else
    {
        iLanguageID = *pIndex;
    }

    lError = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                           L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones",
                           0,
                           KEY_QUERY_VALUE,
                           &hKey);
    if (lError != ERROR_SUCCESS)
    {
        return FALSE;
    }

    dwValueSize = 0;
    lError = RegQueryValueExW(hKey,
                              L"IndexMapping",
                              NULL,
                              &dwType,
                              NULL,
                              &dwValueSize);
    if ((lError != ERROR_SUCCESS) || (dwType != REG_MULTI_SZ))
    {
        RegCloseKey(hKey);
        return FALSE;
    }

    Buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwValueSize);
    if (Buffer == NULL)
    {
        RegCloseKey(hKey);
        return FALSE;
    }

    lError = RegQueryValueExW(hKey,
                              L"IndexMapping",
                              NULL,
                              &dwType,
                              (PBYTE)Buffer,
                              &dwValueSize);

    RegCloseKey(hKey);

    if ((lError != ERROR_SUCCESS) || (dwType != REG_MULTI_SZ))
    {
        HeapFree(GetProcessHeap(), 0, Buffer);
        return FALSE;
    }

    Ptr = Buffer;
    while (*Ptr != 0)
    {
        Length = wcslen(Ptr);
        if (wcstoul(Ptr, NULL, 16) == iLanguageID)
            bFound = TRUE;

        Ptr = Ptr + Length + 1;
        if (*Ptr == 0)
            break;

        if (bFound)
        {
            *pIndex = wcstoul(Ptr, &End, 10);
            HeapFree(GetProcessHeap(), 0, Buffer);
            return TRUE;
        }

        Length = wcslen(Ptr);
        Ptr = Ptr + Length + 1;
    }

    HeapFree(GetProcessHeap(), 0, Buffer);
    return FALSE;
}

/**
 * @brief
 * Retrieves time-zone data from the registry.
 *
 * @param[in]   hZoneKey
 * A handle to an opened registry sub-key of
 * HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Time Zones,
 * that contains data for a particular time-zone.
 *
 * @param[out]  Index
 * Optional pointer to a variable that receives the corresponding
 * time-zone "Index" registry value.
 *
 * @param[out]  TimeZoneInfo
 * Pointer to a REG_TZI_FORMAT buffer that receives the binary time-zone
 * data, as stored in the "TZI" registry value.
 *
 * @param[out]  Description
 * Optional pointer to a character buffer that receives the description
 * for the time-zone.
 *
 * @param[in,out]   DescriptionSize
 * On input, specifies the initial size in bytes of the @p Description buffer.
 * On output, retrieves the actual string size in bytes stored in the buffer.
 * This parameter can be NULL only if @p Description is NULL.
 *
 * @param[out]  StandardName
 * Optional pointer to a character buffer that receives the description
 * for the time-zone's standard time.
 *
 * @param[in,out]   StandardNameSize
 * On input, specifies the initial size in bytes of the @p StandardNameSize buffer.
 * On output, retrieves the actual string size in bytes stored in the buffer.
 * This parameter can be NULL only if @p StandardNameSize is NULL.
 *
 * @param[out]  DaylightName
 * Optional pointer to a character buffer that receives the description
 * for the time-zone's daylight saving time.
 *
 * @param[in,out]   DaylightNameSize
 * On input, specifies the initial size in bytes of the @p DaylightName buffer.
 * On output, retrieves the actual string size in bytes stored in the buffer.
 * This parameter can be NULL only if @p DaylightName is NULL.
 *
 * @return
 * ERROR_SUCCESS if success, or a Win32 error code in case of failure.
 **/
LONG
QueryTimeZoneData(
    _In_ HKEY hZoneKey,
    _Out_opt_ PULONG Index,
    _Out_ PREG_TZI_FORMAT TimeZoneInfo,
    _Out_writes_to_opt_(*DescriptionSize, *DescriptionSize)
        PWCHAR Description,
    _Inout_opt_ PULONG DescriptionSize,
    _Out_writes_to_opt_(*StandardNameSize, *StandardNameSize)
        PWCHAR StandardName,
    _Inout_opt_ PULONG StandardNameSize,
    _Out_writes_to_opt_(*DaylightNameSize, *DaylightNameSize)
        PWCHAR DaylightName,
    _Inout_opt_ PULONG DaylightNameSize)
{
    LONG lError;
    DWORD dwValueSize;

    if (Index)
    {
        dwValueSize = sizeof(*Index);
        lError = RegQueryValueExW(hZoneKey,
                                  L"Index",
                                  NULL,
                                  NULL,
                                  (PBYTE)Index,
                                  &dwValueSize);
        if (lError != ERROR_SUCCESS)
            *Index = 0;
    }

    /* The time zone information structure is mandatory for a valid time zone */
    dwValueSize = sizeof(*TimeZoneInfo);
    lError = RegQueryValueExW(hZoneKey,
                              L"TZI",
                              NULL,
                              NULL,
                              (PBYTE)TimeZoneInfo,
                              &dwValueSize);
    if (lError != ERROR_SUCCESS)
        return lError;

    if (Description && DescriptionSize && *DescriptionSize > 0)
    {
        lError = RegQueryValueExW(hZoneKey,
                                  L"Display",
                                  NULL,
                                  NULL,
                                  (PBYTE)Description,
                                  DescriptionSize);
        if (lError != ERROR_SUCCESS)
            *Description = 0;
    }

    if (StandardName && StandardNameSize && *StandardNameSize > 0)
    {
        lError = RegQueryValueExW(hZoneKey,
                                  L"Std",
                                  NULL,
                                  NULL,
                                  (PBYTE)StandardName,
                                  StandardNameSize);
        if (lError != ERROR_SUCCESS)
            *StandardName = 0;
    }

    if (DaylightName && DaylightNameSize && *DaylightNameSize > 0)
    {
        lError = RegQueryValueExW(hZoneKey,
                                  L"Dlt",
                                  NULL,
                                  NULL,
                                  (PBYTE)DaylightName,
                                  DaylightNameSize);
        if (lError != ERROR_SUCCESS)
            *DaylightName = 0;
    }

    return ERROR_SUCCESS;
}

/**
 * @brief
 * Enumerates time-zone entries stored in the registry. For each,
 * a user-provided callback is called with an optional context.
 *
 * @param[in]   Callback
 * A user-provided callback function, invoked for each time-zone entry.
 *
 * @param[in]   Context
 * Optional context passed to the callback function when it is invoked.
 *
 * @return  None.
 *
 * @note
 * Very similar to the EnumDynamicTimeZoneInformation() function
 * introduced in Windows 8.
 **/
VOID
EnumerateTimeZoneList(
    _In_ PENUM_TIMEZONE_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    LONG lError;
    HKEY hZonesKey;
    HKEY hZoneKey;
    DWORD dwIndex;
    DWORD dwNameSize;
    WCHAR szKeyName[256];

    /* Open the registry key containing the list of time zones */
    lError = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                           L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones",
                           0,
                           KEY_ENUMERATE_SUB_KEYS,
                           &hZonesKey);
    if (lError != ERROR_SUCCESS)
        return;

    /* Enumerate it */
    for (dwIndex = 0; ; dwIndex++)
    {
        dwNameSize = sizeof(szKeyName);
        lError = RegEnumKeyExW(hZonesKey,
                               dwIndex,
                               szKeyName,
                               &dwNameSize,
                               NULL,
                               NULL,
                               NULL,
                               NULL);
        // if (lError != ERROR_SUCCESS && lError != ERROR_MORE_DATA)
        if (lError == ERROR_NO_MORE_ITEMS)
            break;

        /* Open the time zone sub-key */
        if (RegOpenKeyExW(hZonesKey,
                          szKeyName,
                          0,
                          KEY_QUERY_VALUE,
                          &hZoneKey))
        {
            /* We failed, continue with another sub-key */
            continue;
        }

        /* Call the user-provided callback */
        lError = Callback(hZoneKey, Context);
        // lError = QueryTimeZoneData(hZoneKey, Context);

        RegCloseKey(hZoneKey);
    }

    RegCloseKey(hZonesKey);
}

static ULONG
TzGetVersion(VOID)
{
    static OSVERSIONINFOW s_VerInfo = {0};
    static ULONG s_Version = 0;

    if (s_Version == 0)
    {
        s_VerInfo.dwOSVersionInfoSize = sizeof(s_VerInfo);
        GetVersionExW(&s_VerInfo);
        s_Version = (s_VerInfo.dwMajorVersion << 8) | s_VerInfo.dwMinorVersion;
    }

    return s_Version;
}

/**
 * @brief
 * Retrieves the currently-active automatic daylight-time adjustment setting.
 *
 * @return
 * TRUE or FALSE if AutoDaylight is ON or OFF, respectively.
 **/
BOOL
GetAutoDaylight(VOID)
{
    LONG lError;
    HKEY hKey;
    PCWSTR pszAutoDaylightDisable;
    DWORD dwType;
    DWORD dwDisabled;
    DWORD dwValueSize;

    lError = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                           L"SYSTEM\\CurrentControlSet\\Control\\TimeZoneInformation",
                           0,
                           KEY_QUERY_VALUE,
                           &hKey);
    if (lError != ERROR_SUCCESS)
        return FALSE;

    /* Select the correct registry value (OS-dependent) */
    if (TzGetVersion() < _WIN32_WINNT_VISTA)
        pszAutoDaylightDisable = L"DisableAutoDaylightTimeSet";
    else
        pszAutoDaylightDisable = L"DynamicDaylightTimeDisabled";

    dwValueSize = sizeof(dwDisabled);
    lError = RegQueryValueExW(hKey,
                              pszAutoDaylightDisable,
                              NULL,
                              &dwType,
                              (PBYTE)&dwDisabled,
                              &dwValueSize);

    RegCloseKey(hKey);

    if ((lError != ERROR_SUCCESS) || (dwType != REG_DWORD) || (dwValueSize != sizeof(dwDisabled)))
    {
        /*
         * The call failed (non zero) because the registry value isn't available,
         * which means auto-daylight shouldn't be disabled.
         */
        dwDisabled = FALSE;
    }

    return !dwDisabled;
}

/**
 * @brief
 * Enables or disables automatic daylight-time adjustment.
 *
 * @param[in]   EnableAutoDaylightTime
 * Set to TRUE for enabling automatic daylight-time, or FALSE to disable it.
 *
 * @return  None.
 **/
VOID
SetAutoDaylight(
    _In_ BOOL EnableAutoDaylightTime)
{
    LONG lError;
    HKEY hKey;
    PCWSTR pszAutoDaylightDisable;
    DWORD dwDisabled = TRUE;

    lError = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                           L"SYSTEM\\CurrentControlSet\\Control\\TimeZoneInformation",
                           0,
                           KEY_SET_VALUE,
                           &hKey);
    if (lError != ERROR_SUCCESS)
        return;

    /* Select the correct registry value (OS-dependent) */
    if (TzGetVersion() < _WIN32_WINNT_VISTA)
        pszAutoDaylightDisable = L"DisableAutoDaylightTimeSet";
    else
        pszAutoDaylightDisable = L"DynamicDaylightTimeDisabled";

    if (!EnableAutoDaylightTime)
    {
        /* Auto-Daylight disabled: set the value to TRUE */
        RegSetValueExW(hKey,
                       pszAutoDaylightDisable,
                       0,
                       REG_DWORD,
                       (PBYTE)&dwDisabled,
                       sizeof(dwDisabled));
    }
    else
    {
        /* Auto-Daylight enabled: just delete the value */
        RegDeleteValueW(hKey, pszAutoDaylightDisable);
    }

    RegCloseKey(hKey);
}
