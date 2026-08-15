/*
 * Xbox Game runtime Library
 *  GDK Component: System API -> XGameSave and XGameSaveFiles
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

#include <wine/list.h>

struct x_game_save
{
    IXGameSaveImpl3 IXGameSaveImpl3_iface;
    LONG ref;
};

WINE_DEFAULT_DEBUG_CHANNEL(gdkc);

static inline struct x_game_save *impl_from_IXGameSaveImpl3( IXGameSaveImpl3 *iface )
{
    return CONTAINING_RECORD( iface, struct x_game_save, IXGameSaveImpl3_iface );
}


/* Connected Storage, backed by the filesystem.
 *
 * Every function here used to be a stub, so titles using the GDK save system
 * could not save at all: Subnautica 2 reports "InitializeProviderInit: Failed"
 * and then silently loses every save, which reads as the game starting fresh
 * on each launch.
 *
 * There is no cloud to sync with, so the "connected" half is dropped and the
 * layout mirrors the one the real runtime keeps on disk:
 *
 *   %LOCALAPPDATA%\Packages\<family>\SystemAppData\wgs\<configurationId>\
 *       <container>\
 *           .container      -- the container's display name
 *           <blob>          -- one file per blob
 *
 * Keeping that shape means saves stay where a user would look for them, and a
 * container is a directory rather than an opaque blob, so a save can be copied
 * or backed up with ordinary tools.
 */

/* Implemented by kernelbase and forwarded from kernel32, but wine's
 * appmodel.h does not declare it -- same as in xpersistentlocalstorage.c. */
LONG WINAPI GetCurrentPackageFamilyName( UINT32 *length, WCHAR *name );

#define SAVE_QUOTA (256 * 1024 * 1024) /* what the console runtime grants a title */
#define DISPLAY_NAME_FILE L".container"

/* The GDK's documented limits; not declared in this tree's headers. */
#define XGAMESAVE_MAX_CONTAINER_NAME_SIZE 256
#define XGAMESAVE_MAX_BLOB_NAME_SIZE      256

struct save_provider
{
    WCHAR root[MAX_PATH];
};

struct save_container
{
    struct save_provider *provider;
    WCHAR path[MAX_PATH];
    char name[XGAMESAVE_MAX_CONTAINER_NAME_SIZE];
};

struct blob_op
{
    struct list entry;
    char name[XGAMESAVE_MAX_BLOB_NAME_SIZE];
    UINT8 *data;   /* NULL for a delete */
    SIZE_T size;
};

struct save_update
{
    struct save_container *container;
    WCHAR display_name[MAX_PATH];
    struct list ops;
};

/* Results waiting to be collected by an XGameSave*Result call.
 *
 * The work itself is a handful of file operations, so it is done up front and
 * the async block is completed immediately; this only has to carry the value
 * across to the Result call that follows. */
struct pending_result
{
    struct list entry;
    XAsyncBlock *async;
    void *value;
    UINT32 count;
};

static struct list pending_results = LIST_INIT( pending_results );
static CRITICAL_SECTION pending_cs;
static CRITICAL_SECTION_DEBUG pending_cs_debug =
{
    0, 0, &pending_cs,
    { &pending_cs_debug.ProcessLocksList, &pending_cs_debug.ProcessLocksList },
    0, 0, { (DWORD_PTR)(__FILE__ ": pending_cs") }
};
static CRITICAL_SECTION pending_cs = { &pending_cs_debug, -1, 0, 0, 0, 0 };

static void pending_put( XAsyncBlock *async, void *value, UINT32 count )
{
    struct pending_result *pending;

    if (!(pending = calloc( 1, sizeof(*pending) ))) return;
    pending->async = async;
    pending->value = value;
    pending->count = count;

    EnterCriticalSection( &pending_cs );
    list_add_tail( &pending_results, &pending->entry );
    LeaveCriticalSection( &pending_cs );
}

static BOOL pending_take( XAsyncBlock *async, void **value, UINT32 *count )
{
    struct pending_result *pending;
    BOOL found = FALSE;

    EnterCriticalSection( &pending_cs );
    LIST_FOR_EACH_ENTRY( pending, &pending_results, struct pending_result, entry )
    {
        if (pending->async != async) continue;
        list_remove( &pending->entry );
        if (value) *value = pending->value;
        if (count) *count = pending->count;
        free( pending );
        found = TRUE;
        break;
    }
    LeaveCriticalSection( &pending_cs );
    return found;
}

/* Names come from the title and end up as path components, so anything that
 * could climb out of the save folder is rejected rather than rewritten -- a
 * silently renamed container would read back as a missing save. */
static BOOL name_is_safe( const char *name )
{
    const char *p;

    if (!name || !*name) return FALSE;
    for (p = name; *p; p++)
    {
        if (*p >= 'a' && *p <= 'z') continue;
        if (*p >= 'A' && *p <= 'Z') continue;
        if (*p >= '0' && *p <= '9') continue;
        if (*p == '.' || *p == '_' || *p == '-' || *p == ' ') continue;
        return FALSE;
    }
    /* "." and ".." are spelled with allowed characters. */
    if (!strcmp( name, "." ) || !strcmp( name, ".." )) return FALSE;
    return TRUE;
}

static BOOL widen( const char *in, WCHAR *out, int out_len )
{
    return MultiByteToWideChar( CP_UTF8, 0, in, -1, out, out_len ) > 0;
}

static void create_directories( const WCHAR *path )
{
    WCHAR partial[MAX_PATH];
    WCHAR *p;

    lstrcpynW( partial, path, ARRAY_SIZE(partial) );
    for (p = partial; *p; p++)
    {
        if (*p != '\\' || p == partial) continue;
        *p = 0;
        CreateDirectoryW( partial, NULL );
        *p = '\\';
    }
    CreateDirectoryW( partial, NULL );
}

