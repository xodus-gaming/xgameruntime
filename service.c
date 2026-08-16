/*
 * Xbox Game runtime Library
 *  PE side: messages to xodus-service
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
#include "unixlib.h"

WINE_DEFAULT_DEBUG_CHANNEL(gdkc);

/*
 * MicrosoftGame.Config ships at the root of the package -- not necessarily
 * next to the executable -- and is where the
 * store, the packaging tools and the runtime all read a title's identity from:
 * its Xbox title id and the MSA app id it authenticates as. Reading it here
 * means every title gets the right values with no per-game table.
 */
char *xodus_game_config_value( const char *element )
{
    WCHAR path[MAX_PATH];
    HANDLE file;
    DWORD size, read;
    char *data, *value;
    WCHAR *sep;

    if (!GetModuleFileNameW( NULL, path, ARRAY_SIZE(path) )) return NULL;
    if (!(sep = wcsrchr( path, '\\' ))) return NULL;
    *sep = 0;

    /* The config lives at the package root, which is often not where the
     * executable lives: Unreal titles ship theirs several directories down, as
     * <root>\<Project>\Binaries\WinGDK\<Project>-WinGDK-Shipping.exe. Looking
     * only beside the executable found nothing for those, so XGameGetXboxTitleId
     * failed and the title asked Xbox Live for achievements with titleId=0 --
     * a query that cannot match anything. Walk up until it turns up. */
    for (;;)
    {
        WCHAR candidate[MAX_PATH];

        if (wcslen( path ) + ARRAY_SIZE(L"\\MicrosoftGame.Config") > ARRAY_SIZE(candidate)) return NULL;
        wcscpy( candidate, path );
        wcscat( candidate, L"\\MicrosoftGame.Config" );

        file = CreateFileW( candidate, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
        if (file != INVALID_HANDLE_VALUE)
        {
            TRACE( "using %s.\n", debugstr_w( candidate ) );
            break;
        }

        /* Stop at the drive root, where there is no separator left to trim. */
        if (!(sep = wcsrchr( path, '\\' )))
        {
            ERR( "no MicrosoftGame.Config anywhere above the executable; "
                 "the title has no title id and Xbox Live features will not work.\n" );
            return NULL;
        }
        *sep = 0;
    }

    size = GetFileSize( file, NULL );
    if (size == INVALID_FILE_SIZE || size > 0x100000 || !(data = malloc( size + 1 )))
    {
        CloseHandle( file );
        return NULL;
    }
    if (!ReadFile( file, data, size, &read, NULL )) read = 0;
    CloseHandle( file );
    data[read] = 0;

    value = xodus_xml_element( data, element );
    free( data );
    return value;
}

/*
 * The service speaks a small XML dialect (quick-xml on the other end), and the
 * documents involved are a handful of flat string elements. A real parser would
 * be a dependency and an attack surface for no benefit, so extract elements by
 * name and reject anything that does not look like what we asked for.
 */
char *xodus_xml_element( const char *xml, const char *name )
{
    char open[64], close[64];
    const char *start, *end;
    char *value;
    SIZE_T len;

    if (snprintf( open, sizeof(open), "<%s>", name ) >= (int)sizeof(open)) return NULL;
    if (snprintf( close, sizeof(close), "</%s>", name ) >= (int)sizeof(close)) return NULL;

    if (!(start = strstr( xml, open ))) return NULL;
    start += strlen( open );
    if (!(end = strstr( start, close ))) return NULL;

    len = end - start;
    if (!(value = malloc( len + 1 ))) return NULL;
    memcpy( value, start, len );
    value[len] = 0;
    return value;
}

/*
 * Send one message and hand back the reply body.
 *
 * The reply is sized by the service, not by us, so grow the buffer once if the
 * first attempt was too small rather than guessing high: XSTS tokens are a few
 * kilobytes but nothing in the protocol promises that.
 */
HRESULT xodus_service_call( UINT16 message_type, const char *request, char **reply )
{
    struct service_call_params params;
    NTSTATUS status;
    char *buffer;
    UINT32 size = 8192;

    *reply = NULL;
    if (!(buffer = malloc( size ))) return E_OUTOFMEMORY;

    for (;;)
    {
        params.message_type = message_type;
        params.request = request;
        params.reply = buffer;
        params.reply_size = size;

        status = XGAMERUNTIME_UNIX_CALL( service_call, &params );
        if (status != XODUS_STATUS_MORE_ROOM) break;

        size = params.reply_size;
        free( buffer );
        if (!(buffer = malloc( size ))) return E_OUTOFMEMORY;
    }

    if (status)
    {
        free( buffer );
        WARN( "message %u failed, status %#lx.\n", message_type, status );
        /* Nothing here is recoverable by the title: either xodus-service is not
         * running or it could not reach Xbox Live. */
        return status == XODUS_STATUS_NO_SERVICE ? E_GAMEUSER_NO_AUTH_USER
                                                   : E_GAMEUSER_FAILED_TO_GET_TOKEN;
    }

    TRACE( "message %u answered with %u bytes.\n", message_type, params.reply_size );
    *reply = buffer;
    return S_OK;
}
