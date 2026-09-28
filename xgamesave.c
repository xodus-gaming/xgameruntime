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

#include <ctype.h>
#include <stdio.h>
#include "private.h"
#include "util.h"

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

static HRESULT WINAPI x_game_save_QueryInterface( IXGameSaveImpl3 *iface, REFIID iid, void **out )
{
    struct x_game_save *impl = impl_from_IXGameSaveImpl3( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown        ) ||
        IsEqualGUID( iid, &IID_IXGameSaveImpl  ) ||
        IsEqualGUID( iid, &IID_IXGameSaveImpl2 ) ||
        IsEqualGUID( iid, &IID_IXGameSaveImpl3 ) ||
        IsEqualGUID( iid, &IID_IXGameSaveImpl4 ))
    {
        IXGameSaveImpl_AddRef( *out = &impl->IXGameSaveImpl3_iface );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
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

#define E_GS_INVALID_CONTAINER_NAME     ((HRESULT)0x80830001)
#define E_GS_PROVIDED_BUFFER_TOO_SMALL  ((HRESULT)0x80830007)
#define E_GS_BLOB_NOT_FOUND             ((HRESULT)0x80830008)
#define E_GS_NO_SERVICE_CONFIGURATION   ((HRESULT)0x80830009)
#define E_GS_CONTAINER_NOT_IN_SYNC      ((HRESULT)0x8083000A)
#define E_GS_UPDATE_TOO_BIG             ((HRESULT)0x80830005)

#define SAVE_QUOTA (256 * 1024 * 1024)

struct blob_entry
{
    char *name;
    UINT32 size;
    char atom[48];
};

struct container_state
{
    char *name;
    char *display;
    UINT64 time;
    char remote[48];
    BOOL dirty;
    UINT32 count;
    struct blob_entry *blobs;
};

struct XGameSaveProvider
{
    XUserHandle user;
    char *scid;
    char root[MAX_PATH];
    char base[512];
    char *auth;
    BOOL cloud;
    BOOL locked;
    INT64 quota;
    UINT32 downloaded;
    UINT32 uploaded;
    CRITICAL_SECTION cs;
};

struct XGameSaveContainer
{
    struct XGameSaveProvider *provider;
    char *name;
};

struct update_op
{
    char *name;
    BYTE *data;
    SIZE_T size;
    BOOL remove;
};

struct XGameSaveUpdate
{
    struct XGameSaveContainer *container;
    char *display;
    UINT32 count;
    UINT32 capacity;
    struct update_op *ops;
};

static char *hex_encode( const char *str )
{
    static const char digits[] = "0123456789abcdef";
    SIZE_T len = strlen( str );
    char *out = calloc( 1, len * 2 + 1 );
    if (!out) return NULL;
    for (SIZE_T i = 0; i < len; i++)
    {
        out[2 * i] = digits[(BYTE)str[i] >> 4];
        out[2 * i + 1] = digits[(BYTE)str[i] & 15];
    }
    return out;
}

static char *hex_decode( const char *hex )
{
    SIZE_T len = strlen( hex ) / 2;
    char *out = calloc( 1, len + 1 );
    if (!out) return NULL;
    for (SIZE_T i = 0; i < len; i++)
    {
        char pair[3] = { hex[2 * i], hex[2 * i + 1], 0 };
        out[i] = (char)strtoul( pair, NULL, 16 );
    }
    return out;
}

static char *url_encode( const char *str )
{
    static const char digits[] = "0123456789ABCDEF";
    char *out = calloc( 1, strlen( str ) * 3 + 1 ), *p = out;
    if (!out) return NULL;
    for (; *str; str++)
    {
        BYTE c = *str;
        if (isalnum( c ) || c == '-' || c == '.' || c == '_' || c == '~') *p++ = c;
        else
        {
            *p++ = '%';
            *p++ = digits[c >> 4];
            *p++ = digits[c & 15];
        }
    }
    return out;
}

static char *json_escape( const char *str )
{
    char *out = calloc( 1, strlen( str ) * 6 + 1 ), *p = out;
    if (!out) return NULL;
    for (; *str; str++)
    {
        BYTE c = *str;
        if (c == '"' || c == '\\') { *p++ = '\\'; *p++ = c; }
        else if (c < 0x20) p += sprintf( p, "\\u%04x", c );
        else *p++ = c;
    }
    return out;
}

static const char *json_ws( const char *p )
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}

static const char *json_string( const char *p, char **out )
{
    char *buf, *q;
    const char *start;

    if (*p != '"') return NULL;
    start = ++p;
    while (*p && *p != '"') p += (*p == '\\' && p[1]) ? 2 : 1;
    if (!*p || !(buf = calloc( 1, p - start + 1 ))) return NULL;
    for (q = buf, p = start; *p != '"'; p++)
    {
        if (*p != '\\') { *q++ = *p; continue; }
        switch (*++p)
        {
            case 'n': *q++ = '\n'; break;
            case 't': *q++ = '\t'; break;
            case 'r': *q++ = '\r'; break;
            case 'b': *q++ = '\b'; break;
            case 'f': *q++ = '\f'; break;
            case 'u':
            {
                char hex[5] = { 0 };
                WCHAR wc;
                memcpy( hex, p + 1, 4 );
                wc = (WCHAR)strtoul( hex, NULL, 16 );
                q += WideCharToMultiByte( CP_UTF8, 0, &wc, 1, q, 4, NULL, NULL );
                p += 4;
                break;
            }
            default: *q++ = *p; break;
        }
    }
    *out = buf;
    return p + 1;
}

static const char *json_skip( const char *p )
{
    int depth = 0;
    char *tmp;

    p = json_ws( p );
    do
    {
        if (!*p) return NULL;
        if (*p == '"')
        {
            if (!(p = json_string( p, &tmp ))) return NULL;
            free( tmp );
            continue;
        }
        if (*p == '{' || *p == '[') depth++;
        else if (*p == '}' || *p == ']') depth--;
        else if (!depth && (*p == ',')) return p;
        p++;
    } while (depth > 0 || (*p && *p != ',' && *p != '}' && *p != ']'));
    return p;
}

static BOOL json_object_next( const char **p, char **key, const char **value )
{
    const char *q = json_ws( *p );

    if (*q == '{' || *q == ',') q = json_ws( q + 1 );
    if (*q != '"') return FALSE;
    if (!(q = json_string( q, key ))) return FALSE;
    q = json_ws( q );
    if (*q != ':')
    {
        free( *key );
        return FALSE;
    }
    *value = json_ws( q + 1 );
    if (!(q = json_skip( *value )))
    {
        free( *key );
        return FALSE;
    }
    *p = q;
    return TRUE;
}

static const char *json_find( const char *object, const char *name )
{
    const char *p = object, *value;
    char *key;

    while (json_object_next( &p, &key, &value ))
    {
        BOOL match = !strcmp( key, name );
        free( key );
        if (match) return value;
    }
    return NULL;
}

static char *json_find_string( const char *object, const char *name )
{
    const char *value = json_find( object, name );
    char *out = NULL;
    if (value && *value == '"') json_string( value, &out );
    return out;
}

static UINT64 filetime_to_unix( const FILETIME *ft )
{
    ULARGE_INTEGER li = { .LowPart = ft->dwLowDateTime, .HighPart = ft->dwHighDateTime };
    return li.QuadPart / 10000000 - 11644473600ULL;
}

static UINT64 parse_iso_time( const char *str )
{
    SYSTEMTIME st = { 0 };
    FILETIME ft;
    if (!str || sscanf( str, "%hu-%hu-%huT%hu:%hu:%hu", &st.wYear, &st.wMonth, &st.wDay, &st.wHour, &st.wMinute, &st.wSecond ) != 6) return 0;
    if (!SystemTimeToFileTime( &st, &ft )) return 0;
    return filetime_to_unix( &ft );
}

static void format_iso_time( UINT64 unixTime, char *out, SIZE_T size )
{
    ULARGE_INTEGER li = { .QuadPart = (unixTime + 11644473600ULL) * 10000000 };
    FILETIME ft = { li.LowPart, li.HighPart };
    SYSTEMTIME st;
    FileTimeToSystemTime( &ft, &st );
    snprintf( out, size, "%04u-%02u-%02uT%02u:%02u:%02u.0000000Z", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond );
}

static UINT64 unix_now( void )
{
    FILETIME ft;
    GetSystemTimeAsFileTime( &ft );
    return filetime_to_unix( &ft );
}