static BOOL remove_directory_tree( const WCHAR *path )
{
    WCHAR pattern[MAX_PATH], child[MAX_PATH];
    WIN32_FIND_DATAW data;
    HANDLE find;

    swprintf( pattern, ARRAY_SIZE(pattern), L"%s\\*", path );
    if ((find = FindFirstFileW( pattern, &data )) != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (!wcscmp( data.cFileName, L"." ) || !wcscmp( data.cFileName, L".." )) continue;
            swprintf( child, ARRAY_SIZE(child), L"%s\\%s", path, data.cFileName );
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) remove_directory_tree( child );
            else DeleteFileW( child );
        } while (FindNextFileW( find, &data ));
        FindClose( find );
    }
    return RemoveDirectoryW( path );
}

static UINT64 filetime_to_unix( const FILETIME *ft )
{
    ULARGE_INTEGER value;

    value.LowPart = ft->dwLowDateTime;
    value.HighPart = ft->dwHighDateTime;
    /* 100ns ticks since 1601 -> seconds since 1970 */
    return (value.QuadPart / 10000000) - 11644473600ULL;
}

/* Write via a temporary file and rename, so a save interrupted part-way leaves
 * the previous one intact instead of a truncated file that loads as corrupt. */
static BOOL write_file_atomically( const WCHAR *path, const UINT8 *data, SIZE_T size )
{
    WCHAR temp[MAX_PATH];
    DWORD written;
    HANDLE file;
    BOOL ok;

    swprintf( temp, ARRAY_SIZE(temp), L"%s.tmp", path );
    file = CreateFileW( temp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
    if (file == INVALID_HANDLE_VALUE) return FALSE;

    ok = !size || WriteFile( file, data, (DWORD)size, &written, NULL );
    if (ok && size && written != size) ok = FALSE;
    if (ok) ok = FlushFileBuffers( file );
    CloseHandle( file );

    if (!ok)
    {
        DeleteFileW( temp );
        return FALSE;
    }
    return MoveFileExW( temp, path, MOVEFILE_REPLACE_EXISTING );
}

static UINT8 *read_whole_file( const WCHAR *path, DWORD *size )
{
    HANDLE file;
    UINT8 *data;
    DWORD read;

    file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, NULL );
    if (file == INVALID_HANDLE_VALUE) return NULL;

    *size = GetFileSize( file, NULL );
    if (!(data = malloc( *size ? *size : 1 )))
    {
        CloseHandle( file );
        return NULL;
    }
    if (*size && (!ReadFile( file, data, *size, &read, NULL ) || read != *size))
    {
        free( data );
        CloseHandle( file );
        return NULL;
    }
    CloseHandle( file );
    return data;
}

static UINT64 directory_size( const WCHAR *path )
{
    WCHAR pattern[MAX_PATH], child[MAX_PATH];
    WIN32_FIND_DATAW data;
    UINT64 total = 0;
    HANDLE find;

    swprintf( pattern, ARRAY_SIZE(pattern), L"%s\\*", path );
    if ((find = FindFirstFileW( pattern, &data )) == INVALID_HANDLE_VALUE) return 0;
    do
    {
        if (!wcscmp( data.cFileName, L"." ) || !wcscmp( data.cFileName, L".." )) continue;
        swprintf( child, ARRAY_SIZE(child), L"%s\\%s", path, data.cFileName );
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) total += directory_size( child );
        else total += ((UINT64)data.nFileSizeHigh << 32) | data.nFileSizeLow;
    } while (FindNextFileW( find, &data ));
    FindClose( find );
    return total;
}

/* --- provider ---------------------------------------------------------- */

static HRESULT WINAPI x_game_save_XGameSaveInitializeProvider( IXGameSaveImpl3 *iface, XUserHandle requestingUser, const char *configurationId, BOOLEAN syncOnDemand, XGameSaveProviderHandle *provider )
{
    WCHAR local[MAX_PATH], family[256], config[MAX_PATH];
    UINT32 family_len = ARRAY_SIZE(family);
    struct save_provider *impl;

    TRACE( "iface %p, requestingUser %p, configurationId %s, syncOnDemand %d, provider %p.\n",
           iface, requestingUser, debugstr_a( configurationId ), syncOnDemand, provider );

    if (!provider) return E_POINTER;
    *provider = NULL;

    if (!GetEnvironmentVariableW( L"LOCALAPPDATA", local, ARRAY_SIZE(local) ))
    {
        ERR( "LOCALAPPDATA is not set, saves have nowhere to go.\n" );
        return E_FAIL;
    }
    if (GetCurrentPackageFamilyName( &family_len, family )) lstrcpyW( family, L"UnknownPackage" );

    /* A title with no configuration id still needs somewhere of its own. */
    if (!configurationId || !*configurationId) lstrcpyW( config, L"default" );
    else if (!name_is_safe( configurationId ) || !widen( configurationId, config, ARRAY_SIZE(config) ))
    {
        WARN( "configuration id %s is not usable as a folder name.\n", debugstr_a( configurationId ) );
        lstrcpyW( config, L"default" );
    }

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    swprintf( impl->root, ARRAY_SIZE(impl->root), L"%s\\Packages\\%s\\SystemAppData\\wgs\\%s",
              local, family, config );
    create_directories( impl->root );

    TRACE( "saves for this title live in %s.\n", debugstr_w( impl->root ) );
    *provider = (XGameSaveProviderHandle)impl;
    return S_OK;
}

