/*
 * Setupapi install routines
 *
 * Copyright 2002 Alexandre Julliard for CodeWeavers
 *           2005-2006 Hervé Poussineau (hpoussin@reactos.org)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "setupapi_private.h"

#include <winsvc.h>
#include <ndk/cmfuncs.h>

/* info passed to callback functions dealing with files */
struct files_callback_info
{
    HSPFILEQ queue;
    PCWSTR   src_root;
    UINT     copy_flags;
    HINF     layout;
};

/* info passed to callback functions dealing with the registry */
struct registry_callback_info
{
    HKEY default_root;
    BOOL delete;
};

/* info passed to callback functions dealing with registering dlls */
struct register_dll_info
{
    PSP_FILE_CALLBACK_W callback;
    PVOID               callback_context;
    BOOL                unregister;
};

/* info passed to callback functions dealing with Needs directives */
struct needs_callback_info
{
    UINT type;

    HWND             owner;
    UINT             flags;
    HKEY             key_root;
    LPCWSTR          src_root;
    UINT             copy_flags;
    PVOID            callback;
    PVOID            context;
    HDEVINFO         devinfo;
    PSP_DEVINFO_DATA devinfo_data;
    PVOID            reserved1;
    PVOID            reserved2;
};

typedef BOOL (*iterate_fields_func)( HINF hinf, PCWSTR field, void *arg );
static BOOL GetLineText( HINF hinf, PCWSTR section_name, PCWSTR key_name, PWSTR *value);
typedef HRESULT (WINAPI *COINITIALIZE)(IN LPVOID pvReserved);
typedef HRESULT (WINAPI *COCREATEINSTANCE)(IN REFCLSID rclsid, IN LPUNKNOWN pUnkOuter, IN DWORD dwClsContext, IN REFIID riid, OUT LPVOID *ppv);
typedef HRESULT (WINAPI *COUNINITIALIZE)(VOID);


/***********************************************************************
 *            get_field_string
 *
 * Retrieve the contents of a field, dynamically growing the buffer if necessary.
 */
static WCHAR *get_field_string( INFCONTEXT *context, DWORD index, WCHAR *buffer,
                                WCHAR *static_buffer, DWORD *size )
{
    DWORD required;

    if (SetupGetStringFieldW( context, index, buffer, *size, &required )) return buffer;
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
    {
        /* now grow the buffer */
        if (buffer != static_buffer) HeapFree( GetProcessHeap(), 0, buffer );
        if (!(buffer = HeapAlloc( GetProcessHeap(), 0, required*sizeof(WCHAR) ))) return NULL;
        *size = required;
        if (SetupGetStringFieldW( context, index, buffer, *size, &required )) return buffer;
    }
    if (buffer != static_buffer) HeapFree( GetProcessHeap(), 0, buffer );
    return NULL;
}


/***********************************************************************
 *            copy_files_callback
 *
 * Called once for each CopyFiles entry in a given section.
 */
static BOOL copy_files_callback( HINF hinf, PCWSTR field, void *arg )
{
    struct files_callback_info *info = arg;

    if (field[0] == '@')  /* special case: copy single file */
        SetupQueueDefaultCopyW( info->queue, info->layout ? info->layout : hinf, info->src_root, NULL, field+1, info->copy_flags );
    else
        SetupQueueCopySectionW( info->queue, info->src_root, info->layout ? info->layout : hinf, hinf, field, info->copy_flags );
    return TRUE;
}


/***********************************************************************
 *            delete_files_callback
 *
 * Called once for each DelFiles entry in a given section.
 */
static BOOL delete_files_callback( HINF hinf, PCWSTR field, void *arg )
{
    struct files_callback_info *info = arg;
    SetupQueueDeleteSectionW( info->queue, hinf, 0, field );
    return TRUE;
}


/***********************************************************************
 *            rename_files_callback
 *
 * Called once for each RenFiles entry in a given section.
 */
static BOOL rename_files_callback( HINF hinf, PCWSTR field, void *arg )
{
    struct files_callback_info *info = arg;
    SetupQueueRenameSectionW( info->queue, hinf, 0, field );
    return TRUE;
}


/***********************************************************************
 *            get_root_key
 *
 * Retrieve the registry root key from its name.
 */
static HKEY get_root_key( const WCHAR *name, HKEY def_root )
{
    if (!strcmpiW( name, L"HKCR" )) return HKEY_CLASSES_ROOT;
    if (!strcmpiW( name, L"HKCU" )) return HKEY_CURRENT_USER;
    if (!strcmpiW( name, L"HKLM" )) return HKEY_LOCAL_MACHINE;
    if (!strcmpiW( name, L"HKU" )) return HKEY_USERS;
    if (!strcmpiW( name, L"HKR" )) return def_root;
    return 0;
}


/***********************************************************************
 *            append_multi_sz_value
 *
 * Append a multisz string to a multisz registry value.
 */
static void append_multi_sz_value( HKEY hkey, const WCHAR *value, const WCHAR *strings,
                                   DWORD str_size )
{
    DWORD size, type, total;
    WCHAR *buffer, *p;

    if (RegQueryValueExW( hkey, value, NULL, &type, NULL, &size )) return;
    if (type != REG_MULTI_SZ) return;

    size = size + str_size * sizeof(WCHAR) ;
    if (!(buffer = HeapAlloc( GetProcessHeap(), 0, size))) return;
    if (RegQueryValueExW( hkey, value, NULL, NULL, (BYTE *)buffer, &size )) goto done;

    /* compare each string against all the existing ones */
    total = size;
    while (*strings)
    {
        int len = strlenW(strings) + 1;

        for (p = buffer; *p; p += strlenW(p) + 1)
            if (!strcmpiW( p, strings )) break;

        if (!*p)  /* not found, need to append it */
        {
            memcpy( p, strings, len * sizeof(WCHAR) );
            p[len] = 0;
            total += len * sizeof(WCHAR);
        }
        strings += len;
    }
    if (total != size)
    {
        TRACE( "setting value %s to %s\n", debugstr_w(value), debugstr_w(buffer) );
        RegSetValueExW( hkey, value, 0, REG_MULTI_SZ, (BYTE *)buffer, total + sizeof(WCHAR) );
    }
 done:
    HeapFree( GetProcessHeap(), 0, buffer );
}


/***********************************************************************
 *            delete_multi_sz_value
 *
 * Remove a string from a multisz registry value.
 */
static void delete_multi_sz_value( HKEY hkey, const WCHAR *value, const WCHAR *string )
{
    DWORD size, type;
    WCHAR *buffer, *src, *dst;

    if (RegQueryValueExW( hkey, value, NULL, &type, NULL, &size )) return;
    if (type != REG_MULTI_SZ) return;
    /* allocate double the size, one for value before and one for after */
    if (!(buffer = HeapAlloc( GetProcessHeap(), 0, size * 2))) return;
    if (RegQueryValueExW( hkey, value, NULL, NULL, (BYTE *)buffer, &size )) goto done;
    src = buffer;
    dst = buffer + size;
    while (*src)
    {
        int len = strlenW(src) + 1;
        if (strcmpiW( src, string ))
        {
            memcpy( dst, src, len * sizeof(WCHAR) );
            dst += len;
        }
        src += len;
    }
    *dst++ = 0;
    if (dst != buffer + 2*size)  /* did we remove something? */
    {
        TRACE( "setting value %s to %s\n", debugstr_w(value), debugstr_w(buffer + size) );
        RegSetValueExW( hkey, value, 0, REG_MULTI_SZ,
                        (BYTE *)(buffer + size), dst - (buffer + size) );
    }
 done:
    HeapFree( GetProcessHeap(), 0, buffer );
}


/***********************************************************************
 *            do_reg_operation
 *
 * Perform an add/delete registry operation depending on the flags.
 */
static BOOL do_reg_operation( HKEY hkey, const WCHAR *value, INFCONTEXT *context, INT flags )
{
    DWORD type, size;

    if (flags & (FLG_ADDREG_DELREG_BIT | FLG_ADDREG_DELVAL))  /* deletion */
    {
        if (*value && !(flags & FLG_DELREG_KEYONLY_COMMON))
        {
            if ((flags & FLG_DELREG_MULTI_SZ_DELSTRING) == FLG_DELREG_MULTI_SZ_DELSTRING)
            {
                WCHAR *str;

                if (!SetupGetStringFieldW( context, 5, NULL, 0, &size ) || !size) return TRUE;
                if (!(str = HeapAlloc( GetProcessHeap(), 0, size * sizeof(WCHAR) ))) return FALSE;
                SetupGetStringFieldW( context, 5, str, size, NULL );
                delete_multi_sz_value( hkey, value, str );
                HeapFree( GetProcessHeap(), 0, str );
            }
            else RegDeleteValueW( hkey, value );
        }
        else
        {
#ifdef __WINESRC__
            RegDeleteTreeW( hkey, NULL );
#endif
            NtDeleteKey( hkey );
        }
        return TRUE;
    }

    if (flags & (FLG_ADDREG_KEYONLY|FLG_ADDREG_KEYONLY_COMMON)) return TRUE;

    if (flags & (FLG_ADDREG_NOCLOBBER|FLG_ADDREG_OVERWRITEONLY))
    {
        BOOL exists = !RegQueryValueExW( hkey, value, NULL, NULL, NULL, NULL );
        if (exists && (flags & FLG_ADDREG_NOCLOBBER)) return TRUE;
        if (!exists && (flags & FLG_ADDREG_OVERWRITEONLY)) return TRUE;
    }

    switch(flags & FLG_ADDREG_TYPE_MASK)
    {
    case FLG_ADDREG_TYPE_SZ:        type = REG_SZ; break;
    case FLG_ADDREG_TYPE_MULTI_SZ:  type = REG_MULTI_SZ; break;
    case FLG_ADDREG_TYPE_EXPAND_SZ: type = REG_EXPAND_SZ; break;
    case FLG_ADDREG_TYPE_BINARY:    type = REG_BINARY; break;
    case FLG_ADDREG_TYPE_DWORD:     type = REG_DWORD; break;
    case FLG_ADDREG_TYPE_NONE:      type = REG_NONE; break;
    default:                        type = flags >> 16; break;
    }

    if (!(flags & FLG_ADDREG_BINVALUETYPE) ||
        (type == REG_DWORD && SetupGetFieldCount(context) == 5))
    {
        WCHAR *str = NULL;

        if (type == REG_MULTI_SZ)
        {
            if (!SetupGetMultiSzFieldW( context, 5, NULL, 0, &size )) size = 0;
            if (size)
            {
                if (!(str = HeapAlloc( GetProcessHeap(), 0, size * sizeof(WCHAR) ))) return FALSE;
                SetupGetMultiSzFieldW( context, 5, str, size, NULL );
            }
            if (flags & FLG_ADDREG_APPEND)
            {
                if (!str) return TRUE;
                append_multi_sz_value( hkey, value, str, size );
                HeapFree( GetProcessHeap(), 0, str );
                return TRUE;
            }
            /* else fall through to normal string handling */
        }
        else
        {
            if (!SetupGetStringFieldW( context, 5, NULL, 0, &size )) size = 0;
            if (size)
            {
                if (!(str = HeapAlloc( GetProcessHeap(), 0, size * sizeof(WCHAR) ))) return FALSE;
                SetupGetStringFieldW( context, 5, str, size, NULL );
            }
        }

        if (type == REG_DWORD)
        {
            DWORD dw = str ? strtoulW( str, NULL, 0 ) : 0;
            TRACE( "setting dword %s to %x\n", debugstr_w(value), dw );
            RegSetValueExW( hkey, value, 0, type, (BYTE *)&dw, sizeof(dw) );
        }
        else
        {
            TRACE( "setting value %s to %s\n", debugstr_w(value), debugstr_w(str) );
            if (str) RegSetValueExW( hkey, value, 0, type, (BYTE *)str, size * sizeof(WCHAR) );
            else RegSetValueExW( hkey, value, 0, type, (const BYTE *)L"", sizeof(WCHAR) );
        }
        HeapFree( GetProcessHeap(), 0, str );
        return TRUE;
    }
    else  /* get the binary data */
    {
        BYTE *data = NULL;

        if (!SetupGetBinaryField( context, 5, NULL, 0, &size )) size = 0;
        if (size)
        {
            if (!(data = HeapAlloc( GetProcessHeap(), 0, size ))) return FALSE;
            TRACE( "setting binary data %s len %d\n", debugstr_w(value), size );
            SetupGetBinaryField( context, 5, data, size, NULL );
        }
        RegSetValueExW( hkey, value, 0, type, data, size );
        HeapFree( GetProcessHeap(), 0, data );
        return TRUE;
    }
}