static HRESULT read_file( const char *path, BYTE **data, SIZE_T *size )
{
    HANDLE file = CreateFileA( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
    LARGE_INTEGER li;
    DWORD read;

    *data = NULL;
    *size = 0;
    if (file == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32( GetLastError() );
    if (!GetFileSizeEx( file, &li ) || !(*data = calloc( 1, li.QuadPart + 1 )))
    {
        CloseHandle( file );
        return E_OUTOFMEMORY;
    }
    if (li.QuadPart && (!ReadFile( file, *data, li.QuadPart, &read, NULL ) || read != li.QuadPart))
    {
        CloseHandle( file );
        free( *data );
        *data = NULL;
        return HRESULT_FROM_WIN32( GetLastError() );
    }
    CloseHandle( file );
    *size = li.QuadPart;
    return S_OK;
}

static HRESULT write_file( const char *path, const void *data, SIZE_T size )
{
    char tmp[MAX_PATH + 8];
    HANDLE file;
    DWORD written;
    BOOL ok;

    snprintf( tmp, sizeof(tmp), "%s.tmp", path );
    file = CreateFileA( tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL );
    if (file == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32( GetLastError() );
    ok = (!size || (WriteFile( file, data, size, &written, NULL ) && written == size)) && FlushFileBuffers( file );
    CloseHandle( file );
    if (!ok || !MoveFileExA( tmp, path, MOVEFILE_REPLACE_EXISTING ))
    {
        DeleteFileA( tmp );
        return HRESULT_FROM_WIN32( GetLastError() );
    }
    return S_OK;
}

static void create_directories( const char *path )
{
    char buf[MAX_PATH];
    lstrcpynA( buf, path, sizeof(buf) );
    for (char *p = buf + 3; *p; p++)
        if (*p == '\\')
        {
            *p = 0;
            CreateDirectoryA( buf, NULL );
            *p = '\\';
        }
    CreateDirectoryA( buf, NULL );
}

static void delete_directory( const char *path )
{
    char pattern[MAX_PATH], child[MAX_PATH];
    WIN32_FIND_DATAA data;
    HANDLE find;

    snprintf( pattern, sizeof(pattern), "%s\\*", path );
    if ((find = FindFirstFileA( pattern, &data )) != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (!strcmp( data.cFileName, "." ) || !strcmp( data.cFileName, ".." )) continue;
            snprintf( child, sizeof(child), "%s\\%s", path, data.cFileName );
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) delete_directory( child );
            else DeleteFileA( child );
        } while (FindNextFileA( find, &data ));
        FindClose( find );
    }
    RemoveDirectoryA( path );
}

static BOOL container_dir( struct XGameSaveProvider *provider, const char *name, char *out, SIZE_T size )
{
    char *hex = hex_encode( name );
    if (!hex) return FALSE;
    snprintf( out, size, "%s\\%s", provider->root, hex );
    free( hex );
    return TRUE;
}

static BOOL blob_path( struct XGameSaveProvider *provider, const char *container, const char *blob, char *out, SIZE_T size )
{
    char dir[MAX_PATH], *hex = hex_encode( blob );
    BOOL ok = hex && container_dir( provider, container, dir, sizeof(dir) );
    if (ok) snprintf( out, size, "%s\\b_%s", dir, hex );
    free( hex );
    return ok;
}

static void free_state( struct container_state *state )
{
    for (UINT32 i = 0; i < state->count; i++) free( state->blobs[i].name );
    free( state->blobs );
    free( state->name );
    free( state->display );
    memset( state, 0, sizeof(*state) );
}

static struct blob_entry *find_blob( struct container_state *state, const char *name )
{
    for (UINT32 i = 0; i < state->count; i++)
        if (!strcmp( state->blobs[i].name, name )) return &state->blobs[i];
    return NULL;
}

static struct blob_entry *add_blob( struct container_state *state, const char *name )
{
    struct blob_entry *blobs, *entry;
    if ((entry = find_blob( state, name ))) return entry;
    if (!(blobs = realloc( state->blobs, (state->count + 1) * sizeof(*blobs) ))) return NULL;
    state->blobs = blobs;
    entry = &blobs[state->count];
    memset( entry, 0, sizeof(*entry) );
    if (!(entry->name = strdup( name ))) return NULL;
    state->count++;
    return entry;
}

static void remove_blob( struct container_state *state, const char *name )
{
    struct blob_entry *entry = find_blob( state, name );
    if (!entry) return;
    free( entry->name );
    memmove( entry, entry + 1, (state->blobs + state->count - entry - 1) * sizeof(*entry) );
    state->count--;
}

static UINT64 state_total_size( const struct container_state *state )
{
    UINT64 total = 0;
    for (UINT32 i = 0; i < state->count; i++) total += state->blobs[i].size;
    return total;
}

static HRESULT load_state( struct XGameSaveProvider *provider, const char *name, struct container_state *state )
{
    char dir[MAX_PATH], path[MAX_PATH];
    char *line, *next, *text;
    SIZE_T size;
    BYTE *data;
    HRESULT hr;

    memset( state, 0, sizeof(*state) );
    if (!(state->name = strdup( name ))) return E_OUTOFMEMORY;
    if (!container_dir( provider, name, dir, sizeof(dir) )) return E_OUTOFMEMORY;
    snprintf( path, sizeof(path), "%s\\index", dir );
    if (FAILED(hr = read_file( path, &data, &size ))) return hr;
    text = (char *)data;
    for (line = text; line && *line; line = next)
    {
        char a[128] = { 0 }, b[128] = { 0 };
        unsigned int blobSize = 0;
        if ((next = strchr( line, '\n' ))) *next++ = 0;
        if (!strncmp( line, "display ", 8 )) state->display = hex_decode( line + 8 );
        else if (!strncmp( line, "time ", 5 )) state->time = _strtoui64( line + 5, NULL, 10 );
        else if (!strncmp( line, "remote ", 7 )) lstrcpynA( state->remote, strcmp( line + 7, "-" ) ? line + 7 : "", sizeof(state->remote) );
        else if (!strncmp( line, "dirty ", 6 )) state->dirty = atoi( line + 6 );
        else if (sscanf( line, "blob %127s %u %127s", a, &blobSize, b ) == 3)
        {
            char *blobName = hex_decode( a );
            struct blob_entry *entry = blobName ? add_blob( state, blobName ) : NULL;
            free( blobName );
            if (!entry) continue;
            entry->size = blobSize;
            lstrcpynA( entry->atom, strcmp( b, "-" ) ? b : "", sizeof(entry->atom) );
        }
    }
    free( data );
    return S_OK;
}

static HRESULT save_state( struct XGameSaveProvider *provider, const struct container_state *state )
{
    char dir[MAX_PATH], path[MAX_PATH], *hex, *text;
    SIZE_T size = 256 + (state->display ? strlen( state->display ) * 2 : 0), len = 0;
    HRESULT hr;

    for (UINT32 i = 0; i < state->count; i++) size += strlen( state->blobs[i].name ) * 2 + 80;
    if (!container_dir( provider, state->name, dir, sizeof(dir) )) return E_OUTOFMEMORY;
    create_directories( dir );
    if (!(text = calloc( 1, size ))) return E_OUTOFMEMORY;
    hex = hex_encode( state->display ? state->display : "" );
    len += snprintf( text + len, size - len, "display %s\ntime %I64u\nremote %s\ndirty %d\n", hex ? hex : "", state->time, state->remote[0] ? state->remote : "-", state->dirty );
    free( hex );
    for (UINT32 i = 0; i < state->count; i++)
    {
        hex = hex_encode( state->blobs[i].name );
        len += snprintf( text + len, size - len, "blob %s %u %s\n", hex ? hex : "", state->blobs[i].size, state->blobs[i].atom[0] ? state->blobs[i].atom : "-" );
        free( hex );
    }
    snprintf( path, sizeof(path), "%s\\index", dir );
    hr = write_file( path, text, len );
    free( text );
    return hr;
}

typedef BOOL (*container_callback)( struct XGameSaveProvider *provider, const char *name, void *context );

static void enumerate_local( struct XGameSaveProvider *provider, container_callback callback, void *context )
{
    char pattern[MAX_PATH], index[MAX_PATH];
    WIN32_FIND_DATAA data;
    HANDLE find;

    snprintf( pattern, sizeof(pattern), "%s\\*", provider->root );
    if ((find = FindFirstFileA( pattern, &data )) == INVALID_HANDLE_VALUE) return;
    do
    {
        char *name;
        BOOL more;
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || data.cFileName[0] == '.') continue;
        snprintf( index, sizeof(index), "%s\\%s\\index", provider->root, data.cFileName );
        if (GetFileAttributesA( index ) == INVALID_FILE_ATTRIBUTES) continue;
        if (!(name = hex_decode( data.cFileName ))) continue;
        more = callback( provider, name, context );
        free( name );
        if (!more) break;
    } while (FindNextFileA( find, &data ));
    FindClose( find );
}

static HRESULT cloud_request( struct XGameSaveProvider *provider, const char *method, const char *path, const char *extraHeaders,
                              const void *body, DWORD bodySize, DWORD *status, BYTE **buffer, SIZE_T *bufferSize )
{
    char url[2048], headers[1024];
    HRESULT hr;

    snprintf( url, sizeof(url), "%s%s", provider->base, path );
    snprintf( headers, sizeof(headers), "x-xbl-contract-version: 107\r\nx-xbl-pfn: %s\r\n%s", packageFamilyName, extraHeaders ? extraHeaders : "" );
    hr = xuser_signed_request( provider->user, method, url, provider->auth, headers, body, bodySize, status, buffer, bufferSize );
    TRACE( "%s %s -> hr %#lx, status %lu.\n", method, debugstr_a( path ), hr, *status );
    if (SUCCEEDED(hr) && *status >= 400)
        WARN( "%s %s failed with status %lu: %s\n", method, debugstr_a( path ), *status, debugstr_an( (char *)*buffer, min( *bufferSize, 300 ) ) );
    return hr;
}