static HRESULT WINAPI x_game_save_XGameSaveInitializeProviderAsync( IXGameSaveImpl3 *iface, XUserHandle requestingUser, const char *configurationId, BOOLEAN syncOnDemand, XAsyncBlock *async )
{
    XGameSaveProviderHandle provider = NULL;
    HRESULT hr;

    TRACE( "iface %p, requestingUser %p, configurationId %s, syncOnDemand %d, async %p.\n",
           iface, requestingUser, debugstr_a( configurationId ), syncOnDemand, async );

    /* Creating a directory is not worth a worker thread, so the work happens
     * here and the block is completed straight away; the Result call below
     * collects the handle. */
    hr = x_game_save_XGameSaveInitializeProvider( iface, requestingUser, configurationId,
                                                  syncOnDemand, &provider );
    if (SUCCEEDED(hr)) pending_put( async, provider, 0 );
    return xasync_complete_static( async, hr );
}

static HRESULT WINAPI x_game_save_XGameSaveInitializeProviderResult( IXGameSaveImpl3 *iface, XAsyncBlock *async, XGameSaveProviderHandle *provider )
{
    void *value = NULL;
    HRESULT hr;

    TRACE( "iface %p, async %p, provider %p.\n", iface, async, provider );

    if (!provider) return E_POINTER;
    if (FAILED(hr = xasync_peek_status( async ))) return hr;
    if (!pending_take( async, &value, NULL )) return E_UNEXPECTED;

    *provider = value;
    return S_OK;
}

static void WINAPI x_game_save_XGameSaveCloseProvider( IXGameSaveImpl3 *iface, XGameSaveProviderHandle provider )
{
    TRACE( "iface %p, provider %p.\n", iface, provider );
    free( provider );
}

static HRESULT WINAPI x_game_save_XGameSaveGetRemainingQuota( IXGameSaveImpl3 *iface, XGameSaveProviderHandle provider, INT64 *remainingQuota )
{
    struct save_provider *impl = (struct save_provider *)provider;
    UINT64 used;

    TRACE( "iface %p, provider %p, remainingQuota %p.\n", iface, provider, remainingQuota );

    if (!impl || !remainingQuota) return E_INVALIDARG;
    used = directory_size( impl->root );
    *remainingQuota = used >= SAVE_QUOTA ? 0 : (INT64)(SAVE_QUOTA - used);
    return S_OK;
}

static HRESULT WINAPI x_game_save_XGameSaveGetRemainingQuotaAsync( IXGameSaveImpl3 *iface, XGameSaveProviderHandle provider, XAsyncBlock *async )
{
    INT64 *quota;
    HRESULT hr;

    TRACE( "iface %p, provider %p, async %p.\n", iface, provider, async );

    if (!(quota = calloc( 1, sizeof(*quota) ))) return E_OUTOFMEMORY;
    if (FAILED(hr = x_game_save_XGameSaveGetRemainingQuota( iface, provider, quota ))) free( quota );
    else pending_put( async, quota, 0 );
    return xasync_complete_static( async, hr );
}

static HRESULT WINAPI x_game_save_XGameSaveGetRemainingQuotaResult( IXGameSaveImpl3 *iface, XAsyncBlock *async, INT64 *remainingQuota )
{
    void *value = NULL;
    HRESULT hr;

    TRACE( "iface %p, async %p, remainingQuota %p.\n", iface, async, remainingQuota );

    if (!remainingQuota) return E_POINTER;
    if (FAILED(hr = xasync_peek_status( async ))) return hr;
    if (!pending_take( async, &value, NULL )) return E_UNEXPECTED;

    *remainingQuota = *(INT64 *)value;
    free( value );
    return S_OK;
}

/* --- containers -------------------------------------------------------- */

static HRESULT container_path( struct save_provider *provider, const char *name, WCHAR *out, int out_len )
{
    WCHAR wide[XGAMESAVE_MAX_CONTAINER_NAME_SIZE];

    if (!provider || !name_is_safe( name )) return E_INVALIDARG;
    if (!widen( name, wide, ARRAY_SIZE(wide) )) return E_INVALIDARG;
    swprintf( out, out_len, L"%s\\%s", provider->root, wide );
    return S_OK;
}

static HRESULT WINAPI x_game_save_XGameSaveDeleteContainer( IXGameSaveImpl3 *iface, XGameSaveProviderHandle provider, const char *containerName )
{
    WCHAR path[MAX_PATH];
    HRESULT hr;

    TRACE( "iface %p, provider %p, containerName %s.\n", iface, provider, debugstr_a( containerName ) );

    if (FAILED(hr = container_path( (struct save_provider *)provider, containerName, path, ARRAY_SIZE(path) )))
        return hr;
    if (GetFileAttributesW( path ) == INVALID_FILE_ATTRIBUTES) return S_OK; /* already gone */
    return remove_directory_tree( path ) ? S_OK : HRESULT_FROM_WIN32( GetLastError() );
}

static HRESULT WINAPI x_game_save_XGameSaveDeleteContainerAsync( IXGameSaveImpl3 *iface, XGameSaveProviderHandle provider, const char *containerName, XAsyncBlock *async )
{
    TRACE( "iface %p, provider %p, containerName %s, async %p.\n", iface, provider, debugstr_a( containerName ), async );
    return xasync_complete_static( async,
        x_game_save_XGameSaveDeleteContainer( iface, provider, containerName ) );
}

static HRESULT WINAPI x_game_save_XGameSaveDeleteContainerResult( IXGameSaveImpl3 *iface, XAsyncBlock *async )
{
    TRACE( "iface %p, async %p.\n", iface, async );
    return xasync_peek_status( async );
}