/***********************************************************************
 *            registry_callback
 *
 * Called once for each AddReg and DelReg entry in a given section.
 */
static BOOL registry_callback( HINF hinf, PCWSTR field, void *arg )
{
    struct registry_callback_info *info = arg;
    LPWSTR security_key, security_descriptor;
    INFCONTEXT context, security_context;
    PSECURITY_DESCRIPTOR sd = NULL;
    SECURITY_ATTRIBUTES security_attributes = { 0, };
    HKEY root_key, hkey;
    DWORD required;

    BOOL ok = SetupFindFirstLineW( hinf, field, NULL, &context );
    if (!ok)
        return TRUE;

    /* Check for .Security section */
    security_key = MyMalloc( (strlenW( field ) + strlenW( L".Security" )) * sizeof(WCHAR) + sizeof(UNICODE_NULL) );
    if (!security_key)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    strcpyW( security_key, field );
    strcatW( security_key, L".Security" );
    ok = SetupFindFirstLineW( hinf, security_key, NULL, &security_context );
    MyFree(security_key);
    if (ok)
    {
        if (!SetupGetLineTextW( &security_context, NULL, NULL, NULL, NULL, 0, &required ))
            return FALSE;
        security_descriptor = MyMalloc( required * sizeof(WCHAR) );
        if (!security_descriptor)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        if (!SetupGetLineTextW( &security_context, NULL, NULL, NULL, security_descriptor, required, NULL ))
            return FALSE;
        ok = ConvertStringSecurityDescriptorToSecurityDescriptorW( security_descriptor, SDDL_REVISION_1, &sd, NULL );
        MyFree( security_descriptor );
        if (!ok)
            return FALSE;
        security_attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
        security_attributes.lpSecurityDescriptor = sd;
    }

    for (ok = TRUE; ok; ok = SetupFindNextLine( &context, &context ))
    {
        WCHAR buffer[MAX_INF_STRING_LENGTH];
        INT flags;

        /* get root */
        if (!SetupGetStringFieldW( &context, 1, buffer, ARRAY_SIZE( buffer ), NULL ))
            continue;
        if (!(root_key = get_root_key( buffer, info->default_root )))
            continue;

        /* get key */
        if (!SetupGetStringFieldW( &context, 2, buffer, ARRAY_SIZE( buffer ), NULL ))
            *buffer = 0;

        /* get flags */
        if (!SetupGetIntField( &context, 4, &flags )) flags = 0;

        if (!info->delete)
        {
            if (flags & FLG_ADDREG_DELREG_BIT) continue;  /* ignore this entry */
        }
        else
        {
            if (!flags) flags = FLG_ADDREG_DELREG_BIT;
            else if (!(flags & FLG_ADDREG_DELREG_BIT)) continue;  /* ignore this entry */
        }

        if (info->delete || (flags & FLG_ADDREG_OVERWRITEONLY))
        {
            if (RegOpenKeyW( root_key, buffer, &hkey )) continue;  /* ignore if it doesn't exist */
        }
        else if (RegCreateKeyExW( root_key, buffer, 0, NULL, 0, MAXIMUM_ALLOWED,
            sd ? &security_attributes : NULL, &hkey, NULL ))
        {
            ERR( "could not create key %p %s\n", root_key, debugstr_w(buffer) );
            continue;
        }
        TRACE( "key %p %s\n", root_key, debugstr_w(buffer) );

        /* get value name */
        if (!SetupGetStringFieldW( &context, 3, buffer, ARRAY_SIZE( buffer ), NULL ))
            *buffer = 0;

        /* and now do it */
        if (!do_reg_operation( hkey, buffer, &context, flags ))
        {
            if (hkey != root_key) RegCloseKey( hkey );
            if (sd) LocalFree( sd );
            return FALSE;
        }
        if (hkey != root_key) RegCloseKey( hkey );
    }
    if (sd) LocalFree( sd );
    return TRUE;
}


/***********************************************************************
 *            do_register_dll
 *
 * Register or unregister a dll.
 */
static BOOL do_register_dll( const struct register_dll_info *info, const WCHAR *path,
                             INT flags, INT timeout, const WCHAR *args )
{
    HMODULE module;
    HRESULT res;
    SP_REGISTER_CONTROL_STATUSW status;
#ifdef __WINESRC__
    IMAGE_NT_HEADERS *nt;
#endif

    status.cbSize = sizeof(status);
    status.FileName = path;
    status.FailureCode = SPREG_SUCCESS;
    status.Win32Error = ERROR_SUCCESS;

    if (info->callback)
    {
        switch(info->callback( info->callback_context, SPFILENOTIFY_STARTREGISTRATION,
                               (UINT_PTR)&status, !info->unregister ))
        {
        case FILEOP_ABORT:
            SetLastError( ERROR_OPERATION_ABORTED );
            return FALSE;
        case FILEOP_SKIP:
            return TRUE;
        case FILEOP_DOIT:
            break;
        }
    }

    if (!(module = LoadLibraryExW( path, 0, LOAD_WITH_ALTERED_SEARCH_PATH )))
    {
        WARN( "could not load %s\n", debugstr_w(path) );
        status.FailureCode = SPREG_LOADLIBRARY;
        status.Win32Error = GetLastError();
        goto done;
    }

#ifdef __WINESRC__
    if ((nt = RtlImageNtHeader( module )) && !(nt->FileHeader.Characteristics & IMAGE_FILE_DLL))
    {
        /* file is an executable, not a dll */
        STARTUPINFOW startup;
        PROCESS_INFORMATION process_info;
        WCHAR *cmd_line;
        BOOL res;
        SIZE_T len;

        FreeLibrary( module );
        module = NULL;
        if (!args) args = L"/RegServer";
        len = strlenW(path) + strlenW(args) + 4;
        cmd_line = HeapAlloc( GetProcessHeap(), 0, len * sizeof(WCHAR) );
        sprintfW( cmd_line, L"\"%s\" %s", path, args );
        memset( &startup, 0, sizeof(startup) );
        startup.cb = sizeof(startup);
        TRACE( "executing %s\n", debugstr_w(cmd_line) );
        res = CreateProcessW( path, cmd_line, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process_info );
        HeapFree( GetProcessHeap(), 0, cmd_line );
        if (!res)
        {
            status.FailureCode = SPREG_LOADLIBRARY;
            status.Win32Error = GetLastError();
            goto done;
        }
        CloseHandle( process_info.hThread );

        if (WaitForSingleObject( process_info.hProcess, timeout*1000 ) == WAIT_TIMEOUT)
        {
            /* timed out, kill the process */
            TerminateProcess( process_info.hProcess, 1 );
            status.FailureCode = SPREG_TIMEOUT;
            status.Win32Error = ERROR_TIMEOUT;
        }
        CloseHandle( process_info.hProcess );
        goto done;
    }
#endif // __WINESRC__

    if (flags & FLG_REGSVR_DLLREGISTER)
    {
        const char *entry_point = info->unregister ? "DllUnregisterServer" : "DllRegisterServer";
        HRESULT (WINAPI *func)(void) = (void *)GetProcAddress( module, entry_point );

        if (!func)
        {
            status.FailureCode = SPREG_GETPROCADDR;
            status.Win32Error = GetLastError();
            goto done;
        }

        TRACE( "calling %s in %s\n", entry_point, debugstr_w(path) );
        res = func();

        if (FAILED(res))
        {
            WARN( "calling %s in %s returned error %x\n", entry_point, debugstr_w(path), res );
            status.FailureCode = SPREG_REGSVR;
            status.Win32Error = res;
            goto done;
        }
    }

    if (flags & FLG_REGSVR_DLLINSTALL)
    {
        HRESULT (WINAPI *func)(BOOL,LPCWSTR) = (void *)GetProcAddress( module, "DllInstall" );

        if (!func)
        {
            status.FailureCode = SPREG_GETPROCADDR;
            status.Win32Error = GetLastError();
            goto done;
        }

        TRACE( "calling DllInstall(%d,%s) in %s\n",
               !info->unregister, debugstr_w(args), debugstr_w(path) );
        res = func( !info->unregister, args );

        if (FAILED(res))
        {
            WARN( "calling DllInstall in %s returned error %x\n", debugstr_w(path), res );
            status.FailureCode = SPREG_REGSVR;
            status.Win32Error = res;
            goto done;
        }
    }

done:
    if (module) FreeLibrary( module );
    if (info->callback) info->callback( info->callback_context, SPFILENOTIFY_ENDREGISTRATION,
                                        (UINT_PTR)&status, !info->unregister );
    return TRUE;
}


/***********************************************************************
 *            register_dlls_callback
 *
 * Called once for each RegisterDlls entry in a given section.
 */