static BOOL cloud_lock( struct XGameSaveProvider *provider )
{
    const char *quota;
    SIZE_T size;
    BYTE *buffer;
    DWORD status;
    HRESULT hr;

    hr = cloud_request( provider, "PUT", "/lock?friendlyName=Xodus", "x-xbl-lock-ver: 1\r\nContent-Type: application/json\r\n", NULL, 0, &status, &buffer, &size );
    if (FAILED(hr)) return FALSE;
    if (status == 200 || status == 201)
    {
        if ((quota = json_find( (char *)buffer, "quotaBytes" ))) provider->quota = _strtoi64( quota, NULL, 10 );
        provider->locked = TRUE;
    }
    else WARN( "could not acquire the cloud save lock (status %lu), another device may be using it.\n", status );
    free( buffer );
    return provider->locked;
}

static void cloud_unlock( struct XGameSaveProvider *provider, BOOL uploaded )
{
    SIZE_T size;
    BYTE *buffer;
    DWORD status;

    if (!provider->locked) return;
    if (SUCCEEDED(cloud_request( provider, "DELETE", uploaded ? "/lock?newSavesUploaded=true" : "/lock?newSavesUploaded=false", NULL, NULL, 0, &status, &buffer, &size )))
        free( buffer );
    provider->locked = FALSE;
}

static void cloud_disable( struct XGameSaveProvider *provider )
{
    provider->cloud = FALSE;
    ERR( "cloud saves disabled for this session, continuing with local saves only.\n" );
}

static HRESULT cloud_download( struct XGameSaveProvider *provider, const char *name, const char *display, const char *remoteTime )
{
    struct container_state state = { 0 };
    char path[1024], dir[MAX_PATH], blob[MAX_PATH], *enc, *key;
    SIZE_T size, dataSize;
    BYTE *buffer, *data;
    const char *atoms, *value;
    DWORD status;
    HRESULT hr;

    TRACE( "provider %p, name %s.\n", provider, debugstr_a( name ) );

    if (!(enc = url_encode( name ))) return E_OUTOFMEMORY;
    snprintf( path, sizeof(path), "/%s,savedgame", enc );
    free( enc );
    if (FAILED(hr = cloud_request( provider, "GET", path, NULL, NULL, 0, &status, &buffer, &size ))) return hr;
    if (status != 200 || !(atoms = json_find( (char *)buffer, "atoms" )) || *atoms != '{')
    {
        free( buffer );
        return E_FAIL;
    }

    if (!container_dir( provider, name, dir, sizeof(dir) )) hr = E_OUTOFMEMORY;
    else
    {
        delete_directory( dir );
        create_directories( dir );
    }
    if (!(state.name = strdup( name ))) hr = E_OUTOFMEMORY;
    state.display = strdup( display ? display : name );
    state.time = parse_iso_time( remoteTime );
    lstrcpynA( state.remote, remoteTime ? remoteTime : "", sizeof(state.remote) );

    while (SUCCEEDED(hr) && json_object_next( &atoms, &key, &value ))
    {
        char *atom = NULL;
        struct blob_entry *entry;
        if (*value == '"') json_string( value, &atom );
        if (!atom || !(entry = add_blob( &state, key )))
        {
            free( atom );
            free( key );
            hr = E_OUTOFMEMORY;
            break;
        }
        snprintf( path, sizeof(path), "/%s", atom );
        if (SUCCEEDED(hr = cloud_request( provider, "GET", path, NULL, NULL, 0, &status, &data, &dataSize )) && status == 200)
        {
            blob_path( provider, name, key, blob, sizeof(blob) );
            hr = write_file( blob, data, dataSize );
            entry->size = dataSize;
            lstrcpynA( entry->atom, atom, sizeof(entry->atom) );
        }
        else if (SUCCEEDED(hr)) hr = E_FAIL;
        free( data );
        free( atom );
        free( key );
    }
    free( buffer );
    if (SUCCEEDED(hr)) hr = save_state( provider, &state );
    TRACE( "downloaded %s, %u blobs, hr %#lx.\n", debugstr_a( name ), state.count, hr );
    free_state( &state );
    return hr;
}

#define LARGE_BLOB_SIZE (4 * 1024 * 1024)

static HRESULT upload_large_atom( struct XGameSaveProvider *provider, const char *atom, const BYTE *data, SIZE_T size )
{
    char path[128], body[64], *blobUri = NULL, *commit = NULL, *p;
    UINT32 blocks = (size + LARGE_BLOB_SIZE - 1) / LARGE_BLOB_SIZE;
    SIZE_T responseSize, commitSize;
    BYTE *response;
    DWORD status;
    HRESULT hr;

    TRACE( "atom %s, size %Iu, blocks %u.\n", debugstr_a( atom ), size, blocks );

    snprintf( path, sizeof(path), "/atoms/%.36s", atom );
    snprintf( body, sizeof(body), "{\"size\":%Iu}", size );
    if (FAILED(hr = cloud_request( provider, "POST", path, "Content-Type: application/json\r\nAccept: application/json\r\n", body, strlen( body ), &status, &response, &responseSize ))) return hr;
    if (status == 200) blobUri = json_find_string( (char *)response, "blobUri" );
    free( response );
    if (!blobUri) return E_FAIL;

    commitSize = 64 + blocks * 24;
    if (!(commit = calloc( 1, commitSize )))
    {
        free( blobUri );
        return E_OUTOFMEMORY;
    }
    p = commit + sprintf( commit, "{\"blockIds\":[" );

    for (UINT32 i = 0; i < blocks && SUCCEEDED(hr); i++)
    {
        char id[16], encoded[32], *url;
        WCHAR *urlW;
        SIZE_T chunk = min( LARGE_BLOB_SIZE, size - (SIZE_T)i * LARGE_BLOB_SIZE ), urlSize;
        int len;

        snprintf( id, sizeof(id), "%08u", i );
        encode_base64( 8, (const BYTE *)id, sizeof(encoded) - 1, encoded, TRUE );
        encoded[12] = 0;
        p += sprintf( p, "%s\"%s\"", i ? "," : "", encoded );

        urlSize = strlen( blobUri ) + 64;
        if (!(url = calloc( 1, urlSize )))
        {
            hr = E_OUTOFMEMORY;
            break;
        }
        {
            char *q = url + snprintf( url, urlSize, "%s&comp=block&blockid=", blobUri );
            for (const char *c = encoded; *c; c++) q += (*c == '=') ? sprintf( q, "%%3D" ) : (*q = *c, 1);
            *q = 0;
        }
        len = MultiByteToWideChar( CP_UTF8, 0, url, -1, NULL, 0 );
        if (!(urlW = calloc( len, sizeof(WCHAR) )))
        {
            free( url );
            hr = E_OUTOFMEMORY;
            break;
        }
        MultiByteToWideChar( CP_UTF8, 0, url, -1, urlW, len );
        hr = http_request_raw( L"PUT", urlW, L"x-ms-version: 2015-04-05\r\n", data + (SIZE_T)i * LARGE_BLOB_SIZE, chunk, &status, &response, &responseSize );
        if (SUCCEEDED(hr) && status != 201)
        {
            WARN( "block %u upload failed with status %lu: %s\n", i, status, debugstr_an( (char *)response, min( responseSize, 300 ) ) );
            hr = E_FAIL;
        }
        free( response );
        free( urlW );
        free( url );
    }
    free( blobUri );

    if (SUCCEEDED(hr))
    {
        sprintf( p, "],\"size\":%Iu}", size );
        snprintf( path, sizeof(path), "/atoms/%.36s?commit=true", atom );
        hr = cloud_request( provider, "POST", path, "Content-Type: application/json\r\nAccept: application/json\r\n", commit, strlen( commit ), &status, &response, &responseSize );
        free( response );
        if (SUCCEEDED(hr) && (status < 200 || status >= 300)) hr = E_FAIL;
    }
    free( commit );
    TRACE( "atom %s upload hr %#lx.\n", debugstr_a( atom ), hr );
    return hr;
}

