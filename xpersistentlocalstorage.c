/*
 * Xbox Game runtime Library
 *  GDK Component: System API -> XPersistentLocalStorage
 *
 * Copyright 2026 Olivia Ryan
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

#include "private.h"

/* Used to give each title its own storage folder. Exported by kernelbase (and
 * forwarded from kernel32), but wine's appmodel.h does not declare it. */
LONG WINAPI GetCurrentPackageFamilyName( UINT32 *length, WCHAR *name );

struct x_persistent_local_storage
{
    IXPersistentLocalStorageImpl3 IXPersistentLocalStorageImpl3_iface;
    LONG ref;
};

WINE_DEFAULT_DEBUG_CHANNEL(gdkc);

static inline struct x_persistent_local_storage *impl_from_IXPersistentLocalStorageImpl3( IXPersistentLocalStorageImpl3 *iface )
{
    return CONTAINING_RECORD( iface, struct x_persistent_local_storage, IXPersistentLocalStorageImpl3_iface );
}

static HRESULT WINAPI x_persistent_local_storage_QueryInterface( IXPersistentLocalStorageImpl3 *iface, REFIID iid, void **out )
{
    struct x_persistent_local_storage *impl = impl_from_IXPersistentLocalStorageImpl3( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown                      ) ||
        IsEqualGUID( iid, &IID_IXPersistentLocalStorageImpl  ) ||
        IsEqualGUID( iid, &IID_IXPersistentLocalStorageImpl2 ) ||
        IsEqualGUID( iid, &IID_IXPersistentLocalStorageImpl3 ))
    {
        IXPersistentLocalStorageImpl_AddRef( *out = &impl->IXPersistentLocalStorageImpl3_iface );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI x_persistent_local_storage_AddRef( IXPersistentLocalStorageImpl3 *iface )
{
    struct x_persistent_local_storage *impl = impl_from_IXPersistentLocalStorageImpl3( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI x_persistent_local_storage_Release( IXPersistentLocalStorageImpl3 *iface )
{
    struct x_persistent_local_storage *impl = impl_from_IXPersistentLocalStorageImpl3( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    return ref;
}

/*
 * Persistent local storage is the title's private writable folder. E_NOTIMPL is
 * not a safe way to say "unavailable" here: callers ask for the path length
 * first and then use it, so a stub that never writes the out parameter leaves
 * them acting on an uninitialised size. Asphalt Legends dereferences null
 * immediately afterwards and dies before drawing a frame. Provide a real
 * directory instead.
 *
 * Location mirrors where a packaged app's local state lives on Windows, keyed
 * by package family name so two titles cannot collide:
 *   %LOCALAPPDATA%\Packages\<family>\PersistentLocalStorage\
 * The trailing separator is part of the contract -- callers concatenate file
 * names straight onto it.
 */
static char pls_path[MAX_PATH * 3];
static INIT_ONCE pls_path_once = INIT_ONCE_STATIC_INIT;

/* CreateDirectoryW only creates the leaf, so make each component in turn. */
static void create_directory_tree( WCHAR *path )
{
    WCHAR *p;

    for (p = path; *p; p++)
    {
        if (*p != '\\' || p == path) continue;
        *p = 0;
        CreateDirectoryW( path, NULL );
        *p = '\\';
    }
    CreateDirectoryW( path, NULL );
}

static BOOL WINAPI init_pls_path( INIT_ONCE *once, void *param, void **context )
{
    WCHAR path[MAX_PATH], family[256];
    UINT32 family_len = ARRAY_SIZE(family);

    if (!GetEnvironmentVariableW( L"LOCALAPPDATA", path, ARRAY_SIZE(path) ))
    {
        ERR( "LOCALAPPDATA is not set, persistent local storage is unavailable\n" );
        return TRUE;
    }

    /* An unpackaged process has no family name; keep it in its own folder
     * rather than letting every such title share one. */
    if (GetCurrentPackageFamilyName( &family_len, family )) lstrcpyW( family, L"UnknownPackage" );

    lstrcatW( path, L"\\Packages\\" );
    lstrcatW( path, family );
    lstrcatW( path, L"\\PersistentLocalStorage\\" );
    create_directory_tree( path );

    if (!WideCharToMultiByte( CP_ACP, 0, path, -1, pls_path, sizeof(pls_path), NULL, NULL ))
        pls_path[0] = 0;

    TRACE( "persistent local storage at %s\n", debugstr_a( pls_path ) );
    return TRUE;
}

static const char *persistent_local_storage_path(void)
{
    InitOnceExecuteOnce( &pls_path_once, init_pls_path, NULL, NULL );
    return pls_path[0] ? pls_path : NULL;
}

static HRESULT WINAPI x_persistent_local_storage_XPersistentLocalStorageGetPath( IXPersistentLocalStorageImpl3 *iface, SIZE_T pathSize, char *path, SIZE_T *pathUsed )
{
    const char *storage = persistent_local_storage_path();
    SIZE_T needed;

    TRACE( "iface %p, pathSize %Iu, path %p, pathUsed %p.\n", iface, pathSize, path, pathUsed );

    if (!storage) return E_FAIL;
    if (!path) return E_POINTER;

    needed = strlen( storage ) + 1;
    if (pathSize < needed) return E_NOT_SUFFICIENT_BUFFER;

    memcpy( path, storage, needed );
    if (pathUsed) *pathUsed = needed;
    return S_OK;
}

static HRESULT WINAPI x_persistent_local_storage_XPersistentLocalStorageGetPathSize( IXPersistentLocalStorageImpl3 *iface, SIZE_T *pathSize )
{
    const char *storage = persistent_local_storage_path();

    TRACE( "iface %p, pathSize %p.\n", iface, pathSize );

    if (!pathSize) return E_POINTER;
    if (!storage) return E_FAIL;

    *pathSize = strlen( storage ) + 1;
    return S_OK;
}

static HRESULT WINAPI x_persistent_local_storage_XPersistentLocalStorageGetSpaceInfo( IXPersistentLocalStorageImpl3 *iface, XPersistentLocalStorageSpaceInfo *info )
{
    const char *storage = persistent_local_storage_path();
    ULARGE_INTEGER available, total, free_bytes;
    WCHAR path[MAX_PATH];

    TRACE( "iface %p, info %p.\n", iface, info );

    if (!info) return E_POINTER;
    if (!storage) return E_FAIL;
    if (!MultiByteToWideChar( CP_ACP, 0, storage, -1, path, ARRAY_SIZE(path) ))
        return HRESULT_FROM_WIN32( GetLastError() );
    if (!GetDiskFreeSpaceExW( path, &available, &total, &free_bytes ))
        return HRESULT_FROM_WIN32( GetLastError() );

    /* A console gives each title a fixed quota; here the backing filesystem is
     * the only limit, so report that. */
    info->availableFreeBytes = available.QuadPart;
    info->totalFreeBytes = free_bytes.QuadPart;
    info->totalBytes = total.QuadPart;
    info->usedBytes = total.QuadPart - free_bytes.QuadPart;
    return S_OK;
}

static HRESULT WINAPI x_persistent_local_storage_XPersistentLocalStorageMountForPackage( IXPersistentLocalStorageImpl3 *iface, const char *packageIdentifier, XPackageMountHandle *mountHandle )
{
    FIXME( "iface %p, packageIdentifier %s, mountHandle %p stub!\n", iface, debugstr_a( packageIdentifier ), mountHandle );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_persistent_local_storage_XPersistentLocalStoragePromptUserForSpaceAsync( IXPersistentLocalStorageImpl3 *iface, UINT64 requestedBytes, XAsyncBlock *asyncBlock )
{
    FIXME( "iface %p, requestedBytes %llu, asyncBlock %p stub!\n", iface, requestedBytes, asyncBlock );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_persistent_local_storage_XPersistentLocalStoragePromptUserForSpaceResult( IXPersistentLocalStorageImpl3 *iface, XAsyncBlock *asyncBlock )
{
    FIXME( "iface %p, asyncBlock %p stub!\n", iface, asyncBlock );
    return E_NOTIMPL;
}

static const struct IXPersistentLocalStorageImpl3Vtbl x_persistent_local_storage_vtbl =
{
    x_persistent_local_storage_QueryInterface,
    x_persistent_local_storage_AddRef,
    x_persistent_local_storage_Release,
    /* IXPersistentLocalStorageImpl/IXPersistentLocalStorageImpl2 methods */
    x_persistent_local_storage_XPersistentLocalStorageGetPathSize,
    x_persistent_local_storage_XPersistentLocalStorageGetPath,
    x_persistent_local_storage_XPersistentLocalStorageGetSpaceInfo,
    x_persistent_local_storage_XPersistentLocalStoragePromptUserForSpaceAsync,
    x_persistent_local_storage_XPersistentLocalStoragePromptUserForSpaceResult,
    /* IXPersistentLocalStorageImpl3 methods */
    x_persistent_local_storage_XPersistentLocalStorageMountForPackage,
};

static struct x_persistent_local_storage x_persistent_local_storage =
{
    {&x_persistent_local_storage_vtbl},
    0,
};

IXPersistentLocalStorageImpl *x_persistent_local_storage_impl = (IXPersistentLocalStorageImpl *)&x_persistent_local_storage.IXPersistentLocalStorageImpl3_iface;