static BOOL register_dlls_callback( HINF hinf, PCWSTR field, void *arg )
{
    struct register_dll_info *info = arg;
    INFCONTEXT context;
    BOOL ret = TRUE;
    BOOL ok = SetupFindFirstLineW( hinf, field, NULL, &context );

    for (; ok; ok = SetupFindNextLine( &context, &context ))
    {
        WCHAR *path, *args, *p;
        WCHAR buffer[MAX_INF_STRING_LENGTH];
        INT flags, timeout;

        /* get directory */
        if (!(path = PARSER_get_dest_dir( &context ))) continue;

        /* get dll name */
        if (!SetupGetStringFieldW( &context, 3, buffer, ARRAY_SIZE( buffer ), NULL ))
            goto done;
        if (!(p = HeapReAlloc( GetProcessHeap(), 0, path,
                               (strlenW(path) + strlenW(buffer) + 2) * sizeof(WCHAR) ))) goto done;
        path = p;
        p += strlenW(p);
        if (p == path || p[-1] != '\\') *p++ = '\\';
        strcpyW( p, buffer );

        /* get flags */
        if (!SetupGetIntField( &context, 4, &flags )) flags = 0;

        /* get timeout */
        if (!SetupGetIntField( &context, 5, &timeout )) timeout = 60;

        /* get command line */
        args = NULL;
        if (SetupGetStringFieldW( &context, 6, buffer, ARRAY_SIZE( buffer ), NULL ))
            args = buffer;

        ret = do_register_dll( info, path, flags, timeout, args );

    done:
        HeapFree( GetProcessHeap(), 0, path );
        if (!ret) break;
    }
    return ret;
}

#ifdef __WINESRC__
/***********************************************************************
 *            fake_dlls_callback
 *
 * Called once for each WineFakeDlls entry in a given section.
 */
static BOOL fake_dlls_callback( HINF hinf, PCWSTR field, void *arg )
{
    INFCONTEXT context;
    BOOL ret = TRUE;
    BOOL ok = SetupFindFirstLineW( hinf, field, NULL, &context );

    for (; ok; ok = SetupFindNextLine( &context, &context ))
    {
        WCHAR *path, *p;
        WCHAR buffer[MAX_INF_STRING_LENGTH];

        /* get directory */
        if (!(path = PARSER_get_dest_dir( &context ))) continue;

        /* get dll name */
        if (!SetupGetStringFieldW( &context, 3, buffer, ARRAY_SIZE( buffer ), NULL ))
            goto done;
        if (!(p = HeapReAlloc( GetProcessHeap(), 0, path,
                               (strlenW(path) + strlenW(buffer) + 2) * sizeof(WCHAR) ))) goto done;
        path = p;
        p += strlenW(p);
        if (p == path || p[-1] != '\\') *p++ = '\\';
        strcpyW( p, buffer );

        /* get source dll */
        if (SetupGetStringFieldW( &context, 4, buffer, ARRAY_SIZE( buffer ), NULL ))
            p = buffer;  /* otherwise use target base name as default source */

        create_fake_dll( path, p );  /* ignore errors */

    done:
        HeapFree( GetProcessHeap(), 0, path );
        if (!ret) break;
    }
    return ret;
}
#endif // __WINESRC__

/***********************************************************************
 *            update_ini_callback
 *
 * Called once for each UpdateInis entry in a given section.
 */
static BOOL update_ini_callback( HINF hinf, PCWSTR field, void *arg )
{
    INFCONTEXT context;

    BOOL ok = SetupFindFirstLineW( hinf, field, NULL, &context );

    for (; ok; ok = SetupFindNextLine( &context, &context ))
    {
        WCHAR buffer[MAX_INF_STRING_LENGTH];
        WCHAR  filename[MAX_INF_STRING_LENGTH];
        WCHAR  section[MAX_INF_STRING_LENGTH];
        WCHAR  entry[MAX_INF_STRING_LENGTH];
        WCHAR  string[MAX_INF_STRING_LENGTH];
        LPWSTR divider;

        if (!SetupGetStringFieldW( &context, 1, filename, ARRAY_SIZE( filename ), NULL ))
            continue;

        if (!SetupGetStringFieldW( &context, 2, section, ARRAY_SIZE( section ), NULL ))
            continue;

        if (!SetupGetStringFieldW( &context, 4, buffer, ARRAY_SIZE( buffer ), NULL ))
            continue;

        divider = strchrW(buffer,'=');
        if (divider)
        {
            *divider = 0;
            strcpyW(entry,buffer);
            divider++;
            strcpyW(string,divider);
        }
        else
        {
            strcpyW(entry,buffer);
            string[0]=0;
        }

        TRACE("Writing %s = %s in %s of file %s\n",debugstr_w(entry),
               debugstr_w(string),debugstr_w(section),debugstr_w(filename));
        WritePrivateProfileStringW(section,entry,string,filename);

    }
    return TRUE;
}

static BOOL update_ini_fields_callback( HINF hinf, PCWSTR field, void *arg )
{
    FIXME( "should update ini fields %s\n", debugstr_w(field) );
    return TRUE;
}

static BOOL ini2reg_callback( HINF hinf, PCWSTR field, void *arg )
{
    FIXME( "should do ini2reg %s\n", debugstr_w(field) );
    return TRUE;
}

static BOOL logconf_callback( HINF hinf, PCWSTR field, void *arg )
{
    FIXME( "should do logconf %s\n", debugstr_w(field) );
    return TRUE;
}

static BOOL bitreg_callback( HINF hinf, PCWSTR field, void *arg )
{
    FIXME( "should do bitreg %s\n", debugstr_w(field) );
    return TRUE;
}

static BOOL Concatenate(int DirId, LPCWSTR SubDirPart, LPCWSTR NamePart, LPWSTR *pFullName)
{
    DWORD dwRequired = 0;
    LPCWSTR Dir;
    LPWSTR FullName;

    *pFullName = NULL;

    Dir = DIRID_get_string(DirId);
    if (Dir)
        dwRequired += wcslen(Dir) + 1;
    if (SubDirPart)
        dwRequired += wcslen(SubDirPart) + 1;
    if (NamePart)
        dwRequired += wcslen(NamePart);
    dwRequired = dwRequired * sizeof(WCHAR) + sizeof(UNICODE_NULL);

    FullName = MyMalloc(dwRequired);
    if (!FullName)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    FullName[0] = UNICODE_NULL;

    if (Dir)
    {
        wcscat(FullName, Dir);
        if (FullName[wcslen(FullName) - 1] != '\\')
            wcscat(FullName, L"\\");
    }
    if (SubDirPart)
    {
        wcscat(FullName, SubDirPart);
        if (FullName[wcslen(FullName) - 1] != '\\')
            wcscat(FullName, L"\\");
    }
    if (NamePart)
        wcscat(FullName, NamePart);

    *pFullName = FullName;
    return TRUE;
}

/***********************************************************************
 *            profile_items_callback
 *
 * Called once for each ProfileItems entry in a given section.
 */