static HRESULT cloud_upload( struct XGameSaveProvider *provider, struct container_state *state )
{
    char path[2048], blob[MAX_PATH], timestamp[48], *enc, *encDisplay, *json, *p;
    SIZE_T size, jsonSize = 32;
    BYTE *buffer, *data;
    DWORD status;
    HRESULT hr = S_OK;

    TRACE( "provider %p, name %s.\n", provider, debugstr_a( state->name ) );

    if (!provider->cloud) return S_FALSE;
    if (!cloud_lock( provider )) return E_FAIL;

    for (UINT32 i = 0; i < state->count && SUCCEEDED(hr); i++)
    {
        struct blob_entry *entry = &state->blobs[i];
        GUID guid;
        if (entry->atom[0]) continue;
        if (!blob_path( provider, state->name, entry->name, blob, sizeof(blob) ))
        {
            hr = E_OUTOFMEMORY;
            break;
        }
        if (FAILED(hr = read_file( blob, &data, &size ))) break;
        CoCreateGuid( &guid );
        snprintf( entry->atom, sizeof(entry->atom), "%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X,binary",
                  guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
                  guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7] );
        if (size > LARGE_BLOB_SIZE)
        {
            hr = upload_large_atom( provider, entry->atom, data, size );
            free( data );
        }
        else
        {
            snprintf( path, sizeof(path), "/%s", entry->atom );
            hr = cloud_request( provider, "PUT", path, "Content-Type: application/octet-stream\r\n", data, size, &status, &buffer, &size );
            free( data );
            free( buffer );
            if (SUCCEEDED(hr) && (status < 200 || status >= 300)) hr = E_FAIL;
        }
        if (FAILED(hr)) entry->atom[0] = 0;
    }
    if (FAILED(hr))
    {
        cloud_unlock( provider, FALSE );
        return hr;
    }

    for (UINT32 i = 0; i < state->count; i++) jsonSize += strlen( state->blobs[i].name ) * 6 + strlen( state->blobs[i].atom ) + 8;
    if (!(json = calloc( 1, jsonSize )))
    {
        cloud_unlock( provider, FALSE );
        return E_OUTOFMEMORY;
    }
    p = json + sprintf( json, "{\"atoms\":{" );
    for (UINT32 i = 0; i < state->count; i++)
    {
        char *escaped = json_escape( state->blobs[i].name );
        p += sprintf( p, "%s\"%s\":\"%s\"", i ? "," : "", escaped ? escaped : "", state->blobs[i].atom );
        free( escaped );
    }
    strcpy( p, "}}" );

    format_iso_time( state->time ? state->time : unix_now(), timestamp, sizeof(timestamp) );
    enc = url_encode( state->name );
    encDisplay = url_encode( state->display ? state->display : state->name );
    snprintf( path, sizeof(path), "/%s,savedgame?clientFileTime=%s&displayName=%s", enc ? enc : "", timestamp, encDisplay ? encDisplay : "" );
    free( enc );
    free( encDisplay );
    hr = cloud_request( provider, "PUT", path, "Content-Type: application/json\r\n", json, strlen( json ), &status, &buffer, &size );
    free( json );
    free( buffer );
    if (SUCCEEDED(hr) && (status < 200 || status >= 300)) hr = E_FAIL;
    if (SUCCEEDED(hr))
    {
        state->dirty = FALSE;
        lstrcpynA( state->remote, timestamp, sizeof(state->remote) );
    }
    cloud_unlock( provider, SUCCEEDED(hr) );
    TRACE( "uploaded %s, hr %#lx.\n", debugstr_a( state->name ), hr );
    return hr;
}

static HRESULT cloud_delete( struct XGameSaveProvider *provider, const char *name )
{
    char path[1024], *enc = url_encode( name );
    SIZE_T size;
    BYTE *buffer;
    DWORD status;
    HRESULT hr;

    if (!provider->cloud)
    {
        free( enc );
        return S_FALSE;
    }
    if (!enc) return E_OUTOFMEMORY;
    if (!cloud_lock( provider ))
    {
        free( enc );
        return E_FAIL;
    }
    snprintf( path, sizeof(path), "/%s,savedgame", enc );
    free( enc );
    hr = cloud_request( provider, "DELETE", path, NULL, NULL, 0, &status, &buffer, &size );
    free( buffer );
    cloud_unlock( provider, SUCCEEDED(hr) );
    if (SUCCEEDED(hr) && status != 200 && status != 204 && status != 404) hr = E_FAIL;
    return hr;
}

struct remote_container
{
    char *name;
    char *display;
    char *time;
};

enum sync_mode
{
    SYNC_AUTO,
    SYNC_DOWNLOAD,
    SYNC_UPLOAD,
};

static enum sync_mode sync_mode( void )
{
    char value[32];
    if (!GetEnvironmentVariableA( "XODUS_SAVE_SYNC", value, sizeof(value) )) return SYNC_AUTO;
    if (!_stricmp( value, "download" )) return SYNC_DOWNLOAD;
    if (!_stricmp( value, "upload" )) return SYNC_UPLOAD;
    return SYNC_AUTO;
}

static void force_upload( struct XGameSaveProvider *provider, struct container_state *state );

static BOOL sync_local_callback( struct XGameSaveProvider *provider, const char *name, void *context )
{
    struct remote_container *remote = context;
    struct container_state state;
    char dir[MAX_PATH];

    for (; remote->name; remote++) if (!strcmp( remote->name, name )) return TRUE;
    if (FAILED(load_state( provider, name, &state )))
    {
        free_state( &state );
        return TRUE;
    }
    if (sync_mode() == SYNC_UPLOAD) force_upload( provider, &state );
    else if (state.dirty || !state.remote[0])
    {
        if (SUCCEEDED(cloud_upload( provider, &state )))
        {
            save_state( provider, &state );
            provider->uploaded++;
        }
    }
    else if (container_dir( provider, name, dir, sizeof(dir) ))
    {
        TRACE( "container %s was deleted in the cloud, removing local copy.\n", debugstr_a( name ) );
        delete_directory( dir );
    }
    free_state( &state );
    return TRUE;
}

static void force_upload( struct XGameSaveProvider *provider, struct container_state *state )
{
    for (UINT32 i = 0; i < state->count; i++) state->blobs[i].atom[0] = 0;
    state->dirty = TRUE;
    save_state( provider, state );
    if (SUCCEEDED(cloud_upload( provider, state ))) provider->uploaded++;
    save_state( provider, state );
}

static HRESULT cloud_sync( struct XGameSaveProvider *provider )
{
    struct remote_container *remote = NULL;
    enum sync_mode mode;
    const char *blobs, *item;
    UINT32 count = 0, capacity = 0;
    SIZE_T size;
    BYTE *buffer;
    DWORD status;
    HRESULT hr;

    if (FAILED(hr = xuser_device_authorization( provider->user, "http://xboxlive.com", &provider->auth ))) return hr;

    if (FAILED(hr = cloud_request( provider, "GET", "", NULL, NULL, 0, &status, &buffer, &size ))) return hr;
    if (status == 404)
    {
        free( buffer );
        buffer = (BYTE *)strdup( "{\"blobs\":[]}" );
        status = 200;
    }
    if (status != 200 || !buffer || !(blobs = json_find( (char *)buffer, "blobs" )) || *blobs != '[')
    {
        free( buffer );
        return E_FAIL;
    }
    for (item = json_ws( blobs + 1 ); *item == '{'; item = json_ws( item ))
    {
        const char *end = json_skip( item );
        char *fileName = json_find_string( item, "fileName" ), *comma;
        struct remote_container *tmp;
        if (!end) break;
        if (fileName && (comma = strstr( fileName, ",savedgame" )) && !comma[10])
        {
            *comma = 0;
            if (count + 1 >= capacity)
            {
                capacity = capacity ? capacity * 2 : 16;
                if (!(tmp = realloc( remote, capacity * sizeof(*remote) ))) break;
                remote = tmp;
            }
            remote[count].name = fileName;
            remote[count].display = json_find_string( item, "displayName" );
            remote[count].time = json_find_string( item, "clientFileTime" );
            remote[++count].name = NULL;
            fileName = NULL;
        }
        free( fileName );
        item = json_ws( end );
        if (*item == ',') item++;
    }
    free( buffer );

    mode = sync_mode();
    for (UINT32 i = 0; i < count; i++)
    {
        struct container_state state;
        BOOL exists = SUCCEEDED(load_state( provider, remote[i].name, &state ));
        BOOL remoteChanged = !exists || strcmp( state.remote, remote[i].time ? remote[i].time : "" );
        if (mode == SYNC_UPLOAD)
        {
            if (exists) force_upload( provider, &state );
        }
        else if (mode == SYNC_DOWNLOAD || !exists || (remoteChanged && !state.dirty) || (remoteChanged && state.dirty && parse_iso_time( remote[i].time ) > state.time))
        {
            if (SUCCEEDED(cloud_download( provider, remote[i].name, remote[i].display, remote[i].time ))) provider->downloaded++;
        }
        else if (state.dirty && SUCCEEDED(cloud_upload( provider, &state )))
        {
            save_state( provider, &state );
            provider->uploaded++;
        }
        free_state( &state );
    }
    if (remote)
    {
        enumerate_local( provider, sync_local_callback, remote );
        for (UINT32 i = 0; i < count; i++)
        {
            free( remote[i].name );
            free( remote[i].display );
            free( remote[i].time );
        }
        free( remote );
    }
    else
    {
        struct remote_container empty = { 0 };
        enumerate_local( provider, sync_local_callback, &empty );
    }
    return S_OK;
}

