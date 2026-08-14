/*
 * Xbox Game runtime Library
 *  Unix side: talking to xodus-service
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

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "unixlib.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(gdkc);

static BOOL write_all( int fd, const void *buffer, size_t size )
{
    const char *ptr = buffer;

    while (size)
    {
        ssize_t written = write( fd, ptr, size );

        if (written < 0)
        {
            if (errno == EINTR) continue;
            return FALSE;
        }
        ptr += written;
        size -= written;
    }
    return TRUE;
}

static BOOL read_all( int fd, void *buffer, size_t size )
{
    char *ptr = buffer;

    while (size)
    {
        ssize_t got = read( fd, ptr, size );

        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return FALSE;  /* the service closed on us */
        ptr += got;
        size -= got;
    }
    return TRUE;
}

static int connect_to_service(void)
{
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    const char *path = getenv( "XODUS_SERVICE_SOCKET" );
    char buffer[sizeof(addr.sun_path)];
    int fd;

    if (!path)
    {
        const char *runtime_dir = getenv( "XDG_RUNTIME_DIR" );

        if (!runtime_dir)
        {
            WARN( "XDG_RUNTIME_DIR is unset; cannot find xodus-service.\n" );
            return -1;
        }
        snprintf( buffer, sizeof(buffer), "%s/xodus.sock", runtime_dir );
        path = buffer;
    }

    if (strlen( path ) >= sizeof(addr.sun_path))
    {
        WARN( "socket path %s is too long.\n", debugstr_a( path ) );
        return -1;
    }
    strcpy( addr.sun_path, path );

    if ((fd = socket( AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0 )) < 0) return -1;
    if (connect( fd, (struct sockaddr *)&addr, sizeof(addr) ) < 0)
    {
        WARN( "cannot reach xodus-service at %s: %s.\n", debugstr_a( path ), strerror( errno ) );
        close( fd );
        return -1;
    }
    return fd;
}

static NTSTATUS service_call( void *args )
{
    struct service_call_params *params = args;
    UINT32 magic = XODUS_XML_MAGIC;
    UINT16 type, size;
    char header[8];
    size_t request_len;
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    int fd;

    request_len = params->request ? strlen( params->request ) : 0;
    if (request_len > 0xffff) return STATUS_INVALID_PARAMETER;

    if ((fd = connect_to_service()) < 0) return XODUS_STATUS_NO_SERVICE;

    /* One request, one reply, one connection. The service handles messages in
     * a loop per connection, but a shared connection would need locking here
     * for no gain: these calls are rare and the token is cached service-side. */
    type = params->message_type;
    size = request_len;
    memcpy( header + 0, &magic, 4 );
    memcpy( header + 4, &type, 2 );
    memcpy( header + 6, &size, 2 );

    if (!write_all( fd, header, sizeof(header) ) ||
        (request_len && !write_all( fd, params->request, request_len )))
    {
        WARN( "failed to send message %u: %s.\n", params->message_type, strerror( errno ) );
        goto done;
    }

    if (!read_all( fd, header, sizeof(header) ))
    {
        WARN( "no reply to message %u.\n", params->message_type );
        goto done;
    }
    memcpy( &magic, header + 0, 4 );
    memcpy( &type, header + 4, 2 );
    memcpy( &size, header + 6, 2 );

    if (magic != XODUS_XML_MAGIC || type != params->message_type + 1)
    {
        WARN( "unexpected reply magic %#x type %u.\n", magic, type );
        status = STATUS_INVALID_NETWORK_RESPONSE;
        goto done;
    }
    if (!size)
    {
        /* The service answers a failed handler with an empty body. */
        WARN( "xodus-service could not handle message %u.\n", params->message_type );
        status = STATUS_UNSUCCESSFUL;
        goto done;
    }
    if (size + 1u > params->reply_size)
    {
        params->reply_size = size + 1;
        status = XODUS_STATUS_MORE_ROOM;
        goto done;
    }

    if (!read_all( fd, params->reply, size )) goto done;
    params->reply[size] = 0;
    params->reply_size = size;
    status = STATUS_SUCCESS;

done:
    close( fd );
    return status;
}

const unixlib_entry_t __wine_unix_call_funcs[] =
{
    service_call,
};

C_ASSERT( ARRAYSIZE(__wine_unix_call_funcs) == unix_funcs_count );