/* Describe one container directory to a caller's callback. */
static BOOL report_container( const WCHAR *dir, const WCHAR *leaf, void *context, XGameSaveContainerInfoCallback *callback )
{
    char name[XGAMESAVE_MAX_CONTAINER_NAME_SIZE], display[MAX_PATH];
    WCHAR pattern[MAX_PATH], name_file[MAX_PATH];
    XGameSaveContainerInfo info = {0};
    WIN32_FIND_DATAW data;
    WIN32_FILE_ATTRIBUTE_DATA attr;
    UINT32 blobs = 0;
    UINT64 total = 0;
    HANDLE find;
    DWORD size;
    UINT8 *stored;

    if (!WideCharToMultiByte( CP_UTF8, 0, leaf, -1, name, sizeof(name), NULL, NULL )) return TRUE;

    swprintf( pattern, ARRAY_SIZE(pattern), L"%s\\*", dir );
    if ((find = FindFirstFileW( pattern, &data )) != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (!wcscmp( data.cFileName, DISPLAY_NAME_FILE )) continue;
            blobs++;
            total += ((UINT64)data.nFileSizeHigh << 32) | data.nFileSizeLow;
        } while (FindNextFileW( find, &data ));
        FindClose( find );
    }

    /* The display name is what a title shows in its own load menu, so it is
     * kept rather than being re-derived from the folder name. */
    display[0] = 0;
    swprintf( name_file, ARRAY_SIZE(name_file), L"%s\\%s", dir, DISPLAY_NAME_FILE );
    if ((stored = read_whole_file( name_file, &size )))
    {
        if (size >= sizeof(display)) size = sizeof(display) - 1;
        memcpy( display, stored, size );
        display[size] = 0;
        free( stored );
    }
    if (!display[0]) strcpy( display, name );

    info.name = name;
    info.displayName = display;
    info.blobCount = blobs;
    info.totalSize = total;
    if (GetFileAttributesExW( dir, GetFileExInfoStandard, &attr ))
        info.lastModifiedTime = filetime_to_unix( &attr.ftLastWriteTime );
    info.needsSync = FALSE;

    return callback( &info, context );
}

static HRESULT WINAPI x_game_save_XGameSaveGetContainerInfo( IXGameSaveImpl3 *iface, XGameSaveProviderHandle provider, const char *containerName, void *context, XGameSaveContainerInfoCallback *callback )
{
    WCHAR path[MAX_PATH], wide[XGAMESAVE_MAX_CONTAINER_NAME_SIZE];
    HRESULT hr;

    TRACE( "iface %p, provider %p, containerName %s, context %p, callback %p.\n",
           iface, provider, debugstr_a( containerName ), context, callback );

    if (!callback) return E_INVALIDARG;
    if (FAILED(hr = container_path( (struct save_provider *)provider, containerName, path, ARRAY_SIZE(path) )))
        return hr;
    if (GetFileAttributesW( path ) == INVALID_FILE_ATTRIBUTES) return S_OK;

    widen( containerName, wide, ARRAY_SIZE(wide) );
    report_container( path, wide, context, callback );
    return S_OK;
}

static HRESULT enumerate_containers( XGameSaveProviderHandle provider, const char *prefix, void *context, XGameSaveContainerInfoCallback *callback )
{
    struct save_provider *impl = (struct save_provider *)provider;
    WCHAR pattern[MAX_PATH], child[MAX_PATH];
    WIN32_FIND_DATAW data;
    size_t prefix_len;
    char name[XGAMESAVE_MAX_CONTAINER_NAME_SIZE];
    HANDLE find;

    if (!impl || !callback) return E_INVALIDARG;
    prefix_len = prefix ? strlen( prefix ) : 0;

    swprintf( pattern, ARRAY_SIZE(pattern), L"%s\\*", impl->root );
    if ((find = FindFirstFileW( pattern, &data )) == INVALID_HANDLE_VALUE) return S_OK;
    do
    {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (!wcscmp( data.cFileName, L"." ) || !wcscmp( data.cFileName, L".." )) continue;
        if (prefix_len)
        {
            if (!WideCharToMultiByte( CP_UTF8, 0, data.cFileName, -1, name, sizeof(name), NULL, NULL ))
                continue;
            if (strncmp( name, prefix, prefix_len )) continue;
        }
        swprintf( child, ARRAY_SIZE(child), L"%s\\%s", impl->root, data.cFileName );
        if (!report_container( child, data.cFileName, context, callback )) break;
    } while (FindNextFileW( find, &data ));
    FindClose( find );
    return S_OK;
}

static HRESULT WINAPI x_game_save_XGameSaveEnumerateContainerInfo( IXGameSaveImpl3 *iface, XGameSaveProviderHandle provider, void *context, XGameSaveContainerInfoCallback *callback )
{
    TRACE( "iface %p, provider %p, context %p, callback %p.\n", iface, provider, context, callback );
    return enumerate_containers( provider, NULL, context, callback );
}

static HRESULT WINAPI x_game_save_XGameSaveEnumerateContainerInfoByName( IXGameSaveImpl3 *iface, XGameSaveProviderHandle provider, const char *containerNamePrefix, void *context, XGameSaveContainerInfoCallback *callback )
{
    TRACE( "iface %p, provider %p, containerNamePrefix %s, context %p, callback %p.\n",
           iface, provider, debugstr_a( containerNamePrefix ), context, callback );
    return enumerate_containers( provider, containerNamePrefix, context, callback );
}

static HRESULT WINAPI x_game_save_XGameSaveCreateContainer( IXGameSaveImpl3 *iface, XGameSaveProviderHandle provider, const char *containerName, XGameSaveContainerHandle *containerContext )
{
    struct save_container *impl;
    WCHAR path[MAX_PATH];
    HRESULT hr;

    TRACE( "iface %p, provider %p, containerName %s, containerContext %p.\n",
           iface, provider, debugstr_a( containerName ), containerContext );

    if (!containerContext) return E_POINTER;
    *containerContext = NULL;

    if (FAILED(hr = container_path( (struct save_provider *)provider, containerName, path, ARRAY_SIZE(path) )))
        return hr;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->provider = (struct save_provider *)provider;
    lstrcpynW( impl->path, path, ARRAY_SIZE(impl->path) );
    lstrcpynA( impl->name, containerName, sizeof(impl->name) );

    /* Opening a container that does not exist yet is how a new save is made;
     * the directory appears when an update is submitted, not here, so merely
     * looking at a container does not litter the save folder. */
    *containerContext = (XGameSaveContainerHandle)impl;
    return S_OK;
}