static HRESULT initialize_provider( XUserHandle user, const char *configurationId, XGameSaveProviderHandle *out )
{
    struct XGameSaveProvider *provider;
    char localAppData[MAX_PATH], scid[128];
    UINT64 xuid;
    HRESULT hr;

    TRACE( "user %p, configurationId %s, pfn %s.\n", user, debugstr_a( configurationId ), debugstr_a( packageFamilyName ) );

    if (!user || !configurationId || !out) return E_INVALIDARG;
    if (!(provider = calloc( 1, sizeof(*provider) ))) return E_OUTOFMEMORY;
    InitializeCriticalSection( &provider->cs );
    provider->quota = SAVE_QUOTA;
    if (FAILED(hr = IXUserImpl_XUserDuplicateHandle( x_user_impl, user, &provider->user )) || !(provider->scid = strdup( configurationId )))
    {
        DeleteCriticalSection( &provider->cs );
        free( provider );
        return FAILED(hr) ? hr : E_OUTOFMEMORY;
    }
    xuid = xuser_get_xuid( user );
    lstrcpynA( scid, configurationId, sizeof(scid) );
    for (char *p = scid; *p; p++) if (!isalnum( (BYTE)*p ) && *p != '-') *p = '_';
    if (!GetEnvironmentVariableA( "LOCALAPPDATA", localAppData, sizeof(localAppData) )) strcpy( localAppData, "C:\\users\\Public\\AppData\\Local" );
    snprintf( provider->root, sizeof(provider->root), "%s\\XodusGameSave\\%I64u\\%s", localAppData, xuid, scid );
    create_directories( provider->root );
    snprintf( provider->base, sizeof(provider->base), "https://titlestorage.xboxlive.com/connectedstorage/users/xuid(%I64u)/scids/%s", xuid, configurationId );

    provider->cloud = packageFamilyName != NULL;
    if (provider->cloud && FAILED(hr = cloud_sync( provider )))
    {
        ERR( "cloud sync failed, hr %#lx.\n", hr );
        cloud_disable( provider );
    }

    TRACE( "provider %p, root %s, cloud %d, downloaded %u, uploaded %u.\n", provider, debugstr_a( provider->root ), provider->cloud, provider->downloaded, provider->uploaded );
    if (GetEnvironmentVariableA( "XODUS_SAVE_SYNC", NULL, 0 ))
        MESSAGE( "xodus-savesync: cloud %d downloaded %u uploaded %u\n", provider->cloud, provider->downloaded, provider->uploaded );
    *out = provider;
    return S_OK;
}

static void close_provider( struct XGameSaveProvider *provider )
{
    if (!provider) return;
    cloud_unlock( provider, FALSE );
    IXUserImpl_XUserCloseHandle( x_user_impl, provider->user );
    DeleteCriticalSection( &provider->cs );
    free( provider->auth );
    free( provider->scid );
    free( provider );
}

struct usage_context
{
    UINT64 total;
};

static BOOL usage_callback( struct XGameSaveProvider *provider, const char *name, void *context )
{
    struct usage_context *usage = context;
    struct container_state state;
    if (SUCCEEDED(load_state( provider, name, &state ))) usage->total += state_total_size( &state );
    free_state( &state );
    return TRUE;
}

static INT64 remaining_quota( struct XGameSaveProvider *provider )
{
    struct usage_context usage = { 0 };
    enumerate_local( provider, usage_callback, &usage );
    return max( 0, provider->quota - (INT64)usage.total );
}

static HRESULT delete_container( struct XGameSaveProvider *provider, const char *name )
{
    char dir[MAX_PATH];
    HRESULT hr;

    if (!provider || !name || !*name) return E_INVALIDARG;
    EnterCriticalSection( &provider->cs );
    if (!container_dir( provider, name, dir, sizeof(dir) )) hr = E_OUTOFMEMORY;
    else
    {
        delete_directory( dir );
        hr = cloud_delete( provider, name );
        if (FAILED(hr)) WARN( "cloud delete of %s failed, hr %#lx.\n", debugstr_a( name ), hr );
        hr = S_OK;
    }
    LeaveCriticalSection( &provider->cs );
    return hr;
}

struct info_context
{
    const char *prefix;
    const char *exact;
    void *context;
    XGameSaveContainerInfoCallback *callback;
    BOOL found;
};

static BOOL info_callback( struct XGameSaveProvider *provider, const char *name, void *context )
{
    struct info_context *ctx = context;
    struct container_state state;
    XGameSaveContainerInfo info;
    BOOL more;

    if (ctx->exact && strcmp( ctx->exact, name )) return TRUE;
    if (ctx->prefix && strncmp( ctx->prefix, name, strlen( ctx->prefix ) )) return TRUE;
    if (FAILED(load_state( provider, name, &state )))
    {
        free_state( &state );
        return TRUE;
    }
    info.name = state.name;
    info.displayName = state.display ? state.display : state.name;
    info.blobCount = state.count;
    info.totalSize = state_total_size( &state );
    info.lastModifiedTime = state.time;
    info.needsSync = FALSE;
    ctx->found = TRUE;
    more = ctx->callback( &info, ctx->context );
    free_state( &state );
    return more && !ctx->exact;
}

static HRESULT enumerate_containers( struct XGameSaveProvider *provider, const char *prefix, const char *exact, void *context, XGameSaveContainerInfoCallback *callback )
{
    struct info_context ctx = { prefix, exact, context, callback, FALSE };
    if (!provider || !callback) return E_INVALIDARG;
    EnterCriticalSection( &provider->cs );
    enumerate_local( provider, info_callback, &ctx );
    LeaveCriticalSection( &provider->cs );
    return S_OK;
}

static HRESULT enumerate_blobs( struct XGameSaveContainer *container, const char *prefix, void *context, XGameSaveBlobInfoCallback *callback )
{
    struct container_state state;
    HRESULT hr;

    if (!container || !callback) return E_INVALIDARG;
    EnterCriticalSection( &container->provider->cs );
    if (SUCCEEDED(hr = load_state( container->provider, container->name, &state )))
    {
        for (UINT32 i = 0; i < state.count; i++)
        {
            XGameSaveBlobInfo info = { state.blobs[i].name, state.blobs[i].size };
            if (prefix && strncmp( prefix, info.name, strlen( prefix ) )) continue;
            if (!callback( &info, context )) break;
        }
    }
    free_state( &state );
    LeaveCriticalSection( &container->provider->cs );
    return hr == HRESULT_FROM_WIN32( ERROR_FILE_NOT_FOUND ) || hr == HRESULT_FROM_WIN32( ERROR_PATH_NOT_FOUND ) ? S_OK : hr;
}

struct read_request
{
    struct XGameSaveContainer *container;
    char **names;
    UINT32 count;
    UINT32 *countOut;
    BYTE *prepared;
    SIZE_T preparedSize;
    UINT32 preparedCount;
};

static HRESULT prepare_read( struct read_request *req )
{
    struct container_state state;
    char path[MAX_PATH];
    SIZE_T size = 0, offset;
    UINT32 count;
    HRESULT hr;

    EnterCriticalSection( &req->container->provider->cs );
    if (FAILED(hr = load_state( req->container->provider, req->container->name, &state )))
    {
        free_state( &state );
        LeaveCriticalSection( &req->container->provider->cs );
        return req->names ? E_GS_BLOB_NOT_FOUND : (req->preparedCount = 0, req->preparedSize = 0, S_OK);
    }
    count = req->names ? req->count : state.count;
    for (UINT32 i = 0; i < count; i++)
    {
        struct blob_entry *entry = req->names ? find_blob( &state, req->names[i] ) : &state.blobs[i];
        if (!entry)
        {
            free_state( &state );
            LeaveCriticalSection( &req->container->provider->cs );
            return E_GS_BLOB_NOT_FOUND;
        }
        size += sizeof(XGameSaveBlob) + strlen( entry->name ) + 1 + entry->size;
    }
    if (!(req->prepared = calloc( 1, size + 1 ))) hr = E_OUTOFMEMORY;
    offset = count * sizeof(XGameSaveBlob);
    for (UINT32 i = 0; SUCCEEDED(hr) && i < count; i++)
    {
        struct blob_entry *entry = req->names ? find_blob( &state, req->names[i] ) : &state.blobs[i];
        XGameSaveBlob *blob = (XGameSaveBlob *)req->prepared + i;
        SIZE_T dataSize;
        BYTE *data;
        blob->info.name = (const char *)(ULONG_PTR)offset;
        strcpy( (char *)req->prepared + offset, entry->name );
        offset += strlen( entry->name ) + 1;
        blob_path( req->container->provider, req->container->name, entry->name, path, sizeof(path) );
        if (FAILED(hr = read_file( path, &data, &dataSize ))) break;
        blob->info.size = min( dataSize, entry->size );
        blob->data = (UINT8 *)(ULONG_PTR)offset;
        memcpy( req->prepared + offset, data, blob->info.size );
        offset += entry->size;
        free( data );
    }
    free_state( &state );
    LeaveCriticalSection( &req->container->provider->cs );
    req->preparedSize = size;
    req->preparedCount = count;
    return hr;
}