static BOOL
profile_items_callback(
    IN HINF hInf,
    IN PCWSTR SectionName,
    IN PVOID Arg)
{
    INFCONTEXT Context;
    LPWSTR LinkSubDir = NULL, LinkName = NULL;
    INT LinkAttributes = 0;
    INT LinkFolder = 0;
    INT FileDirId = 0;
    INT CSIDL = CSIDL_COMMON_PROGRAMS;
    LPWSTR FileSubDir = NULL;
    INT DirId = 0;
    LPWSTR SubDirPart = NULL, NamePart = NULL;
    LPWSTR FullLinkName = NULL, FullFileName = NULL, FullWorkingDir = NULL, FullIconName = NULL;
    INT IconIdx = 0;
    LPWSTR lpHotKey = NULL, lpInfoTip = NULL;
    LPWSTR DisplayName = NULL;
    INT DisplayResId = 0;
    BOOL ret = FALSE;
    DWORD Index, Required;

    IShellLinkW *psl;
    IPersistFile *ppf;
    HMODULE hOle32 = NULL;
    COINITIALIZE pCoInitialize;
    COCREATEINSTANCE pCoCreateInstance;
    COUNINITIALIZE pCoUninitialize;
    HRESULT hr;

    TRACE("hInf %p, SectionName %s, Arg %p\n",
        hInf, debugstr_w(SectionName), Arg);

    /* Read 'Name' entry */
    if (!SetupFindFirstLineW(hInf, SectionName, L"Name", &Context))
        goto cleanup;
    if (!GetStringField(&Context, 1, &LinkName))
        goto cleanup;
    if (SetupGetFieldCount(&Context) >= 2)
    {
        if (!SetupGetIntField(&Context, 2, &LinkAttributes))
            goto cleanup;
    }
    if (SetupGetFieldCount(&Context) >= 3)
    {
        if (!SetupGetIntField(&Context, 3, &LinkFolder))
            goto cleanup;
    }

    /* Read 'CmdLine' entry */
    if (!SetupFindFirstLineW(hInf, SectionName, L"CmdLine", &Context))
        goto cleanup;
    Index = 1;
    if (!SetupGetIntField(&Context, Index++, &FileDirId))
        goto cleanup;
    if (SetupGetFieldCount(&Context) >= 3)
    {
        if (!GetStringField(&Context, Index++, &FileSubDir))
            goto cleanup;
    }
    if (!GetStringField(&Context, Index++, &NamePart))
        goto cleanup;
    if (!Concatenate(FileDirId, FileSubDir, NamePart, &FullFileName))
        goto cleanup;
    MyFree(NamePart);
    NamePart = NULL;

    /* Read 'SubDir' entry */
    if ((LinkAttributes & FLG_PROFITEM_GROUP) == 0 && SetupFindFirstLineW(hInf, SectionName, L"SubDir", &Context))
    {
        if (!GetStringField(&Context, 1, &LinkSubDir))
            goto cleanup;
    }

    /* Read 'WorkingDir' entry */
    if (SetupFindFirstLineW(hInf, SectionName, L"WorkingDir", &Context))
    {
        if (!SetupGetIntField(&Context, 1, &DirId))
            goto cleanup;
        if (SetupGetFieldCount(&Context) >= 2)
        {
            if (!GetStringField(&Context, 2, &SubDirPart))
                goto cleanup;
        }
        if (!Concatenate(DirId, SubDirPart, NULL, &FullWorkingDir))
            goto cleanup;
        MyFree(SubDirPart);
        SubDirPart = NULL;
    }
    else
    {
        if (!Concatenate(FileDirId, FileSubDir, NULL, &FullWorkingDir))
            goto cleanup;
    }

    /* Read 'IconPath' entry */
    if (SetupFindFirstLineW(hInf, SectionName, L"IconPath", &Context))
    {
        Index = 1;
        if (!SetupGetIntField(&Context, Index++, &DirId))
            goto cleanup;
        if (SetupGetFieldCount(&Context) >= 3)
        {
            if (!GetStringField(&Context, Index++, &SubDirPart))
                goto cleanup;
        }
        if (!GetStringField(&Context, Index, &NamePart))
            goto cleanup;
        if (!Concatenate(DirId, SubDirPart, NamePart, &FullIconName))
            goto cleanup;
        MyFree(SubDirPart);
        MyFree(NamePart);
        SubDirPart = NamePart = NULL;
    }
    else
    {
        FullIconName = pSetupDuplicateString(FullFileName);
        if (!FullIconName)
            goto cleanup;
    }

    /* Read 'IconIndex' entry */
    if (SetupFindFirstLineW(hInf, SectionName, L"IconIndex", &Context))
    {
        if (!SetupGetIntField(&Context, 1, &IconIdx))
            goto cleanup;
    }

    /* Read 'HotKey' and 'InfoTip' entries */
    GetLineText(hInf, SectionName, L"HotKey", &lpHotKey);
    GetLineText(hInf, SectionName, L"InfoTip", &lpInfoTip);

    /* Read 'DisplayResource' entry */
    if (SetupFindFirstLineW(hInf, SectionName, L"DisplayResource", &Context))
    {
        if (!GetStringField(&Context, 1, &DisplayName))
            goto cleanup;
        if (!SetupGetIntField(&Context, 2, &DisplayResId))
            goto cleanup;
    }

    /* Some debug */
    TRACE("Link is %s\\%s, attributes 0x%x\n", debugstr_w(LinkSubDir), debugstr_w(LinkName), LinkAttributes);
    TRACE("File is %s\n", debugstr_w(FullFileName));
    TRACE("Working dir %s\n", debugstr_w(FullWorkingDir));
    TRACE("Icon is %s, %d\n", debugstr_w(FullIconName), IconIdx);
    TRACE("Hotkey %s\n", debugstr_w(lpHotKey));
    TRACE("InfoTip %s\n", debugstr_w(lpInfoTip));
    TRACE("Display %s, %d\n", DisplayName, DisplayResId);

    /* Load ole32.dll */
    hOle32 = LoadLibraryA("ole32.dll");
    if (!hOle32)
        goto cleanup;
    pCoInitialize = (COINITIALIZE)GetProcAddress(hOle32, "CoInitialize");
    if (!pCoInitialize)
        goto cleanup;
    pCoCreateInstance = (COCREATEINSTANCE)GetProcAddress(hOle32, "CoCreateInstance");
    if (!pCoCreateInstance)
        goto cleanup;
    pCoUninitialize = (COUNINITIALIZE)GetProcAddress(hOle32, "CoUninitialize");
    if (!pCoUninitialize)
        goto cleanup;

    /* Create shortcut */
    hr = pCoInitialize(NULL);
    if (!SUCCEEDED(hr))
    {
        if (HRESULT_FACILITY(hr) == FACILITY_WIN32)
            SetLastError(HRESULT_CODE(hr));
        else
            SetLastError(E_FAIL);
        goto cleanup;
    }
    hr = pCoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (LPVOID*)&psl);
    if (SUCCEEDED(hr))
    {
        /* Fill link properties */
        hr = IShellLinkW_SetPath(psl, FullFileName);
        if (SUCCEEDED(hr))
            hr = IShellLinkW_SetArguments(psl, L"");
        if (SUCCEEDED(hr))
            hr = IShellLinkW_SetWorkingDirectory(psl, FullWorkingDir);
        if (SUCCEEDED(hr))
            hr = IShellLinkW_SetIconLocation(psl, FullIconName, IconIdx);
        if (SUCCEEDED(hr) && lpHotKey)
            FIXME("Need to store hotkey %s in shell link\n", debugstr_w(lpHotKey));
        if (SUCCEEDED(hr) && lpInfoTip)
            hr = IShellLinkW_SetDescription(psl, lpInfoTip);
        if (SUCCEEDED(hr) && DisplayName)
            FIXME("Need to store display name %s, %d in shell link\n", debugstr_w(DisplayName), DisplayResId);
        if (SUCCEEDED(hr))
        {
            hr = IShellLinkW_QueryInterface(psl, &IID_IPersistFile, (LPVOID*)&ppf);
            if (SUCCEEDED(hr))
            {
                Required = (MAX_PATH + 1 +
                           ((LinkSubDir != NULL) ? wcslen(LinkSubDir) : 0) +
                           ((LinkName != NULL) ? wcslen(LinkName) : 0)) * sizeof(WCHAR);
                FullLinkName = MyMalloc(Required);
                if (!FullLinkName)
                    hr = E_OUTOFMEMORY;
                else
                {
                    if (LinkAttributes & (FLG_PROFITEM_DELETE | FLG_PROFITEM_GROUP))
                        FIXME("Need to handle FLG_PROFITEM_DELETE and FLG_PROFITEM_GROUP\n");
                    if (LinkAttributes & FLG_PROFITEM_CSIDL)
                        CSIDL = LinkFolder;
                    else if (LinkAttributes & FLG_PROFITEM_CURRENTUSER)
                        CSIDL = CSIDL_PROGRAMS;

                    if (SHGetSpecialFolderPathW(
                        NULL,
                        FullLinkName,
                        CSIDL,
                        TRUE))
                    {
                        if (FullLinkName[wcslen(FullLinkName) - 1] != '\\')
                            wcscat(FullLinkName, L"\\");
                        if (LinkSubDir)
                        {
                            wcscat(FullLinkName, LinkSubDir);
                            if (FullLinkName[wcslen(FullLinkName) - 1] != '\\')
                                wcscat(FullLinkName, L"\\");
                        }
                        if (LinkName)
                        {
                            wcscat(FullLinkName, LinkName);
                            wcscat(FullLinkName, L".lnk");
                        }
                        hr = IPersistFile_Save(ppf, FullLinkName, TRUE);
                    }
                    else
                        hr = HRESULT_FROM_WIN32(GetLastError());
                }
                IPersistFile_Release(ppf);
            }
        }
        IShellLinkW_Release(psl);
    }
    pCoUninitialize();
    if (SUCCEEDED(hr))
        ret = TRUE;
    else
    {
        if (HRESULT_FACILITY(hr) == FACILITY_WIN32)
            SetLastError(HRESULT_CODE(hr));
        else
            SetLastError(E_FAIL);
    }

cleanup:
    MyFree(LinkSubDir);
    MyFree(LinkName);
    MyFree(FileSubDir);
    MyFree(SubDirPart);
    MyFree(NamePart);
    MyFree(FullFileName);
    MyFree(FullWorkingDir);
    MyFree(FullIconName);
    MyFree(FullLinkName);
    MyFree(lpHotKey);
    MyFree(lpInfoTip);
    MyFree(DisplayName);
    if (hOle32)
        FreeLibrary(hOle32);

    TRACE("Returning %d\n", ret);
    return ret;
}

static BOOL copy_inf_callback( HINF hinf, PCWSTR field, void *arg )
{
    FIXME( "should do copy inf %s\n", debugstr_w(field) );
    return TRUE;
}


/***********************************************************************
 *            iterate_section_fields
 *
 * Iterate over all fields of a certain key of a certain section
 */
static BOOL iterate_section_fields( HINF hinf, PCWSTR section, PCWSTR key,
                                    iterate_fields_func callback, void *arg )
{
    WCHAR static_buffer[200];
    WCHAR *buffer = static_buffer;
    DWORD size = ARRAY_SIZE( static_buffer );
    INFCONTEXT context;
    BOOL ret = FALSE;

    BOOL ok = SetupFindFirstLineW( hinf, section, key, &context );
    while (ok)
    {
        UINT i, count = SetupGetFieldCount( &context );
        for (i = 1; i <= count; i++)
        {
            if (!(buffer = get_field_string( &context, i, buffer, static_buffer, &size )))
                goto done;
            if (!callback( hinf, buffer, arg ))
            {
                WARN("callback failed for %s %s err %d\n",
                     debugstr_w(section), debugstr_w(buffer), GetLastError() );
                goto done;
            }
        }
        ok = SetupFindNextMatchLineW( &context, key, &context );
    }
    ret = TRUE;
 done:
    if (buffer != static_buffer) HeapFree( GetProcessHeap(), 0, buffer );
    return ret;
}


/***********************************************************************
 *            SetupInstallFilesFromInfSectionA   (SETUPAPI.@)
 */
BOOL WINAPI SetupInstallFilesFromInfSectionA( HINF hinf, HINF hlayout, HSPFILEQ queue,
                                              PCSTR section, PCSTR src_root, UINT flags )
{
    UNICODE_STRING sectionW;
    BOOL ret = FALSE;

    if (!RtlCreateUnicodeStringFromAsciiz( &sectionW, section ))
    {
        SetLastError( ERROR_NOT_ENOUGH_MEMORY );
        return FALSE;
    }
    if (!src_root)
        ret = SetupInstallFilesFromInfSectionW( hinf, hlayout, queue, sectionW.Buffer,
                                                NULL, flags );
    else
    {
        UNICODE_STRING srcW;
        if (RtlCreateUnicodeStringFromAsciiz( &srcW, src_root ))
        {
            ret = SetupInstallFilesFromInfSectionW( hinf, hlayout, queue, sectionW.Buffer,
                                                    srcW.Buffer, flags );
            RtlFreeUnicodeString( &srcW );
        }
        else SetLastError( ERROR_NOT_ENOUGH_MEMORY );
    }
    RtlFreeUnicodeString( &sectionW );
    return ret;
}


/***********************************************************************
 *            SetupInstallFilesFromInfSectionW   (SETUPAPI.@)
 */
BOOL WINAPI SetupInstallFilesFromInfSectionW( HINF hinf, HINF hlayout, HSPFILEQ queue,
                                              PCWSTR section, PCWSTR src_root, UINT flags )
{
    struct files_callback_info info;

    info.queue      = queue;
    info.src_root   = src_root;
    info.copy_flags = flags;
    info.layout     = hlayout;
    return iterate_section_fields( hinf, section, L"CopyFiles", copy_files_callback, &info );
}


/***********************************************************************
 *            SetupInstallFromInfSectionA   (SETUPAPI.@)
 */
BOOL WINAPI SetupInstallFromInfSectionA( HWND owner, HINF hinf, PCSTR section, UINT flags,
                                         HKEY key_root, PCSTR src_root, UINT copy_flags,
                                         PSP_FILE_CALLBACK_A callback, PVOID context,
                                         HDEVINFO devinfo, PSP_DEVINFO_DATA devinfo_data )
{
    UNICODE_STRING sectionW, src_rootW;
    struct callback_WtoA_context ctx;
    BOOL ret = FALSE;

    src_rootW.Buffer = NULL;
    if (src_root && !RtlCreateUnicodeStringFromAsciiz( &src_rootW, src_root ))
    {
        SetLastError( ERROR_NOT_ENOUGH_MEMORY );
        return FALSE;
    }

    if (RtlCreateUnicodeStringFromAsciiz( &sectionW, section ))
    {
        ctx.orig_context = context;
        ctx.orig_handler = callback;
        ret = SetupInstallFromInfSectionW( owner, hinf, sectionW.Buffer, flags, key_root,
                                           src_rootW.Buffer, copy_flags, QUEUE_callback_WtoA,
                                           &ctx, devinfo, devinfo_data );
        RtlFreeUnicodeString( &sectionW );
    }
    else SetLastError( ERROR_NOT_ENOUGH_MEMORY );

    RtlFreeUnicodeString( &src_rootW );
    return ret;
}