static void WINAPI x_game_save_XGameSaveCloseContainer( IXGameSaveImpl3 *iface, XGameSaveContainerHandle context )
{
    TRACE( "iface %p, context %p.\n", iface, context );
    free( context );
}

/* --- blobs ------------------------------------------------------------- */

static HRESULT blob_path( struct save_container *container, const char *name, WCHAR *out, int out_len )
{
    WCHAR wide[XGAMESAVE_MAX_BLOB_NAME_SIZE];

    if (!container || !name_is_safe( name )) return E_INVALIDARG;
    if (!widen( name, wide, ARRAY_SIZE(wide) )) return E_INVALIDARG;
    swprintf( out, out_len, L"%s\\%s", container->path, wide );
    return S_OK;
}

static HRESULT enumerate_blobs( XGameSaveContainerHandle container, const char *prefix, void *context, XGameSaveBlobInfoCallback *callback )
{
    struct save_container *impl = (struct save_container *)container;
    char name[XGAMESAVE_MAX_BLOB_NAME_SIZE];
    WCHAR pattern[MAX_PATH];
    WIN32_FIND_DATAW data;
    size_t prefix_len;
    HANDLE find;

    if (!impl || !callback) return E_INVALIDARG;
    prefix_len = prefix ? strlen( prefix ) : 0;

    swprintf( pattern, ARRAY_SIZE(pattern), L"%s\\*", impl->path );
    if ((find = FindFirstFileW( pattern, &data )) == INVALID_HANDLE_VALUE) return S_OK;
    do
    {
        XGameSaveBlobInfo info;

        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (!wcscmp( data.cFileName, DISPLAY_NAME_FILE )) continue;
        if (!WideCharToMultiByte( CP_UTF8, 0, data.cFileName, -1, name, sizeof(name), NULL, NULL ))
            continue;
        if (prefix_len && strncmp( name, prefix, prefix_len )) continue;

        info.name = name;
        info.size = data.nFileSizeLow;
        if (!callback( &info, context )) break;
    } while (FindNextFileW( find, &data ));
    FindClose( find );
    return S_OK;
}

static HRESULT WINAPI x_game_save_XGameSaveEnumerateBlobInfo( IXGameSaveImpl3 *iface, XGameSaveContainerHandle container, void *context, XGameSaveBlobInfoCallback *callback )
{
    TRACE( "iface %p, container %p, context %p, callback %p.\n", iface, container, context, callback );
    return enumerate_blobs( container, NULL, context, callback );
}

static HRESULT WINAPI x_game_save_XGameSaveEnumerateBlobInfoByName( IXGameSaveImpl3 *iface, XGameSaveContainerHandle container, const char *blobNamePrefix, void *context, XGameSaveBlobInfoCallback *callback )
{
    TRACE( "iface %p, container %p, blobNamePrefix %s, context %p, callback %p.\n",
           iface, container, debugstr_a( blobNamePrefix ), context, callback );
    return enumerate_blobs( container, blobNamePrefix, context, callback );
}

/* Collect blob names when the caller did not name any: reading "everything in
 * this container" is how a title loads a save it did not write itself. */
struct name_collector
{
    char (*names)[XGAMESAVE_MAX_BLOB_NAME_SIZE];
    UINT32 count;
    UINT32 capacity;
};

static BOOLEAN CALLBACK collect_name( const XGameSaveBlobInfo *info, void *context )
{
    struct name_collector *collector = context;

    if (collector->count == collector->capacity)
    {
        UINT32 capacity = collector->capacity ? collector->capacity * 2 : 8;
        void *grown = realloc( collector->names, capacity * sizeof(*collector->names) );

        if (!grown) return FALSE;
        collector->names = grown;
        collector->capacity = capacity;
    }
    lstrcpynA( collector->names[collector->count++], info->name, XGAMESAVE_MAX_BLOB_NAME_SIZE );
    return TRUE;
}

/* The caller hands over one buffer and gets back an array of XGameSaveBlob
 * followed by each blob's name and bytes packed in behind it -- the layout the
 * GDK uses, so `blobData[i].data` points inside the buffer the caller owns. */
static HRESULT read_blobs( XGameSaveContainerHandle container, const char **blobNames, UINT32 *countOfBlobs,
                           SIZE_T blobsSize, XGameSaveBlob *blobData )
{
    struct save_container *impl = (struct save_container *)container;
    struct name_collector collector = {0};
    const char **names = blobNames;
    UINT32 count, i;
    HRESULT hr = S_OK;
    SIZE_T used;
    UINT8 *cursor;

    if (!impl || !countOfBlobs || !blobData) return E_INVALIDARG;
    count = *countOfBlobs;

    if (!names)
    {
        if (FAILED(hr = enumerate_blobs( container, NULL, &collector, collect_name )))
            return hr;
        count = collector.count;
        if (!(names = calloc( count ? count : 1, sizeof(*names) )))
        {
            free( collector.names );
            return E_OUTOFMEMORY;
        }
        for (i = 0; i < count; i++) names[i] = collector.names[i];
    }

    used = count * sizeof(XGameSaveBlob);
    cursor = (UINT8 *)blobData + used;

    for (i = 0; i < count; i++)
    {
        WCHAR path[MAX_PATH];
        SIZE_T name_len;
        UINT8 *data;
        DWORD size;

        if (FAILED(hr = blob_path( impl, names[i], path, ARRAY_SIZE(path) ))) break;
        if (!(data = read_whole_file( path, &size )))
        {
            hr = HRESULT_FROM_WIN32( ERROR_FILE_NOT_FOUND );
            break;
        }

        name_len = strlen( names[i] ) + 1;
        if (used + name_len + size > blobsSize)
        {
            free( data );
            hr = E_NOT_SUFFICIENT_BUFFER;
            break;
        }

        memcpy( cursor, names[i], name_len );
        blobData[i].info.name = (char *)cursor;
        cursor += name_len;
        used += name_len;

        memcpy( cursor, data, size );
        blobData[i].info.size = size;
        blobData[i].data = cursor;
        cursor += size;
        used += size;
        free( data );
    }

    if (SUCCEEDED(hr)) *countOfBlobs = count;
    if (names != blobNames) free( (void *)names );
    free( collector.names );
    return hr;
}