static void layout_read( struct read_request *req, void *buffer )
{
    memcpy( buffer, req->prepared, req->preparedSize );
    for (UINT32 i = 0; i < req->preparedCount; i++)
    {
        XGameSaveBlob *blob = (XGameSaveBlob *)buffer + i;
        blob->info.name = (const char *)buffer + (ULONG_PTR)blob->info.name;
        blob->data = (UINT8 *)buffer + (ULONG_PTR)blob->data;
    }
}

static void free_read_request( struct read_request *req )
{
    if (!req) return;
    for (UINT32 i = 0; req->names && i < req->count; i++) free( req->names[i] );
    free( req->names );
    free( req->prepared );
    free( req );
}

static struct read_request *create_read_request( struct XGameSaveContainer *container, const char **names, UINT32 count )
{
    struct read_request *req = calloc( 1, sizeof(*req) );
    if (!req) return NULL;
    req->container = container;
    req->count = count;
    if (names && count)
    {
        if (!(req->names = calloc( count, sizeof(char *) )))
        {
            free( req );
            return NULL;
        }
        for (UINT32 i = 0; i < count; i++)
            if (!(req->names[i] = strdup( names[i] )))
            {
                free_read_request( req );
                return NULL;
            }
    }
    return req;
}

static HRESULT submit_update( struct XGameSaveUpdate *update )
{
    struct XGameSaveProvider *provider = update->container->provider;
    struct container_state state;
    char path[MAX_PATH];
    UINT64 total = 0;
    HRESULT hr = S_OK;

    for (UINT32 i = 0; i < update->count; i++) total += update->ops[i].size;
    if (total > 16 * 1024 * 1024) return E_GS_UPDATE_TOO_BIG;

    EnterCriticalSection( &provider->cs );
    if (FAILED(load_state( provider, update->container->name, &state )))
    {
        free_state( &state );
        if (!(state.name = strdup( update->container->name ))) hr = E_OUTOFMEMORY;
    }
    if (update->display)
    {
        free( state.display );
        state.display = strdup( update->display );
    }
    else if (!state.display) state.display = strdup( update->container->name );
    if (SUCCEEDED(hr))
    {
        char dir[MAX_PATH];
        container_dir( provider, state.name, dir, sizeof(dir) );
        create_directories( dir );
    }

    for (UINT32 i = 0; SUCCEEDED(hr) && i < update->count; i++)
    {
        struct update_op *op = &update->ops[i];
        if (!blob_path( provider, state.name, op->name, path, sizeof(path) ))
        {
            hr = E_OUTOFMEMORY;
            break;
        }
        if (op->remove)
        {
            DeleteFileA( path );
            remove_blob( &state, op->name );
        }
        else
        {
            struct blob_entry *entry;
            if (FAILED(hr = write_file( path, op->data, op->size ))) break;
            if (!(entry = add_blob( &state, op->name )))
            {
                hr = E_OUTOFMEMORY;
                break;
            }
            entry->size = op->size;
            entry->atom[0] = 0;
        }
    }
    if (SUCCEEDED(hr))
    {
        state.time = unix_now();
        state.dirty = TRUE;
        hr = save_state( provider, &state );
    }
    if (SUCCEEDED(hr) && provider->cloud)
    {
        HRESULT cloud = cloud_upload( provider, &state );
        if (FAILED(cloud)) WARN( "cloud upload of %s failed, hr %#lx, it will be retried at next launch.\n", debugstr_a( state.name ), cloud );
        save_state( provider, &state );
    }
    free_state( &state );
    LeaveCriticalSection( &provider->cs );
    TRACE( "update of %s, %u ops, hr %#lx.\n", debugstr_a( update->container->name ), update->count, hr );
    return hr;
}

enum async_kind
{
    ASYNC_INITIALIZE,
    ASYNC_QUOTA,
    ASYNC_DELETE,
    ASYNC_READ,
    ASYNC_SUBMIT,
};

struct save_async
{
    enum async_kind kind;
    XUserHandle user;
    char *text;
    struct XGameSaveProvider *provider;
    struct read_request *read;
    struct XGameSaveUpdate *update;
    XGameSaveProviderHandle result;
    INT64 quota;
};

static HRESULT WINAPI save_async_provider( XAsyncOp op, const XAsyncProviderData *data )
{
    struct save_async *ctx = data->context;
    IXThreadingImpl *xthreading;
    SIZE_T size = 0;
    HRESULT hr;

    TRACE( "op %d, kind %d.\n", op, ctx->kind );

    if (FAILED(hr = QueryApiImpl( &CLSID_XThreadingImpl, &IID_IXThreadingImpl, (void **)&xthreading ))) return hr;
    switch (op)
    {
        case XAsyncOp_Begin:
            hr = IXThreadingImpl_XAsyncSchedule( xthreading, data->async, 0 );
            break;

        case XAsyncOp_DoWork:
            switch (ctx->kind)
            {
                case ASYNC_INITIALIZE:
                    hr = initialize_provider( ctx->user, ctx->text, &ctx->result );
                    size = sizeof(XGameSaveProviderHandle);
                    break;
                case ASYNC_QUOTA:
                    EnterCriticalSection( &ctx->provider->cs );
                    ctx->quota = remaining_quota( ctx->provider );
                    LeaveCriticalSection( &ctx->provider->cs );
                    hr = S_OK;
                    size = sizeof(INT64);
                    break;
                case ASYNC_DELETE:
                    hr = delete_container( ctx->provider, ctx->text );
                    break;
                case ASYNC_READ:
                    hr = prepare_read( ctx->read );
                    size = max( ctx->read->preparedSize, 1 );
                    break;
                case ASYNC_SUBMIT:
                    hr = submit_update( ctx->update );
                    break;
            }
            IXThreadingImpl_XAsyncComplete( xthreading, data->async, hr, SUCCEEDED(hr) ? size : 0 );
            hr = S_OK;
            break;

        case XAsyncOp_GetResult:
            switch (ctx->kind)
            {
                case ASYNC_INITIALIZE:
                    memcpy( data->buffer, &ctx->result, sizeof(ctx->result) );
                    ctx->result = NULL;
                    break;
                case ASYNC_QUOTA:
                    memcpy( data->buffer, &ctx->quota, sizeof(ctx->quota) );
                    break;
                case ASYNC_READ:
                    layout_read( ctx->read, data->buffer );
                    if (ctx->read->countOut) *ctx->read->countOut = ctx->read->preparedCount;
                    break;
                default:
                    break;
            }
            break;

        case XAsyncOp_Cleanup:
            if (ctx->result) close_provider( ctx->result );
            if (ctx->user) IXUserImpl_XUserCloseHandle( x_user_impl, ctx->user );
            free_read_request( ctx->read );
            free( ctx->text );
            free( ctx );
            break;

        case XAsyncOp_Cancel:
            break;
    }
    IXThreadingImpl_Release( xthreading );
    return hr;
}

static HRESULT begin_save_async( struct save_async *ctx, XAsyncBlock *async, const char *name )
{
    IXThreadingImpl *xthreading;
    HRESULT hr;

    if (!async)
    {
        free( ctx );
        return E_INVALIDARG;
    }
    if (FAILED(hr = QueryApiImpl( &CLSID_XThreadingImpl, &IID_IXThreadingImpl, (void **)&xthreading )))
    {
        free( ctx );
        return hr;
    }
    hr = IXThreadingImpl_XAsyncBegin( xthreading, async, ctx, begin_save_async, name, save_async_provider );
    IXThreadingImpl_Release( xthreading );
    return hr;
}

static HRESULT get_save_result( XAsyncBlock *async, SIZE_T size, void *buffer )
{
    IXThreadingImpl *xthreading;
    HRESULT hr;

    if (FAILED(hr = QueryApiImpl( &CLSID_XThreadingImpl, &IID_IXThreadingImpl, (void **)&xthreading ))) return hr;
    hr = IXThreadingImpl_XAsyncGetResult( xthreading, async, begin_save_async, size, buffer, NULL );
    IXThreadingImpl_Release( xthreading );
    TRACE( "async %p, size %Iu -> hr %#lx.\n", async, size, hr );
    return hr;
}

static HRESULT WINAPI x_game_save_XGameSaveInitializeProvider( IXGameSaveImpl3 *iface, XUserHandle requestingUser, const char *configurationId, BOOLEAN syncOnDemand, XGameSaveProviderHandle *provider )
{
    TRACE( "iface %p, requestingUser %p, configurationId %s, syncOnDemand %d, provider %p.\n", iface, requestingUser, debugstr_a( configurationId ), syncOnDemand, provider );
    return initialize_provider( requestingUser, configurationId, provider );
}