/***********************************************************************
 *            include_callback
 *
 * Called once for each Include entry in a given section.
 */
static BOOL include_callback( HINF hinf, PCWSTR field, void *arg )
{
    return SetupOpenAppendInfFileW( field, hinf, NULL );
}


/***********************************************************************
 *            needs_callback
 *
 * Called once for each Needs entry in a given section.
 */
static BOOL needs_callback( HINF hinf, PCWSTR field, void *arg )
{
    struct needs_callback_info *info = arg;

    switch (info->type)
    {
        case 0:
            return SetupInstallFromInfSectionW(info->owner, *(HINF*)hinf, field, info->flags,
               info->key_root, info->src_root, info->copy_flags, info->callback,
               info->context, info->devinfo, info->devinfo_data);
        case 1:
            return SetupInstallServicesFromInfSectionExW(*(HINF*)hinf, field, info->flags,
                info->devinfo, info->devinfo_data, info->reserved1, info->reserved2);
        default:
            ERR("Unknown info type %u\n", info->type);
            return FALSE;
    }
}


/***********************************************************************
 *            SetupInstallFromInfSectionW   (SETUPAPI.@)
 */
BOOL WINAPI SetupInstallFromInfSectionW( HWND owner, HINF hinf, PCWSTR section, UINT flags,
                                         HKEY key_root, PCWSTR src_root, UINT copy_flags,
                                         PSP_FILE_CALLBACK_W callback, PVOID context,
                                         HDEVINFO devinfo, PSP_DEVINFO_DATA devinfo_data )
{
    struct needs_callback_info needs_info;

    /* Parse 'Include' and 'Needs' directives */
    iterate_section_fields( hinf, section, L"Include", include_callback, NULL);
    needs_info.type = 0;
    needs_info.owner = owner;
    needs_info.flags = flags;
    needs_info.key_root = key_root;
    needs_info.src_root = src_root;
    needs_info.copy_flags = copy_flags;
    needs_info.callback = callback;
    needs_info.context = context;
    needs_info.devinfo = devinfo;
    needs_info.devinfo_data = devinfo_data;
    iterate_section_fields( hinf, section, L"Needs", needs_callback, &needs_info);

    if (flags & SPINST_FILES)
    {
        SP_DEVINSTALL_PARAMS_W install_params;
        struct files_callback_info info;
        HSPFILEQ queue = NULL;
        BOOL use_custom_queue;
        BOOL ret;

        install_params.cbSize = sizeof(SP_DEVINSTALL_PARAMS);
        use_custom_queue = SetupDiGetDeviceInstallParamsW(devinfo, devinfo_data, &install_params) && (install_params.Flags & DI_NOVCP);
        if (!use_custom_queue && ((queue = SetupOpenFileQueue()) == (HSPFILEQ)INVALID_HANDLE_VALUE ))
            return FALSE;
        info.queue      = use_custom_queue ? install_params.FileQueue : queue;
        info.src_root   = src_root;
        info.copy_flags = copy_flags;
        info.layout     = hinf;
        ret = (iterate_section_fields( hinf, section, L"CopyFiles", copy_files_callback, &info ) &&
               iterate_section_fields( hinf, section, L"DelFiles", delete_files_callback, &info ) &&
               iterate_section_fields( hinf, section, L"RenFiles", rename_files_callback, &info ));
        if (!use_custom_queue)
        {
            if (ret)
                ret = SetupCommitFileQueueW( owner, queue, callback, context );
            SetupCloseFileQueue( queue );
        }
        if (!ret) return FALSE;
    }
    if (flags & SPINST_INIFILES)
    {
        if (!iterate_section_fields( hinf, section, L"UpdateInis", update_ini_callback, NULL ) ||
            !iterate_section_fields( hinf, section, L"UpdateIniFields",
                                     update_ini_fields_callback, NULL ))
            return FALSE;
    }
    if (flags & SPINST_INI2REG)
    {
        if (!iterate_section_fields( hinf, section, L"Ini2Reg", ini2reg_callback, NULL ))
            return FALSE;
    }
    if (flags & SPINST_LOGCONFIG)
    {
        if (!iterate_section_fields( hinf, section, L"LogConf", logconf_callback, NULL ))
            return FALSE;
    }
    if (flags & SPINST_REGSVR)
    {
        struct register_dll_info info;

        info.unregister = FALSE;
        if (flags & SPINST_REGISTERCALLBACKAWARE)
        {
            info.callback         = callback;
            info.callback_context = context;
        }
        else info.callback = NULL;

        if (!iterate_section_fields( hinf, section, L"RegisterDlls", register_dlls_callback, &info ))
            return FALSE;

#ifdef __WINESRC__
        if (!iterate_section_fields( hinf, section, L"WineFakeDlls", fake_dlls_callback, NULL ))
            return FALSE;
#endif // __WINESRC__
    }
    if (flags & SPINST_UNREGSVR)
    {
        struct register_dll_info info;

        info.unregister = TRUE;
        if (flags & SPINST_REGISTERCALLBACKAWARE)
        {
            info.callback         = callback;
            info.callback_context = context;
        }
        else info.callback = NULL;

        if (!iterate_section_fields( hinf, section, L"UnregisterDlls", register_dlls_callback, &info ))
            return FALSE;
    }
    if (flags & SPINST_REGISTRY)
    {
        struct registry_callback_info info;

        info.default_root = key_root;
        info.delete = TRUE;
        if (!iterate_section_fields( hinf, section, L"DelReg", registry_callback, &info ))
            return FALSE;
        info.delete = FALSE;
        if (!iterate_section_fields( hinf, section, L"AddReg", registry_callback, &info ))
            return FALSE;
    }
    if (flags & SPINST_BITREG)
    {
        if (!iterate_section_fields( hinf, section, L"BitReg", bitreg_callback, NULL ))
            return FALSE;
    }
    if (flags & SPINST_PROFILEITEMS)
    {
        if (!iterate_section_fields( hinf, section, L"ProfileItems", profile_items_callback, NULL ))
            return FALSE;
    }
    if (flags & SPINST_COPYINF)
    {
        if (!iterate_section_fields( hinf, section, L"CopyINF", copy_inf_callback, NULL ))
            return FALSE;
    }

    SetLastError(ERROR_SUCCESS);
    return TRUE;
}


/***********************************************************************
 *		InstallHinfSectionW  (SETUPAPI.@)
 *
 * NOTE: 'cmdline' is <section> <mode> <path> from
 *   RUNDLL32.EXE SETUPAPI.DLL,InstallHinfSection <section> <mode> <path>
 */
void WINAPI InstallHinfSectionW( HWND hwnd, HINSTANCE handle, LPCWSTR cmdline, INT show )
{
    BOOL ret = FALSE;
    WCHAR *s, *path, section[MAX_PATH];
    void *callback_context = NULL;
    DWORD SectionNameLength;
    UINT mode;
    HINF hinf = INVALID_HANDLE_VALUE;
    BOOL bRebootRequired = FALSE;

    TRACE("hwnd %p, handle %p, cmdline %s\n", hwnd, handle, debugstr_w(cmdline));

    lstrcpynW( section, cmdline, MAX_PATH );

    if (!(s = strchrW( section, ' ' ))) goto cleanup;
    *s++ = 0;
    while (*s == ' ') s++;
    mode = atoiW( s );

    /* quoted paths are not allowed on native, the rest of the command line is taken as the path */
    if (!(s = strchrW( s, ' ' ))) goto cleanup;
    while (*s == ' ') s++;
    path = s;

    if (mode & 0x80)
    {
        FIXME("default path of the installation not changed\n");
        mode &= ~0x80;
    }

    hinf = SetupOpenInfFileW( path, NULL, INF_STYLE_WIN4, NULL );
    if (hinf == INVALID_HANDLE_VALUE)
    {
        WARN("SetupOpenInfFileW(%s) failed (Error %u)\n", path, GetLastError());
        goto cleanup;
    }

    ret = SetupDiGetActualSectionToInstallW(
       hinf, section, section, ARRAY_SIZE( section ), &SectionNameLength, NULL );
    if (!ret)
    {
        WARN("SetupDiGetActualSectionToInstallW() failed (Error %u)\n", GetLastError());
        goto cleanup;
    }
    if (SectionNameLength > MAX_PATH - strlenW(L".Services"))
    {
        WARN("Section name '%s' too long\n", section);
        goto cleanup;
    }

    /* Copy files and add registry entries */
    callback_context = SetupInitDefaultQueueCallback( hwnd );
    ret = SetupInstallFromInfSectionW( hwnd, hinf, section, SPINST_ALL, NULL, NULL,
                                       SP_COPY_NEWER | SP_COPY_IN_USE_NEEDS_REBOOT,
                                       SetupDefaultQueueCallbackW, callback_context,
                                       NULL, NULL );
    if (!ret)
    {
        WARN("SetupInstallFromInfSectionW() failed (Error %u)\n", GetLastError());
        goto cleanup;
    }
    /* FIXME: need to check if some files were in use and need reboot
     * bReboot = ...;
     */

    /* Install services */
    wcscat(section, L".Services");
    ret = SetupInstallServicesFromInfSectionW( hinf, section, 0 );
    if (!ret && GetLastError() == ERROR_SECTION_NOT_FOUND)
        ret = TRUE;
    if (!ret)
    {
        WARN("SetupInstallServicesFromInfSectionW() failed (Error %u)\n", GetLastError());
        goto cleanup;
    }
    else if (GetLastError() == ERROR_SUCCESS_REBOOT_REQUIRED)
    {
        bRebootRequired = TRUE;
    }

    /* Check if we need to reboot */
    switch (mode)
    {
        case 0:
            /* Never reboot */
            break;
        case 1:
            /* Always reboot */
            ExitWindowsEx(EWX_REBOOT, SHTDN_REASON_MAJOR_APPLICATION |
                SHTDN_REASON_MINOR_INSTALLATION | SHTDN_REASON_FLAG_PLANNED);
            break;
        case 2:
            /* Query user before rebooting */
            SetupPromptReboot(NULL, hwnd, FALSE);
            break;
        case 3:
            /* Reboot if necessary */
            if (bRebootRequired)
            {
                ExitWindowsEx(EWX_REBOOT, SHTDN_REASON_MAJOR_APPLICATION |
                    SHTDN_REASON_MINOR_INSTALLATION | SHTDN_REASON_FLAG_PLANNED);
            }
            break;
        case 4:
            /* If necessary, query user before rebooting */
            if (bRebootRequired)
            {
                SetupPromptReboot(NULL, hwnd, FALSE);
            }
            break;
        default:
            break;
    }

cleanup:
    if ( callback_context )
        SetupTermDefaultQueueCallback( callback_context );
    if ( hinf != INVALID_HANDLE_VALUE )
        SetupCloseInfFile( hinf );

#ifdef CORE_11689_IS_FIXED
    // TODO: Localize the error string.
    if (!ret && !(GlobalSetupFlags & PSPGF_NONINTERACTIVE))
    {
        MessageBoxW(hwnd, section, L"setupapi.dll: An error happened...", MB_ICONERROR | MB_OK);
    }
#endif
}