static HRESULT WINAPI x_game_save_XGameSaveReadBlobData( IXGameSaveImpl3 *iface, XGameSaveContainerHandle container, const char **blobNames, UINT32 *countOfBlobs, SIZE_T blobsSize, XGameSaveBlob *blobData )
{
    TRACE( "iface %p, container %p, blobNames %p, countOfBlobs %p, blobsSize %Iu, blobData %p.\n",
           iface, container, blobNames, countOfBlobs, blobsSize, blobData );
    return read_blobs( container, blobNames, countOfBlobs, blobsSize, blobData );
}

/* The async read keeps the names it was given, because the caller only
 * supplies a buffer when collecting the result. */
struct pending_read
{
    XGameSaveContainerHandle container;
    char (*names)[XGAMESAVE_MAX_BLOB_NAME_SIZE];
    const char **pointers;
    UINT32 count;
};

static HRESULT WINAPI x_game_save_XGameSaveReadBlobDataAsync( IXGameSaveImpl3 *iface, XGameSaveContainerHandle container, const char **blobNames, UINT32 countOfBlobs, XAsyncBlock *async )
{
    struct pending_read *read;
    UINT32 i;

    TRACE( "iface %p, container %p, blobNames %p, countOfBlobs %u, async %p.\n",
           iface, container, blobNames, countOfBlobs, async );

    if (!(read = calloc( 1, sizeof(*read) ))) return E_OUTOFMEMORY;
    read->container = container;
    read->count = blobNames ? countOfBlobs : 0;

    if (blobNames && countOfBlobs)
    {
        if (!(read->names = calloc( countOfBlobs, sizeof(*read->names) )) ||
            !(read->pointers = calloc( countOfBlobs, sizeof(*read->pointers) )))
        {
            free( read->names );
            free( read );
            return E_OUTOFMEMORY;
        }
        for (i = 0; i < countOfBlobs; i++)
        {
            lstrcpynA( read->names[i], blobNames[i], XGAMESAVE_MAX_BLOB_NAME_SIZE );
            read->pointers[i] = read->names[i];
        }
    }

    pending_put( async, read, 0 );
    return xasync_complete_static( async, S_OK );
}

static HRESULT WINAPI x_game_save_XGameSaveReadBlobDataResult( IXGameSaveImpl3 *iface, XAsyncBlock *async, SIZE_T blobsSize, XGameSaveBlob *blobData, UINT32 *countOfBlobs )
{
    struct pending_read *read = NULL;
    UINT32 count;
    HRESULT hr;

    TRACE( "iface %p, async %p, blobsSize %Iu, blobData %p, countOfBlobs %p.\n",
           iface, async, blobsSize, blobData, countOfBlobs );

    if (FAILED(hr = xasync_peek_status( async ))) return hr;
    if (!pending_take( async, (void **)&read, NULL )) return E_UNEXPECTED;

    count = read->count;
    hr = read_blobs( read->container, read->pointers, &count, blobsSize, blobData );
    if (SUCCEEDED(hr) && countOfBlobs) *countOfBlobs = count;

    free( read->pointers );
    free( read->names );
    free( read );
    return hr;
}

/* --- updates ----------------------------------------------------------- */

static HRESULT WINAPI x_game_save_XGameSaveCreateUpdate( IXGameSaveImpl3 *iface, XGameSaveContainerHandle container, const char *containerDisplayName, XGameSaveUpdateHandle *updateContext )
{
    struct save_update *update;

    TRACE( "iface %p, container %p, containerDisplayName %s, updateContext %p.\n",
           iface, container, debugstr_a( containerDisplayName ), updateContext );

    if (!container || !updateContext) return E_INVALIDARG;
    if (!(update = calloc( 1, sizeof(*update) ))) return E_OUTOFMEMORY;

    update->container = (struct save_container *)container;
    list_init( &update->ops );
    if (containerDisplayName)
        widen( containerDisplayName, update->display_name, ARRAY_SIZE(update->display_name) );

    *updateContext = (XGameSaveUpdateHandle)update;
    return S_OK;
}

static void free_update( struct save_update *update )
{
    struct blob_op *op, *next;

    LIST_FOR_EACH_ENTRY_SAFE( op, next, &update->ops, struct blob_op, entry )
    {
        list_remove( &op->entry );
        free( op->data );
        free( op );
    }
    free( update );
}

static void WINAPI x_game_save_XGameSaveCloseUpdate( IXGameSaveImpl3 *iface, XGameSaveUpdateHandle context )
{
    TRACE( "iface %p, context %p.\n", iface, context );
    if (context) free_update( (struct save_update *)context );
}

static HRESULT WINAPI x_game_save_XGameSaveSubmitBlobWrite( IXGameSaveImpl3 *iface, XGameSaveUpdateHandle updateContext, const char *blobName, UINT8 *data, SIZE_T byteCount )
{
    struct save_update *update = (struct save_update *)updateContext;
    struct blob_op *op;

    TRACE( "iface %p, updateContext %p, blobName %s, data %p, byteCount %Iu.\n",
           iface, updateContext, debugstr_a( blobName ), data, byteCount );

    if (!update || !name_is_safe( blobName )) return E_INVALIDARG;
    if (!(op = calloc( 1, sizeof(*op) ))) return E_OUTOFMEMORY;

    /* The caller's buffer is only guaranteed until it returns, and the write
     * does not happen until the update is submitted, so take a copy. */
    if (byteCount && !(op->data = malloc( byteCount )))
    {
        free( op );
        return E_OUTOFMEMORY;
    }
    if (byteCount) memcpy( op->data, data, byteCount );
    else op->data = malloc( 1 ); /* an empty blob is still a write, not a delete */

    lstrcpynA( op->name, blobName, sizeof(op->name) );
    op->size = byteCount;
    list_add_tail( &update->ops, &op->entry );
    return S_OK;
}