static HRESULT WINAPI x_game_save_XGameSaveInitializeProviderAsync( IXGameSaveImpl3 *iface, XUserHandle requestingUser, const char *configurationId, BOOLEAN syncOnDemand, XAsyncBlock *async )
{
    struct save_async *ctx;
    HRESULT hr;

    TRACE( "iface %p, requestingUser %p, configurationId %s, syncOnDemand %d, async %p.\n", iface, requestingUser, debugstr_a( configurationId ), syncOnDemand, async );

    if (!requestingUser || !configurationId) return E_INVALIDARG;
    if (!(ctx = calloc( 1, sizeof(*ctx) ))) return E_OUTOFMEMORY;
    ctx->kind = ASYNC_INITIALIZE;
    if (!(ctx->text = strdup( configurationId )) || FAILED(hr = IXUserImpl_XUserDuplicateHandle( x_user_impl, requestingUser, &ctx->user )))
    {
        free( ctx->text );
        free( ctx );
        return E_OUTOFMEMORY;
    }
    return begin_save_async( ctx, async, "XGameSaveInitializeProviderAsync" );
}

static HRESULT WINAPI x_game_save_XGameSaveInitializeProviderResult( IXGameSaveImpl3 *iface, XAsyncBlock *async, XGameSaveProviderHandle *provider )
{
    TRACE( "iface %p, async %p, provider %p.\n", iface, async, provider );
    if (!provider) return E_INVALIDARG;
    return get_save_result( async, sizeof(*provider), provider );
}

static void WINAPI x_game_save_XGameSaveCloseProvider( IXGameSaveImpl3 *iface, XGameSaveProviderHandle provider )
{
    TRACE( "iface %p, provider %p.\n", iface, provider );
    close_provider( provider );
}

static HRESULT WINAPI x_game_save_XGameSaveGetRemainingQuota( IXGameSaveImpl3 *iface, XGameSaveProviderHandle provider, INT64 *remainingQuota )
{
    TRACE( "iface %p, provider %p, remainingQuota %p.\n", iface, provider, remainingQuota );
    if (!provider || !remainingQuota) return E_INVALIDARG;
    EnterCriticalSection( &provider->cs );
    *remainingQuota = remaining_quota( provider );
    LeaveCriticalSection( &provider->cs );
    return S_OK;
}

static HRESULT WINAPI x_game_save_XGameSaveGetRemainingQuotaAsync( IXGameSaveImpl3 *iface, XGameSaveProviderHandle provider, XAsyncBlock *async )
{
    struct save_async *ctx;

    TRACE( "iface %p, provider %p, async %p.\n", iface, provider, async );

    if (!provider) return E_INVALIDARG;
    if (!(ctx = calloc( 1, sizeof(*ctx) ))) return E_OUTOFMEMORY;
    ctx->kind = ASYNC_QUOTA;
    ctx->provider = provider;
    return begin_save_async( ctx, async, "XGameSaveGetRemainingQuotaAsync" );
}

static HRESULT WINAPI x_game_save_XGameSaveGetRemainingQuotaResult( IXGameSaveImpl3 *iface, XAsyncBlock *async, INT64 *remainingQuota )
{
    TRACE( "iface %p, async %p, remainingQuota %p.\n", iface, async, remainingQuota );
    if (!remainingQuota) return E_INVALIDARG;
    return get_save_result( async, sizeof(*remainingQuota), remainingQuota );
}

static HRESULT WINAPI x_game_save_XGameSaveDeleteContainer( IXGameSaveImpl3 *iface, XGameSaveProviderHandle provider, const char *containerName )
{
    TRACE( "iface %p, provider %p, containerName %s.\n", iface, provider, debugstr_a( containerName ) );
    return delete_container( provider, containerName );
}

static HRESULT WINAPI x_game_save_XGameSaveDeleteContainerAsync( IXGameSaveImpl3 *iface, XGameSaveProviderHandle provider, const char *containerName, XAsyncBlock *async )
{
    struct save_async *ctx;

    TRACE( "iface %p, provider %p, containerName %s, async %p.\n", iface, provider, debugstr_a( containerName ), async );

    if (!provider || !containerName) return E_INVALIDARG;
    if (!(ctx = calloc( 1, sizeof(*ctx) ))) return E_OUTOFMEMORY;
    ctx->kind = ASYNC_DELETE;
    ctx->provider = provider;
    if (!(ctx->text = strdup( containerName )))
    {
        free( ctx );
        return E_OUTOFMEMORY;
    }
    return begin_save_async( ctx, async, "XGameSaveDeleteContainerAsync" );
}

static HRESULT WINAPI x_game_save_XGameSaveDeleteContainerResult( IXGameSaveImpl3 *iface, XAsyncBlock *async )
{
    TRACE( "iface %p, async %p.\n", iface, async );
    return get_save_result( async, 0, NULL );
}

static HRESULT WINAPI x_game_save_XGameSaveGetContainerInfo( IXGameSaveImpl3 *iface, XGameSaveProviderHandle provider, const char *containerName, void *context, XGameSaveContainerInfoCallback *callback )
{
    TRACE( "iface %p, provider %p, containerName %s, context %p, callback %p.\n", iface, provider, debugstr_a( containerName ), context, callback );
    if (!containerName) return E_INVALIDARG;
    return enumerate_containers( provider, NULL, containerName, context, callback );
}

static HRESULT WINAPI x_game_save_XGameSaveEnumerateContainerInfo( IXGameSaveImpl3 *iface, XGameSaveProviderHandle provider, void *context, XGameSaveContainerInfoCallback *callback )
{
    TRACE( "iface %p, provider %p, context %p, callback %p.\n", iface, provider, context, callback );
    return enumerate_containers( provider, NULL, NULL, context, callback );
}

static HRESULT WINAPI x_game_save_XGameSaveEnumerateContainerInfoByName( IXGameSaveImpl3 *iface, XGameSaveProviderHandle provider, const char *containerNamePrefix, void *context, XGameSaveContainerInfoCallback *callback )
{
    TRACE( "iface %p, provider %p, containerNamePrefix %s, context %p, callback %p.\n", iface, provider, debugstr_a( containerNamePrefix ), context, callback );
    return enumerate_containers( provider, containerNamePrefix, NULL, context, callback );
}

static HRESULT WINAPI x_game_save_XGameSaveCreateContainer( IXGameSaveImpl3 *iface, XGameSaveProviderHandle provider, const char *containerName, XGameSaveContainerHandle *containerContext )
{
    struct XGameSaveContainer *container;

    TRACE( "iface %p, provider %p, containerName %s, containerContext %p.\n", iface, provider, debugstr_a( containerName ), containerContext );

    if (!provider || !containerContext) return E_INVALIDARG;
    if (!containerName || !*containerName || strlen( containerName ) > 256) return E_GS_INVALID_CONTAINER_NAME;
    if (!(container = calloc( 1, sizeof(*container) ))) return E_OUTOFMEMORY;
    container->provider = provider;
    if (!(container->name = strdup( containerName )))
    {
        free( container );
        return E_OUTOFMEMORY;
    }
    *containerContext = container;
    return S_OK;
}

static void WINAPI x_game_save_XGameSaveCloseContainer( IXGameSaveImpl3 *iface, XGameSaveContainerHandle context )
{
    TRACE( "iface %p, context %p.\n", iface, context );
    if (!context) return;
    free( context->name );
    free( context );
}

static HRESULT WINAPI x_game_save_XGameSaveEnumerateBlobInfo( IXGameSaveImpl3 *iface, XGameSaveContainerHandle container, void *context, XGameSaveBlobInfoCallback *callback )
{
    TRACE( "iface %p, container %p, context %p, callback %p.\n", iface, container, context, callback );
    return enumerate_blobs( container, NULL, context, callback );
}

static HRESULT WINAPI x_game_save_XGameSaveEnumerateBlobInfoByName( IXGameSaveImpl3 *iface, XGameSaveContainerHandle container, const char *blobNamePrefix, void *context, XGameSaveBlobInfoCallback *callback )
{
    TRACE( "iface %p, container %p, blobNamePrefix %s, context %p, callback %p.\n", iface, container, debugstr_a( blobNamePrefix ), context, callback );
    return enumerate_blobs( container, blobNamePrefix, context, callback );
}

static HRESULT WINAPI x_game_save_XGameSaveReadBlobData( IXGameSaveImpl3 *iface, XGameSaveContainerHandle container, const char **blobNames, UINT32 *countOfBlobs, SIZE_T blobsSize, XGameSaveBlob *blobData )
{
    struct read_request *req;
    HRESULT hr;

    TRACE( "iface %p, container %p, blobNames %p, countOfBlobs %p, blobsSize %Iu, blobData %p.\n", iface, container, blobNames, countOfBlobs, blobsSize, blobData );

    if (!container || !countOfBlobs || (!blobData && blobsSize)) return E_INVALIDARG;
    if (!(req = create_read_request( container, blobNames, blobNames ? *countOfBlobs : 0 ))) return E_OUTOFMEMORY;
    if (SUCCEEDED(hr = prepare_read( req )))
    {
        if (blobsSize < req->preparedSize) hr = E_GS_PROVIDED_BUFFER_TOO_SMALL;
        else
        {
            layout_read( req, blobData );
            *countOfBlobs = req->preparedCount;
        }
    }
    free_read_request( req );
    return hr;
}