/***********************************************************************
 *		InstallHinfSectionA  (SETUPAPI.@)
 */
void WINAPI InstallHinfSectionA( HWND hwnd, HINSTANCE handle, LPCSTR cmdline, INT show )
{
    UNICODE_STRING cmdlineW;

    if (RtlCreateUnicodeStringFromAsciiz( &cmdlineW, cmdline ))
    {
        InstallHinfSectionW( hwnd, handle, cmdlineW.Buffer, show );
        RtlFreeUnicodeString( &cmdlineW );
    }
}

/***********************************************************************
 *              SetupInstallServicesFromInfSectionW  (SETUPAPI.@)
 */
BOOL WINAPI SetupInstallServicesFromInfSectionW( HINF Inf, PCWSTR SectionName, DWORD Flags)
{
    return SetupInstallServicesFromInfSectionExW( Inf, SectionName, Flags,
                                                  NULL, NULL, NULL, NULL );
}

/***********************************************************************
 *              SetupInstallServicesFromInfSectionA  (SETUPAPI.@)
 */
BOOL WINAPI SetupInstallServicesFromInfSectionA( HINF Inf, PCSTR SectionName, DWORD Flags)
{
    return SetupInstallServicesFromInfSectionExA( Inf, SectionName, Flags,
                                                  NULL, NULL, NULL, NULL );
}

/***********************************************************************
 *              SetupInstallServicesFromInfSectionExA  (SETUPAPI.@)
 */
BOOL WINAPI SetupInstallServicesFromInfSectionExA( HINF hinf, PCSTR sectionname, DWORD flags, HDEVINFO devinfo, PSP_DEVINFO_DATA devinfo_data, PVOID reserved1, PVOID reserved2 )
{
    UNICODE_STRING sectionnameW;
    BOOL ret = FALSE;

    if (RtlCreateUnicodeStringFromAsciiz( &sectionnameW, sectionname ))
    {
        ret = SetupInstallServicesFromInfSectionExW( hinf, sectionnameW.Buffer, flags, devinfo, devinfo_data, reserved1, reserved2 );
        RtlFreeUnicodeString( &sectionnameW );
    }
    else
        SetLastError( ERROR_NOT_ENOUGH_MEMORY );

    return ret;
}


static BOOL GetLineText( HINF hinf, PCWSTR section_name, PCWSTR key_name, PWSTR *value)
{
    DWORD required;
    PWSTR buf = NULL;

    *value = NULL;

    if (! SetupGetLineTextW( NULL, hinf, section_name, key_name, NULL, 0, &required )
        && GetLastError() != ERROR_INSUFFICIENT_BUFFER )
        return FALSE;

    buf = HeapAlloc( GetProcessHeap(), 0, required * sizeof(WCHAR) );
    if ( ! buf )
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    if (! SetupGetLineTextW( NULL, hinf, section_name, key_name, buf, required, &required ) )
    {
        HeapFree( GetProcessHeap(), 0, buf );
        return FALSE;
    }

    *value = buf;
    return TRUE;
}

static BOOL GetIntField( HINF hinf, PCWSTR section_name, PCWSTR key_name, INT *value)
{
    LPWSTR buffer, end;
    INT res;

    if (! GetLineText( hinf, section_name, key_name, &buffer ) )
        return FALSE;

    res = wcstol( buffer, &end, 0 );
    if (end != buffer && !*end)
    {
        HeapFree(GetProcessHeap(), 0, buffer);
        *value = res;
        return TRUE;
    }
    else
    {
        HeapFree(GetProcessHeap(), 0, buffer);
        SetLastError( ERROR_INVALID_DATA );
        return FALSE;
    }
}

BOOL GetStringField( PINFCONTEXT context, DWORD index, PWSTR *value)
{
    DWORD RequiredSize;
    BOOL ret;

    ret = SetupGetStringFieldW(
        context,
        index,
        NULL, 0,
        &RequiredSize);
    if (!ret)
        return FALSE;
    else if (RequiredSize == 0)
    {
        *value = NULL;
        return TRUE;
    }

    /* We got the needed size for the buffer */
    *value = MyMalloc(RequiredSize * sizeof(WCHAR));
    if (!*value)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    ret = SetupGetStringFieldW(
        context,
        index,
        *value, RequiredSize, NULL);
    if (!ret)
        MyFree(*value);

    return ret;
}

static VOID FixupServiceBinaryPath(
    IN DWORD ServiceType,
    IN OUT LPWSTR *ServiceBinary)
{
    LPWSTR Buffer;
    WCHAR ReactOSDir[MAX_PATH];
    DWORD RosDirLength, ServiceLength, Win32Length;

    GetWindowsDirectoryW(ReactOSDir, MAX_PATH);
    RosDirLength = strlenW(ReactOSDir);
    ServiceLength = strlenW(*ServiceBinary);

    /* Check and fix two things:
       1. Get rid of C:\ReactOS and use relative
          path instead.
       2. Add %SystemRoot% for Win32 services */

    if (ServiceLength < RosDirLength)
        return;

    if (!wcsnicmp(*ServiceBinary, ReactOSDir, RosDirLength))
    {
        /* Yes, the first part is the C:\ReactOS\, just skip it */
        MoveMemory(*ServiceBinary, *ServiceBinary + RosDirLength + 1,
            (ServiceLength - RosDirLength) * sizeof(WCHAR));

        /* Handle Win32-services differently */
        if (ServiceType & SERVICE_WIN32)
        {
            Win32Length = (ServiceLength - RosDirLength) * sizeof(WCHAR)
                        - sizeof(L'\\') + sizeof(L"%SystemRoot%\\");
            Buffer = MyMalloc(Win32Length);

            wcscpy(Buffer, L"%SystemRoot%\\");
            wcscat(Buffer, *ServiceBinary);
            MyFree(*ServiceBinary);

            *ServiceBinary = Buffer;
        }
    }
}

/***********************************************************************
 *            add_service
 *
 * Create a new service. Helper for SetupInstallServicesFromInfSectionW.
 */