static HRESULT WINAPI x_game_save_XGameSaveSubmitBlobDelete( IXGameSaveImpl3 *iface, XGameSaveUpdateHandle updateContext, const char *blobName )
{
    struct save_update *update = (struct save_update *)updateContext;
    struct blob_op *op;

    TRACE( "iface %p, updateContext %p, blobName %s.\n", iface, updateContext, debugstr_a( blobName ) );

    if (!update || !name_is_safe( blobName )) return E_INVALIDARG;
    if (!(op = calloc( 1, sizeof(*op) ))) return E_OUTOFMEMORY;

    lstrcpynA( op->name, blobName, sizeof(op->name) );
    op->data = NULL; /* a delete */
    list_add_tail( &update->ops, &op->entry );
    return S_OK;
}

static HRESULT WINAPI x_game_save_XGameSaveSubmitUpdate( IXGameSaveImpl3 *iface, XGameSaveUpdateHandle updateContext )
{
    struct save_update *update = (struct save_update *)updateContext;
    struct blob_op *op;
    HRESULT hr = S_OK;

    TRACE( "iface %p, updateContext %p.\n", iface, updateContext );

    if (!update) return E_INVALIDARG;

    /* The container directory is created here rather than when the container
     * is opened, so a title that only reads never leaves empty saves behind. */
    create_directories( update->container->path );

    if (update->display_name[0])
    {
        WCHAR name_file[MAX_PATH];
        char utf8[MAX_PATH];
        int len;

        swprintf( name_file, ARRAY_SIZE(name_file), L"%s\\%s", update->container->path, DISPLAY_NAME_FILE );
        len = WideCharToMultiByte( CP_UTF8, 0, update->display_name, -1, utf8, sizeof(utf8), NULL, NULL );
        if (len > 0) write_file_atomically( name_file, (UINT8 *)utf8, len - 1 );
    }

    LIST_FOR_EACH_ENTRY( op, &update->ops, struct blob_op, entry )
    {
        WCHAR path[MAX_PATH];

        if (FAILED(hr = blob_path( update->container, op->name, path, ARRAY_SIZE(path) ))) break;

        if (!op->data)
        {
            DeleteFileW( path );
            continue;
        }
        if (!write_file_atomically( path, op->data, op->size ))
        {
            hr = HRESULT_FROM_WIN32( GetLastError() );
            ERR( "could not write %s: %#lx\n", debugstr_w( path ), GetLastError() );
            break;
        }
    }

    return hr;
}

static HRESULT WINAPI x_game_save_XGameSaveSubmitUpdateAsync( IXGameSaveImpl3 *iface, XGameSaveUpdateHandle updateContext, XAsyncBlock *async )
{
    TRACE( "iface %p, updateContext %p, async %p.\n", iface, updateContext, async );
    return xasync_complete_static( async, x_game_save_XGameSaveSubmitUpdate( iface, updateContext ) );
}

static HRESULT WINAPI x_game_save_XGameSaveSubmitUpdateResult( IXGameSaveImpl3 *iface, XAsyncBlock *async )
{
    TRACE( "iface %p, async %p.\n", iface, async );
    return xasync_peek_status( async );
}

/* --- IXGameSaveImpl2: the save folder as a plain directory -------------- */

static HRESULT WINAPI x_game_save_XGameSaveFilesGetFolderWithUiAsync( IXGameSaveImpl3 *iface, XUserHandle requestingUser, const char *configurationId, XAsyncBlock *async )
{
    XGameSaveProviderHandle provider = NULL;
    HRESULT hr;

    TRACE( "iface %p, requestingUser %p, configurationId %s, async %p.\n",
           iface, requestingUser, debugstr_a( configurationId ), async );

    /* No picker to show: there is one folder and this is where it is. */
    hr = x_game_save_XGameSaveInitializeProvider( iface, requestingUser, configurationId, FALSE, &provider );
    if (SUCCEEDED(hr)) pending_put( async, provider, 0 );
    return xasync_complete_static( async, hr );
}

static HRESULT WINAPI x_game_save_XGameSaveFilesGetFolderWithUiResult( IXGameSaveImpl3 *iface, XAsyncBlock *async, SIZE_T folderSize, char *folderResult )
{
    struct save_provider *provider = NULL;
    HRESULT hr = S_OK;
    int len;

    TRACE( "iface %p, async %p, folderSize %Iu, folderResult %p.\n", iface, async, folderSize, folderResult );

    if (!folderResult) return E_POINTER;
    if (FAILED(hr = xasync_peek_status( async ))) return hr;
    if (!pending_take( async, (void **)&provider, NULL )) return E_UNEXPECTED;

    len = WideCharToMultiByte( CP_UTF8, 0, provider->root, -1, NULL, 0, NULL, NULL );
    if (len <= 0 || (SIZE_T)len > folderSize) hr = E_NOT_SUFFICIENT_BUFFER;
    else WideCharToMultiByte( CP_UTF8, 0, provider->root, -1, folderResult, len, NULL, NULL );

    free( provider );
    return hr;
}