static HRESULT WINAPI x_game_save_XGameSaveReadBlobDataAsync( IXGameSaveImpl3 *iface, XGameSaveContainerHandle container, const char **blobNames, UINT32 countOfBlobs, XAsyncBlock *async )
{
    struct save_async *ctx;

    TRACE( "iface %p, container %p, blobNames %p, countOfBlobs %u, async %p.\n", iface, container, blobNames, countOfBlobs, async );

    if (!container) return E_INVALIDARG;
    if (!(ctx = calloc( 1, sizeof(*ctx) ))) return E_OUTOFMEMORY;
    ctx->kind = ASYNC_READ;
    if (!(ctx->read = create_read_request( container, blobNames, blobNames ? countOfBlobs : 0 )))
    {
        free( ctx );
        return E_OUTOFMEMORY;
    }
    return begin_save_async( ctx, async, "XGameSaveReadBlobDataAsync" );
}

static HRESULT WINAPI x_game_save_XGameSaveReadBlobDataResult( IXGameSaveImpl3 *iface, XAsyncBlock *async, SIZE_T blobsSize, XGameSaveBlob *blobData, UINT32 *countOfBlobs )
{
    struct save_async *ctx;

    TRACE( "iface %p, async %p, blobsSize %Iu, blobData %p, countOfBlobs %p.\n", iface, async, blobsSize, blobData, countOfBlobs );

    if (!async || !countOfBlobs || !async->internal[0]) return E_INVALIDARG;
    *countOfBlobs = 0;
    ctx = ((XAsyncProviderData *)async->internal[0])->context;
    if (ctx && ctx->read) ctx->read->countOut = countOfBlobs;
    return get_save_result( async, blobsSize, blobData );
}

static HRESULT WINAPI x_game_save_XGameSaveCreateUpdate( IXGameSaveImpl3 *iface, XGameSaveContainerHandle container, const char *containerDisplayName, XGameSaveUpdateHandle *updateContext )
{
    struct XGameSaveUpdate *update;

    TRACE( "iface %p, container %p, containerDisplayName %s, updateContext %p.\n", iface, container, debugstr_a( containerDisplayName ), updateContext );

    if (!container || !updateContext) return E_INVALIDARG;
    if (!(update = calloc( 1, sizeof(*update) ))) return E_OUTOFMEMORY;
    update->container = container;
    if (containerDisplayName && !(update->display = strdup( containerDisplayName )))
    {
        free( update );
        return E_OUTOFMEMORY;
    }
    *updateContext = update;
    return S_OK;
}

static void WINAPI x_game_save_XGameSaveCloseUpdate( IXGameSaveImpl3 *iface, XGameSaveUpdateHandle context )
{
    TRACE( "iface %p, context %p.\n", iface, context );
    if (!context) return;
    for (UINT32 i = 0; i < context->count; i++)
    {
        free( context->ops[i].name );
        free( context->ops[i].data );
    }
    free( context->ops );
    free( context->display );
    free( context );
}

static HRESULT add_update_op( struct XGameSaveUpdate *update, const char *name, const UINT8 *data, SIZE_T size, BOOL remove )
{
    struct update_op *op;

    if (!update || !name || !*name) return E_INVALIDARG;
    for (UINT32 i = 0; i < update->count; i++)
        if (!strcmp( update->ops[i].name, name )) return E_INVALIDARG;
    if (update->count == update->capacity)
    {
        UINT32 capacity = update->capacity ? update->capacity * 2 : 8;
        if (!(op = realloc( update->ops, capacity * sizeof(*op) ))) return E_OUTOFMEMORY;
        update->ops = op;
        update->capacity = capacity;
    }
    op = &update->ops[update->count];
    memset( op, 0, sizeof(*op) );
    if (!(op->name = strdup( name ))) return E_OUTOFMEMORY;
    op->remove = remove;
    if (!remove)
    {
        if (!(op->data = malloc( max( size, 1 ) )))
        {
            free( op->name );
            return E_OUTOFMEMORY;
        }
        if (size) memcpy( op->data, data, size );
        op->size = size;
    }
    update->count++;
    return S_OK;
}

static HRESULT WINAPI x_game_save_XGameSaveSubmitBlobWrite( IXGameSaveImpl3 *iface, XGameSaveUpdateHandle updateContext, const char *blobName, UINT8 *data, SIZE_T byteCount )
{
    TRACE( "iface %p, updateContext %p, blobName %s, data %p, byteCount %Iu.\n", iface, updateContext, debugstr_a( blobName ), data, byteCount );
    if (!data && byteCount) return E_INVALIDARG;
    return add_update_op( updateContext, blobName, data, byteCount, FALSE );
}

static HRESULT WINAPI x_game_save_XGameSaveSubmitBlobDelete( IXGameSaveImpl3 *iface, XGameSaveUpdateHandle updateContext, const char *blobName )
{
    TRACE( "iface %p, updateContext %p, blobName %s.\n", iface, updateContext, debugstr_a( blobName ) );
    return add_update_op( updateContext, blobName, NULL, 0, TRUE );
}

static HRESULT WINAPI x_game_save_XGameSaveSubmitUpdate( IXGameSaveImpl3 *iface, XGameSaveUpdateHandle updateContext )
{
    TRACE( "iface %p, updateContext %p.\n", iface, updateContext );
    if (!updateContext) return E_INVALIDARG;
    return submit_update( updateContext );
}

static HRESULT WINAPI x_game_save_XGameSaveSubmitUpdateAsync( IXGameSaveImpl3 *iface, XGameSaveUpdateHandle updateContext, XAsyncBlock *async )
{
    struct save_async *ctx;

    TRACE( "iface %p, updateContext %p, async %p.\n", iface, updateContext, async );

    if (!updateContext) return E_INVALIDARG;
    if (!(ctx = calloc( 1, sizeof(*ctx) ))) return E_OUTOFMEMORY;
    ctx->kind = ASYNC_SUBMIT;
    ctx->update = updateContext;
    return begin_save_async( ctx, async, "XGameSaveSubmitUpdateAsync" );
}

static HRESULT WINAPI x_game_save_XGameSaveSubmitUpdateResult( IXGameSaveImpl3 *iface, XAsyncBlock *async )
{
    TRACE( "iface %p, async %p.\n", iface, async );
    return get_save_result( async, 0, NULL );
}

static HRESULT WINAPI x_game_save_XGameSaveFilesGetFolderWithUiAsync( IXGameSaveImpl3 *iface, XUserHandle requestingUser, const char *configurationId, XAsyncBlock *async )
{
    FIXME( "iface %p, requestingUser %p, configurationId %s, async %p stub!\n", iface, requestingUser, debugstr_a( configurationId ), async );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_save_XGameSaveFilesGetFolderWithUiResult( IXGameSaveImpl3 *iface, XAsyncBlock *async, SIZE_T folderSize, char *folderResult )
{
    FIXME( "iface %p, async %p, folderSize %Iu, folderResult %p stub!\n", iface, async, folderSize, folderResult );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_save_XGameSaveFilesGetRemainingQuota( IXGameSaveImpl3 *iface, XUserHandle userContext, const char *configurationId, INT64 *remainingQuota )
{
    FIXME( "iface %p, userContext %p, configurationId %s, remainingQuota %p stub!\n", iface, userContext, debugstr_a( configurationId ), remainingQuota );
    return E_NOTIMPL;
}

#define UNKNOWN_SLOT(n) \
static HRESULT WINAPI x_game_save_unknown##n( void *iface, void *a, void *b, void *c, void *d ) \
{ \
    FIXME( "unknown IXGameSaveImpl4 method %d called, iface %p, args %p %p %p %p.\n", n, iface, a, b, c, d ); \
    return E_NOTIMPL; \
}
UNKNOWN_SLOT(0) UNKNOWN_SLOT(1) UNKNOWN_SLOT(2) UNKNOWN_SLOT(3) UNKNOWN_SLOT(4) UNKNOWN_SLOT(5) UNKNOWN_SLOT(6) UNKNOWN_SLOT(7)
UNKNOWN_SLOT(8) UNKNOWN_SLOT(9) UNKNOWN_SLOT(10) UNKNOWN_SLOT(11) UNKNOWN_SLOT(12) UNKNOWN_SLOT(13) UNKNOWN_SLOT(14) UNKNOWN_SLOT(15)

static const struct
{
    struct IXGameSaveImpl3Vtbl base;
    void *extra[16];
} x_game_save_vtbl_ext =
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
    x_game_save_unknown0, x_game_save_unknown1, x_game_save_unknown2, x_game_save_unknown3,
    x_game_save_unknown4, x_game_save_unknown5, x_game_save_unknown6, x_game_save_unknown7,
    x_game_save_unknown8, x_game_save_unknown9, x_game_save_unknown10, x_game_save_unknown11,
    x_game_save_unknown12, x_game_save_unknown13, x_game_save_unknown14, x_game_save_unknown15,
}
};

static struct x_game_save x_game_save =
{
    {&x_game_save_vtbl_ext.base},
    0,
};

IXGameSaveImpl *x_game_save_impl = (IXGameSaveImpl *)&x_game_save.IXGameSaveImpl3_iface;