static BOOL add_service(
    IN SC_HANDLE hSCManager,
    IN HKEY hkMachine,
    IN HINF hInf,
    IN LPCWSTR ServiceSection,
    IN LPCWSTR ServiceName,
    IN UINT ServiceFlags)
{
    SC_HANDLE hService = NULL;
    LPDWORD GroupOrder = NULL;
    LPQUERY_SERVICE_CONFIGW ServiceConfig = NULL;
    HKEY hServicesKey, hServiceKey;
    LONG rc;
    BOOL ret = FALSE;

    HKEY hGroupOrderListKey = NULL;
    LPWSTR ServiceBinary = NULL;
    LPWSTR LoadOrderGroup = NULL;
    LPWSTR DisplayName = NULL;
    SERVICE_DESCRIPTIONW Description = {NULL};
    LPWSTR Dependencies = NULL;
    LPWSTR StartName = NULL;
    LPWSTR SecurityDescriptor = NULL;
    PSECURITY_DESCRIPTOR sd = NULL;
    INT ServiceType, StartType, ErrorControl;
    DWORD dwRegType;
    DWORD tagId = (DWORD)-1;
    BOOL useTag;

    if (!GetIntField(hInf, ServiceSection, L"ServiceType", &ServiceType))
    {
        SetLastError( ERROR_BAD_SERVICE_INSTALLSECT );
        goto cleanup;
    }
    if (!GetIntField(hInf, ServiceSection, L"StartType", &StartType))
    {
        SetLastError( ERROR_BAD_SERVICE_INSTALLSECT );
        goto cleanup;
    }
    if (!GetIntField(hInf, ServiceSection, L"ErrorControl", &ErrorControl))
    {
        SetLastError( ERROR_BAD_SERVICE_INSTALLSECT );
        goto cleanup;
    }
    useTag = (ServiceType == SERVICE_BOOT_START || ServiceType == SERVICE_SYSTEM_START);

    if (!GetLineText(hInf, ServiceSection, L"ServiceBinary", &ServiceBinary))
    {
        SetLastError( ERROR_BAD_SERVICE_INSTALLSECT );
        goto cleanup;
    }

    /* Adjust binary path according to the service type */
    FixupServiceBinaryPath(ServiceType, &ServiceBinary);

    /* Don't check return value, as these fields are optional and
     * GetLineText initialize output parameter even on failure */
    GetLineText(hInf, ServiceSection, L"LoadOrderGroup", &LoadOrderGroup);
    GetLineText(hInf, ServiceSection, L"DisplayName", &DisplayName);
    GetLineText(hInf, ServiceSection, L"Description", &Description.lpDescription);
    GetLineText(hInf, ServiceSection, L"Dependencies", &Dependencies);
    GetLineText(hInf, ServiceSection, L"StartName", &StartName);

    /* If there is no group, we must not request a tag */
    if (!LoadOrderGroup || !*LoadOrderGroup)
        useTag = FALSE;

    hService = OpenServiceW(hSCManager,
                            ServiceName,
                            SERVICE_QUERY_CONFIG | SERVICE_CHANGE_CONFIG | WRITE_DAC);
    if (hService == NULL && GetLastError() != ERROR_SERVICE_DOES_NOT_EXIST)
        goto cleanup;

    if (hService == NULL)
    {
        /* Create new service */
        hService = CreateServiceW(hSCManager,
                                  ServiceName,
                                  DisplayName,
                                  WRITE_DAC,
                                  ServiceType,
                                  StartType,
                                  ErrorControl,
                                  ServiceBinary,
                                  LoadOrderGroup,
                                  useTag ? &tagId : NULL,
                                  Dependencies,
                                  StartName,
                                  NULL);
        if (hService == NULL)
            goto cleanup;

        if (Description.lpDescription)
            ChangeServiceConfig2W(hService, SERVICE_CONFIG_DESCRIPTION, &Description);
    }
    else
    {
        DWORD bufferSize;

        /* Read current configuration */
        if (!QueryServiceConfigW(hService, NULL, 0, &bufferSize))
        {
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
                goto cleanup;
            ServiceConfig = MyMalloc(bufferSize);
            if (!ServiceConfig)
            {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                goto cleanup;
            }
            if (!QueryServiceConfigW(hService, ServiceConfig, bufferSize, &bufferSize))
                goto cleanup;
        }
        tagId = ServiceConfig->dwTagId;

        /* Update configuration */
        ret = ChangeServiceConfigW(hService,
                                   ServiceType,
                                   (ServiceFlags & SPSVCINST_NOCLOBBER_STARTTYPE) ? SERVICE_NO_CHANGE : StartType,
                                   (ServiceFlags & SPSVCINST_NOCLOBBER_ERRORCONTROL) ? SERVICE_NO_CHANGE : ErrorControl,
                                   ServiceBinary,
                                   (ServiceFlags & SPSVCINST_NOCLOBBER_LOADORDERGROUP && ServiceConfig->lpLoadOrderGroup) ? NULL : LoadOrderGroup,
                                   useTag ? &tagId : NULL,
                                   (ServiceFlags & SPSVCINST_NOCLOBBER_DEPENDENCIES && ServiceConfig->lpDependencies) ? NULL : Dependencies,
                                   StartName,
                                   NULL,
                                   (ServiceFlags & SPSVCINST_NOCLOBBER_DISPLAYNAME && ServiceConfig->lpDisplayName) ? NULL : DisplayName);
        if (!ret)
            goto cleanup;

        if (!(ServiceFlags & SPSVCINST_NOCLOBBER_DESCRIPTION))
            ChangeServiceConfig2W(hService, SERVICE_CONFIG_DESCRIPTION, &Description);
    }

    /* Set security */
    // if (ServiceFlags & SPSVCINST_CLOBBER_SECURITY)
    {
        if (GetLineText(hInf, ServiceSection, L"Security", &SecurityDescriptor))
        {
            ret = ConvertStringSecurityDescriptorToSecurityDescriptorW(SecurityDescriptor, SDDL_REVISION_1, &sd, NULL);
            MyFree(SecurityDescriptor);
            if (!ret)
                goto cleanup;

            ret = SetServiceObjectSecurity(hService, DACL_SECURITY_INFORMATION, sd);
            if (sd != NULL)
                LocalFree(sd);
            if (!ret)
                goto cleanup;
        }
    }

    if (useTag)
    {
        /* Add the tag to SYSTEM\CurrentControlSet\Control\GroupOrderList key */
        LPCWSTR lpLoadOrderGroup;
        DWORD bufferSize;

        lpLoadOrderGroup = LoadOrderGroup;
        if ((ServiceFlags & SPSVCINST_NOCLOBBER_LOADORDERGROUP) && ServiceConfig && ServiceConfig->lpLoadOrderGroup)
            lpLoadOrderGroup = ServiceConfig->lpLoadOrderGroup;

        rc = RegOpenKeyW(hkMachine,
                         L"SYSTEM\\CurrentControlSet\\Control\\GroupOrderList",
                         &hGroupOrderListKey);
        if (rc != ERROR_SUCCESS)
        {
            SetLastError(rc);
            goto cleanup;
        }
        rc = RegQueryValueExW(hGroupOrderListKey, lpLoadOrderGroup, NULL,
                              &dwRegType, NULL, &bufferSize);
        if (rc == ERROR_FILE_NOT_FOUND)
            bufferSize = sizeof(DWORD);
        else if (rc != ERROR_SUCCESS)
        {
            SetLastError(rc);
            goto cleanup;
        }
        else if (dwRegType != REG_BINARY || bufferSize == 0 || bufferSize % sizeof(DWORD) != 0)
        {
            SetLastError(ERROR_GEN_FAILURE);
            goto cleanup;
        }
        /* Allocate buffer to store existing data + the new tag */
        GroupOrder = MyMalloc(bufferSize + sizeof(DWORD));
        if (!GroupOrder)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            goto cleanup;
        }
        if (rc == ERROR_SUCCESS)
        {
            /* Read existing data */
            rc = RegQueryValueExW(hGroupOrderListKey,
                                  lpLoadOrderGroup,
                                  NULL,
                                  NULL,
                                  (BYTE*)GroupOrder,
                                  &bufferSize);
            if (rc != ERROR_SUCCESS)
            {
                SetLastError(rc);
                goto cleanup;
            }
            if (ServiceFlags & SPSVCINST_TAGTOFRONT)
                MoveMemory(&GroupOrder[2], &GroupOrder[1], bufferSize - sizeof(DWORD));
        }
        else
        {
            GroupOrder[0] = 0;
        }
        GroupOrder[0]++;
        if (ServiceFlags & SPSVCINST_TAGTOFRONT)
            GroupOrder[1] = tagId;
        else
            GroupOrder[bufferSize / sizeof(DWORD)] = tagId;

        rc = RegSetValueExW(hGroupOrderListKey,
                            lpLoadOrderGroup,
                            0,
                            REG_BINARY,
                            (BYTE*)GroupOrder,
                            bufferSize + sizeof(DWORD));
        if (rc != ERROR_SUCCESS)
        {
            SetLastError(rc);
            goto cleanup;
        }
    }

    /* Handle AddReg, DelReg and BitReg entries */
    rc = RegOpenKeyExW(hkMachine,
                       REGSTR_PATH_SERVICES,
                       0,
                       READ_CONTROL,
                       &hServicesKey);
    if (rc != ERROR_SUCCESS)
    {
        SetLastError(rc);
        goto cleanup;
    }
    rc = RegOpenKeyExW(hServicesKey,
                       ServiceName,
                       0,
                       KEY_READ | KEY_WRITE,
                       &hServiceKey);
    RegCloseKey(hServicesKey);
    if (rc != ERROR_SUCCESS)
    {
        SetLastError(rc);
        goto cleanup;
    }

    ret = SetupInstallFromInfSectionW(NULL,
                                      hInf,
                                      ServiceSection,
                                      SPINST_REGISTRY | SPINST_BITREG,
                                      hServiceKey,
                                      NULL,
                                      0,
                                      NULL,
                                      NULL,
                                      NULL,
                                      NULL);
    RegCloseKey(hServiceKey);

    if (ServiceFlags & SPSVCINST_STARTSERVICE)
        StartServiceW(hService, 0, NULL);

cleanup:
    if (hService != NULL)
        CloseServiceHandle(hService);
    if (hGroupOrderListKey != NULL)
        RegCloseKey(hGroupOrderListKey);
    MyFree(ServiceConfig);
    MyFree(ServiceBinary);
    MyFree(LoadOrderGroup);
    MyFree(DisplayName);
    MyFree(Description.lpDescription);
    MyFree(Dependencies);
    MyFree(GroupOrder);
    MyFree(StartName);

    TRACE("Returning %d\n", ret);
    return ret;
}


/***********************************************************************
 *            del_service
 *
 * Delete service. Helper for SetupInstallServicesFromInfSectionW.
 */
static BOOL del_service(
    IN SC_HANDLE hSCManager,
    IN HINF hInf,
    IN LPCWSTR ServiceName,
    IN UINT ServiceFlags)
{
    BOOL ret;
    SC_HANDLE hService;
    SERVICE_STATUS ServiceStatus;

    hService = OpenServiceW(hSCManager,
                            ServiceName,
                            SERVICE_STOP | DELETE);
    if (!hService)
    {
        if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST)
        {
            ret = TRUE;
            goto cleanup;
        }
        WARN( "cannot open %s err %u\n", debugstr_w(ServiceName), GetLastError() );
        ret = FALSE;
        goto cleanup;
    }

    if (ServiceFlags & SPSVCINST_STOPSERVICE)
    {
        ret = ControlService(hService, SERVICE_CONTROL_STOP, &ServiceStatus);
        if (!ret && GetLastError() != ERROR_SERVICE_NOT_ACTIVE)
            goto cleanup;
        if (ServiceStatus.dwCurrentState != SERVICE_STOP_PENDING && ServiceStatus.dwCurrentState != SERVICE_STOPPED)
        {
            // SetLastError(ERROR_INSTALL_SERVICE_FAILURE);
            goto cleanup;
        }

        /* This may lead to require a reboot */
        // bNeedReboot = TRUE;
    }

    TRACE( "deleting %s\n", debugstr_w(ServiceName) );
    ret = DeleteService(hService);
    if (!ret && GetLastError() != ERROR_SERVICE_MARKED_FOR_DELETE)
        goto cleanup;

    // FIXME: TODO!
    // if (ServiceFlags & SPSVCINST_DELETEEVENTLOGENTRY)

cleanup:
    if (hService != NULL)
        CloseServiceHandle(hService);

    return ret;
}


/***********************************************************************
 *              SetupInstallServicesFromInfSectionExW  (SETUPAPI.@)
 */