static HRESULT WINAPI x_game_save_XGameSaveFilesGetRemainingQuota( IXGameSaveImpl3 *iface, XUserHandle userContext, const char *configurationId, INT64 *remainingQuota )
{
    XGameSaveProviderHandle provider = NULL;
    HRESULT hr;

    TRACE( "iface %p, userContext %p, configurationId %s, remainingQuota %p.\n",
           iface, userContext, debugstr_a( configurationId ), remainingQuota );

    if (FAILED(hr = x_game_save_XGameSaveInitializeProvider( iface, userContext, configurationId, FALSE, &provider )))
        return hr;
    hr = x_game_save_XGameSaveGetRemainingQuota( iface, provider, remainingQuota );
    free( provider );
    return hr;
}

/* --- interface --------------------------------------------------------- */

/* The revision Subnautica 2 asks for.
 *
 * It is not one of the three the IDL declares, so it was refused with
 * E_NOINTERFACE and the title's save system failed before reaching any of the
 * code above. The methods it does call line up with IXGameSaveImpl3, which the
 * GDK extends by appending rather than reordering -- but that cannot be
 * verified from here, so the vtable below carries spare slots that return
 * E_NOTIMPL instead of letting a call past the end of it jump into whatever
 * follows in memory. */
static const GUID IID_IXGameSaveImplUnknownRevision =
    { 0xab4ae4fb, 0x6508, 0x4950, { 0xa0, 0x32, 0x45, 0xfd, 0x4b, 0xf8, 0xc4, 0x3b } };

static HRESULT WINAPI x_game_save_QueryInterface( IXGameSaveImpl3 *iface, REFIID iid, void **out )
{
    struct x_game_save *impl = impl_from_IXGameSaveImpl3( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown        ) ||
        IsEqualGUID( iid, &IID_IXGameSaveImpl  ) ||
        IsEqualGUID( iid, &IID_IXGameSaveImpl2 ) ||
        IsEqualGUID( iid, &IID_IXGameSaveImpl3 ))
    {
        IXGameSaveImpl_AddRef( *out = &impl->IXGameSaveImpl3_iface );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IXGameSaveImplUnknownRevision ))
    {
        WARN( "serving %s with the IXGameSaveImpl3 vtable.\n", debugstr_guid( iid ) );
        IXGameSaveImpl_AddRef( *out = &impl->IXGameSaveImpl3_iface );
        return S_OK;
    }

    ERR( "unsupported XGameSave interface %s, returning E_NOINTERFACE. "
         "Saving will not work for this title.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI x_game_save_AddRef( IXGameSaveImpl3 *iface )
{
    struct x_game_save *impl = impl_from_IXGameSaveImpl3( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI x_game_save_Release( IXGameSaveImpl3 *iface )
{
    struct x_game_save *impl = impl_from_IXGameSaveImpl3( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    return ref;
}

/* Reached only if a title calls a method this build does not know about. */
static HRESULT WINAPI x_game_save_unknown_method( IXGameSaveImpl3 *iface )
{
    FIXME( "iface %p: a method beyond IXGameSaveImpl3 was called.\n", iface );
    return E_NOTIMPL;
}

struct x_game_save_vtbl_ext
{
    IXGameSaveImpl3Vtbl base;
    void *spare[16];
};

static const struct x_game_save_vtbl_ext x_game_save_vtbl =
{
    {
        x_game_save_QueryInterface,
        x_game_save_AddRef,
        x_game_save_Release,
        /* IXGameSaveImpl methods */
        x_game_save_XGameSaveInitializeProvider,
        x_game_save_XGameSaveInitializeProviderAsync,
        x_game_save_XGameSaveInitializeProviderResult,
        x_game_save_XGameSaveCloseProvider,
        x_game_save_XGameSaveGetRemainingQuota,
        x_game_save_XGameSaveGetRemainingQuotaAsync,
        x_game_save_XGameSaveGetRemainingQuotaResult,
        x_game_save_XGameSaveDeleteContainer,
        x_game_save_XGameSaveDeleteContainerAsync,
        x_game_save_XGameSaveDeleteContainerResult,
        x_game_save_XGameSaveGetContainerInfo,
        x_game_save_XGameSaveEnumerateContainerInfo,
        x_game_save_XGameSaveEnumerateContainerInfoByName,
        x_game_save_XGameSaveCreateContainer,
        x_game_save_XGameSaveCloseContainer,
        x_game_save_XGameSaveEnumerateBlobInfo,
        x_game_save_XGameSaveEnumerateBlobInfoByName,
        x_game_save_XGameSaveReadBlobData,
        x_game_save_XGameSaveReadBlobDataAsync,
        x_game_save_XGameSaveReadBlobDataResult,
        x_game_save_XGameSaveCreateUpdate,
        x_game_save_XGameSaveCloseUpdate,
        x_game_save_XGameSaveSubmitBlobWrite,
        x_game_save_XGameSaveSubmitBlobDelete,
        x_game_save_XGameSaveSubmitUpdate,
        x_game_save_XGameSaveSubmitUpdateAsync,
        x_game_save_XGameSaveSubmitUpdateResult,
        /* IXGameSaveImpl2 methods */
        x_game_save_XGameSaveFilesGetFolderWithUiAsync,
        x_game_save_XGameSaveFilesGetFolderWithUiResult,
        x_game_save_XGameSaveFilesGetRemainingQuota,
    },
    {
        x_game_save_unknown_method, x_game_save_unknown_method,
        x_game_save_unknown_method, x_game_save_unknown_method,
        x_game_save_unknown_method, x_game_save_unknown_method,
        x_game_save_unknown_method, x_game_save_unknown_method,
        x_game_save_unknown_method, x_game_save_unknown_method,
        x_game_save_unknown_method, x_game_save_unknown_method,
        x_game_save_unknown_method, x_game_save_unknown_method,
        x_game_save_unknown_method, x_game_save_unknown_method,
    },
};

static struct x_game_save x_game_save =
{
    {&x_game_save_vtbl.base},
    0,
};

IXGameSaveImpl *x_game_save_impl = (IXGameSaveImpl *)&x_game_save.IXGameSaveImpl3_iface;