BOOL WINAPI SetupInstallServicesFromInfSectionExW( HINF hinf, PCWSTR sectionname, DWORD flags, HDEVINFO DeviceInfoSet, PSP_DEVINFO_DATA DeviceInfoData, PVOID reserved1, PVOID reserved2 )
{
    struct DeviceInfoSet *list = NULL;
    BOOL ret = FALSE;
    SC_HANDLE hSCManager = NULL;

    TRACE("%p, %s, 0x%lx, %p, %p, %p, %p\n", hinf, debugstr_w(sectionname),
        flags, DeviceInfoSet, DeviceInfoData, reserved1, reserved2);

    if (!sectionname)
        SetLastError(ERROR_INVALID_PARAMETER);
    else if (flags & ~(SPSVCINST_TAGTOFRONT | SPSVCINST_DELETEEVENTLOGENTRY | SPSVCINST_NOCLOBBER_DISPLAYNAME | SPSVCINST_NOCLOBBER_STARTTYPE | SPSVCINST_NOCLOBBER_ERRORCONTROL | SPSVCINST_NOCLOBBER_LOADORDERGROUP | SPSVCINST_NOCLOBBER_DEPENDENCIES | SPSVCINST_STOPSERVICE))
    {
        TRACE("Unknown flags: 0x%08lx\n", flags & ~(SPSVCINST_TAGTOFRONT | SPSVCINST_DELETEEVENTLOGENTRY | SPSVCINST_NOCLOBBER_DISPLAYNAME | SPSVCINST_NOCLOBBER_STARTTYPE | SPSVCINST_NOCLOBBER_ERRORCONTROL | SPSVCINST_NOCLOBBER_LOADORDERGROUP | SPSVCINST_NOCLOBBER_DEPENDENCIES | SPSVCINST_STOPSERVICE));
        SetLastError(ERROR_INVALID_FLAGS);
    }
    else if (DeviceInfoSet == (HDEVINFO)INVALID_HANDLE_VALUE)
        SetLastError(ERROR_INVALID_HANDLE);
    else if (DeviceInfoSet && (list = (struct DeviceInfoSet *)DeviceInfoSet)->magic != SETUP_DEVICE_INFO_SET_MAGIC)
        SetLastError(ERROR_INVALID_HANDLE);
    else if (DeviceInfoData && DeviceInfoData->cbSize != sizeof(SP_DEVINFO_DATA))
        SetLastError(ERROR_INVALID_USER_BUFFER);
    else if (reserved1 != NULL || reserved2 != NULL)
        SetLastError(ERROR_INVALID_PARAMETER);
    else
    {
        struct needs_callback_info needs_info;
        LPWSTR ServiceName = NULL;
        LPWSTR ServiceSection = NULL;
        INT ServiceFlags;
        INFCONTEXT ContextService;
        BOOL bNeedReboot = FALSE;

        /* Parse 'Include' and 'Needs' directives */
        iterate_section_fields( hinf, sectionname, L"Include", include_callback, NULL);
        needs_info.type = 1;
        needs_info.flags = flags;
        needs_info.devinfo = DeviceInfoSet;
        needs_info.devinfo_data = DeviceInfoData;
        needs_info.reserved1 = reserved1;
        needs_info.reserved2 = reserved2;
        iterate_section_fields( hinf, sectionname, L"Needs", needs_callback, &needs_info);

        if (!(ret = SetupFindFirstLineW( hinf, sectionname, NULL, &ContextService )))
        {
            SetLastError( ERROR_SECTION_NOT_FOUND );
            goto done;
        }

        /*
         * Process the 'AddService' and 'DelService' directives.
         */

        hSCManager = OpenSCManagerW(list ? list->MachineName : NULL,
                                    SERVICES_ACTIVE_DATABASE,
                                    SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
        if (hSCManager == NULL)
            goto done;

        ret = SetupFindFirstLineW(hinf, sectionname, L"AddService", &ContextService);
        while (ret)
        {
            if (!GetStringField(&ContextService, 1, &ServiceName))
                goto done;

            ret = SetupGetIntField(&ContextService,
                                   2, /* Field index */
                                   &ServiceFlags);
            if (!ret)
            {
                /* The field may be empty. Ignore the error */
                ServiceFlags = 0;
            }

            if (!GetStringField(&ContextService, 3, &ServiceSection))
            {
                HeapFree(GetProcessHeap(), 0, ServiceName);
                goto done;
            }

            ret = add_service(hSCManager, list ? list->HKLM : HKEY_LOCAL_MACHINE,
                              hinf, ServiceSection, ServiceName, (ServiceFlags & ~SPSVCINST_ASSOCSERVICE) | flags);
            if (!ret)
            {
                HeapFree(GetProcessHeap(), 0, ServiceName);
                HeapFree(GetProcessHeap(), 0, ServiceSection);
                goto done;
            }

            if (ServiceFlags & SPSVCINST_ASSOCSERVICE)
            {
                ret = SetupDiSetDeviceRegistryPropertyW(DeviceInfoSet, DeviceInfoData, SPDRP_SERVICE, (LPBYTE)ServiceName, (strlenW(ServiceName) + 1) * sizeof(WCHAR));
                if (!ret)
                {
                    HeapFree(GetProcessHeap(), 0, ServiceName);
                    HeapFree(GetProcessHeap(), 0, ServiceSection);
                    goto done;
                }
            }

            HeapFree(GetProcessHeap(), 0, ServiceName);
            HeapFree(GetProcessHeap(), 0, ServiceSection);
            ServiceName = ServiceSection = NULL;
            ret = SetupFindNextMatchLineW(&ContextService, L"AddService", &ContextService);
        }

        ret = SetupFindFirstLineW(hinf, sectionname, L"DelService", &ContextService);
        while (ret)
        {
            if (!GetStringField(&ContextService, 1, &ServiceName))
                goto done;

            ret = SetupGetIntField(&ContextService,
                                   2, /* Field index */
                                   &ServiceFlags);
            if (!ret)
            {
                /* The field may be empty. Ignore the error */
                ServiceFlags = 0;
            }

            ret = del_service(hSCManager, hinf, ServiceName, (ServiceFlags & ~SPSVCINST_ASSOCSERVICE) | flags);
            if (!ret)
            {
                HeapFree(GetProcessHeap(), 0, ServiceName);
                goto done;
            }

            HeapFree(GetProcessHeap(), 0, ServiceName);
            ServiceName = NULL;
            ret = SetupFindNextMatchLineW(&ContextService, L"DelService", &ContextService);
        }

        if (bNeedReboot)
            SetLastError(ERROR_SUCCESS_REBOOT_REQUIRED);
        else
            SetLastError(ERROR_SUCCESS);
        ret = TRUE;
    }

done:
    if (hSCManager != NULL)
        CloseServiceHandle(hSCManager);

    TRACE("Returning %d\n", ret);
    return ret;
}


/***********************************************************************
 *              SetupGetInfFileListA    (SETUPAPI.@)
 */
BOOL WINAPI
SetupGetInfFileListA(
    IN PCSTR DirectoryPath OPTIONAL,
    IN DWORD InfStyle,
    IN OUT PSTR ReturnBuffer OPTIONAL,
    IN DWORD ReturnBufferSize OPTIONAL,
    OUT PDWORD RequiredSize OPTIONAL)
{
    PWSTR DirectoryPathW = NULL;
    PWSTR ReturnBufferW = NULL;
    BOOL ret = FALSE;

    TRACE("%s %lx %p %ld %p\n", debugstr_a(DirectoryPath), InfStyle,
        ReturnBuffer, ReturnBufferSize, RequiredSize);

    if (DirectoryPath != NULL)
    {
        DirectoryPathW = pSetupMultiByteToUnicode(DirectoryPath, CP_ACP);
        if (DirectoryPathW == NULL) goto Cleanup;
    }

    if (ReturnBuffer != NULL && ReturnBufferSize != 0)
    {
        ReturnBufferW = MyMalloc(ReturnBufferSize * sizeof(WCHAR));
        if (ReturnBufferW == NULL) goto Cleanup;
    }

    ret = SetupGetInfFileListW(DirectoryPathW, InfStyle, ReturnBufferW, ReturnBufferSize, RequiredSize);

    if (ret && ReturnBufferW != NULL)
    {
        ret = WideCharToMultiByte(CP_ACP, 0, ReturnBufferW, -1, ReturnBuffer, ReturnBufferSize, NULL, NULL) != 0;
    }

Cleanup:
    MyFree(DirectoryPathW);
    MyFree(ReturnBufferW);

    return ret;
}


/***********************************************************************
 *              SetupGetInfFileListW    (SETUPAPI.@)
 */
BOOL WINAPI
SetupGetInfFileListW(
    IN PCWSTR DirectoryPath OPTIONAL,
    IN DWORD InfStyle,
    IN OUT PWSTR ReturnBuffer OPTIONAL,
    IN DWORD ReturnBufferSize OPTIONAL,
    OUT PDWORD RequiredSize OPTIONAL)
{
    HANDLE hSearch;
    LPWSTR pFullFileName = NULL;
    LPWSTR pFileName; /* Pointer into pFullFileName buffer */
    LPWSTR pBuffer = ReturnBuffer;
    WIN32_FIND_DATAW wfdFileInfo;
    size_t len;
    DWORD requiredSize = 0;
    BOOL ret = FALSE;

    TRACE("%s %lx %p %ld %p\n", debugstr_w(DirectoryPath), InfStyle,
        ReturnBuffer, ReturnBufferSize, RequiredSize);

    if (InfStyle & ~(INF_STYLE_OLDNT | INF_STYLE_WIN4))
    {
        TRACE("Unknown flags: 0x%08lx\n", InfStyle & ~(INF_STYLE_OLDNT  | INF_STYLE_WIN4));
        SetLastError(ERROR_INVALID_PARAMETER);
        goto cleanup;
    }
    else if (ReturnBufferSize == 0 && ReturnBuffer != NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        goto cleanup;
    }
    else if (ReturnBufferSize > 0 && ReturnBuffer == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        goto cleanup;
    }

    /* Allocate memory for file filter */
    if (DirectoryPath != NULL)
        /* "DirectoryPath\" form */
        len = strlenW(DirectoryPath) + 1 + 1;
    else
        /* "%SYSTEMROOT%\Inf\" form */
        len = MAX_PATH + 1 + strlenW(L"inf\\") + 1;
    len += MAX_PATH; /* To contain file name or "*.inf" string */
    pFullFileName = MyMalloc(len * sizeof(WCHAR));
    if (pFullFileName == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        goto cleanup;
    }

    /* Fill file filter buffer */
    if (DirectoryPath)
    {
        strcpyW(pFullFileName, DirectoryPath);
        if (*pFullFileName && pFullFileName[strlenW(pFullFileName) - 1] != '\\')
            strcatW(pFullFileName, L"\\");
    }
    else
    {
        len = GetSystemWindowsDirectoryW(pFullFileName, MAX_PATH);
        if (len == 0 || len > MAX_PATH)
            goto cleanup;
        if (pFullFileName[strlenW(pFullFileName) - 1] != '\\')
            strcatW(pFullFileName, L"\\");
        strcatW(pFullFileName, L"inf\\");
    }
    pFileName = &pFullFileName[strlenW(pFullFileName)];

    /* Search for the first file */
    strcpyW(pFileName, L"*.inf");
    hSearch = FindFirstFileW(pFullFileName, &wfdFileInfo);
    if (hSearch == INVALID_HANDLE_VALUE)
    {
        TRACE("No file returned by %s\n", debugstr_w(pFullFileName));
        goto cleanup;
    }

    do
    {
        HINF hInf;

        strcpyW(pFileName, wfdFileInfo.cFileName);
        hInf = SetupOpenInfFileW(pFullFileName,
                                 NULL, /* Inf class */
                                 InfStyle,
                                 NULL /* Error line */);
        if (hInf == INVALID_HANDLE_VALUE)
        {
            if (GetLastError() == ERROR_CLASS_MISMATCH)
            {
                /* InfStyle was not correct. Skip this file */
                continue;
            }
            TRACE("Invalid .inf file %s\n", debugstr_w(pFullFileName));
            continue;
        }

        len = strlenW(wfdFileInfo.cFileName) + 1;
        requiredSize += (DWORD)len;
        if (requiredSize <= ReturnBufferSize)
        {
            strcpyW(pBuffer, wfdFileInfo.cFileName);
            pBuffer = &pBuffer[len];
        }
        SetupCloseInfFile(hInf);
    } while (FindNextFileW(hSearch, &wfdFileInfo));
    FindClose(hSearch);

    requiredSize += 1; /* Final NULL char */
    if (requiredSize <= ReturnBufferSize)
    {
        *pBuffer = '\0';
        ret = TRUE;
    }
    else
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        ret = FALSE;
    }
    if (RequiredSize)
        *RequiredSize = requiredSize;

cleanup:
    MyFree(pFullFileName);
    return ret;
}
