/*
 * Xbox Game runtime Library
 *  Xodus Interopability Layer -> test-only host library with a server thread
 *
 * Serves the xodus IPC protocol on a caller-chosen unix socket so Wine tests
 * can drive UnixTransport and XodusClient. Test only; never deployed.
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

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "xodus_test_server.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

typedef int32_t NTSTATUS;
typedef NTSTATUS (*unixlib_entry_t)( void *args );

#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000D)

#define FRAME_MAGIC 0x58445358u
#define HEADER_SIZE 8u
#define MAX_PAYLOAD 0xFFFFu

static struct
{
    int listen_fd;
    char path[108];
    int mode;
    uint8_t *resp;
    uint32_t resp_len;
    pthread_t thread;
    atomic_bool stop;
    atomic_bool running;
    atomic_uint accepted;
    atomic_uint frames_answered;
    atomic_ullong bytes_received;
    pthread_mutex_t last_lock;
    uint16_t last_type;
    uint32_t last_len;
    uint8_t last_body[MAX_PAYLOAD];
} srv = { .listen_fd = -1, .last_lock = PTHREAD_MUTEX_INITIALIZER };

static int wait_readable( int fd )
{
    struct pollfd pfd = { fd, POLLIN, 0 };
    int ret = poll( &pfd, 1, 20 );

    return ret > 0 ? 1 : (ret == 0 || errno == EINTR) ? 0 : -1;
}

static int send_all( int fd, const uint8_t *p, size_t n )
{
    while (n)
    {
        ssize_t w = send( fd, p, n, MSG_NOSIGNAL );
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return -1;
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

static int reply( int fd, uint16_t type, const uint8_t *body, uint32_t len )
{
    uint8_t hdr[HEADER_SIZE];

    memcpy( hdr, &(uint32_t){ FRAME_MAGIC }, 4 ); /* little-endian hosts only: x86_64 / arm64 */
    memcpy( hdr + 4, &type, 2 );
    memcpy( hdr + 6, &(uint16_t){ (uint16_t)len }, 2 );
    return send_all( fd, hdr, HEADER_SIZE ) || (len && send_all( fd, body, len ));
}

static void serve( int fd )
{
    static uint8_t buf[HEADER_SIZE + MAX_PAYLOAD];
    size_t have = 0;

    while (!atomic_load( &srv.stop ))
    {
        int r = wait_readable( fd );
        ssize_t n;

        if (r < 0) return;
        if (!r) continue;
        n = recv( fd, buf + have, sizeof(buf) - have, 0 );
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return;
        have += (size_t)n;
        atomic_fetch_add( &srv.bytes_received, (unsigned long long)n );
        if (srv.mode == XTS_MODE_SILENT) { have = 0; continue; }

        while (have >= HEADER_SIZE)
        {
            uint16_t type, len;
            memcpy( &type, buf + 4, 2 );
            memcpy( &len, buf + 6, 2 );
            if (have < HEADER_SIZE + len) break;
            pthread_mutex_lock( &srv.last_lock );
            srv.last_type = type;
            srv.last_len = len;
            memcpy( srv.last_body, buf + HEADER_SIZE, len );
            pthread_mutex_unlock( &srv.last_lock );
            if (type == 1 && reply( fd, 2, buf + HEADER_SIZE, len )) return;
            if (type == 3 || type == 5 || type == 7)
            {
                int failed;
                pthread_mutex_lock( &srv.last_lock );
                failed = reply( fd, type + 1, srv.resp, srv.resp_len );
                pthread_mutex_unlock( &srv.last_lock );
                if (failed) return;
            }
            atomic_fetch_add( &srv.frames_answered, 1 );
            memmove( buf, buf + HEADER_SIZE + len, have - HEADER_SIZE - len );
            have -= HEADER_SIZE + len;
        }
    }
}

static void *server_main( void *unused )
{
    (void)unused;
    while (!atomic_load( &srv.stop ))
    {
        int c, r = wait_readable( srv.listen_fd );

        if (r <= 0) continue;
        c = accept( srv.listen_fd, NULL, NULL );
        if (c < 0) continue;
#ifdef SO_NOSIGPIPE
        setsockopt( c, SOL_SOCKET, SO_NOSIGPIPE, &(int){ 1 }, sizeof(int) );
#endif
        atomic_fetch_add( &srv.accepted, 1 );
        if (srv.mode != XTS_MODE_CLOSE) serve( c );
        close( c );
    }
    return NULL;
}

static NTSTATUS xts_start( void *args )
{
    struct xts_start_args *a = args;
    struct sockaddr_un addr;

    a->status = 0;
    a->err = 0;
    if (atomic_load( &srv.running ) || !a->path || strlen( a->path ) + 1 > sizeof(addr.sun_path))
        return STATUS_INVALID_PARAMETER;

    srv.mode = a->mode;
    srv.resp_len = a->resp_len;
    srv.resp = a->resp_len ? malloc( a->resp_len ) : NULL;
    if (a->resp_len) memcpy( srv.resp, a->resp, a->resp_len );
    strcpy( srv.path, a->path );
    atomic_store( &srv.stop, false );
    atomic_store( &srv.accepted, 0 );
    atomic_store( &srv.frames_answered, 0 );
    atomic_store( &srv.bytes_received, 0 );
    srv.last_type = 0;
    srv.last_len = 0;

    unlink( srv.path );
    srv.listen_fd = socket( AF_UNIX, SOCK_STREAM, 0 );
    memset( &addr, 0, sizeof(addr) );
    addr.sun_family = AF_UNIX;
    strcpy( addr.sun_path, srv.path );
    if (srv.listen_fd < 0 || bind( srv.listen_fd, (struct sockaddr *)&addr, sizeof(addr) ) < 0 ||
        listen( srv.listen_fd, 4 ) < 0)
    {
        a->status = 1;
        a->err = errno;
        if (srv.listen_fd >= 0) close( srv.listen_fd );
        srv.listen_fd = -1;
        free( srv.resp );
        return STATUS_SUCCESS;
    }
    atomic_store( &srv.running, true );
    pthread_create( &srv.thread, NULL, server_main, NULL );
    return STATUS_SUCCESS;
}

static NTSTATUS xts_stats( void *args )
{
    struct xts_stats_args *a = args;

    a->accepted = atomic_load( &srv.accepted );
    a->frames_answered = atomic_load( &srv.frames_answered );
    a->bytes_received = atomic_load( &srv.bytes_received );
    return STATUS_SUCCESS;
}

static NTSTATUS xts_last( void *args )
{
    struct xts_last_args *a = args;

    pthread_mutex_lock( &srv.last_lock );
    a->type = srv.last_type;
    a->len = srv.last_len;
    memcpy( a->buf, srv.last_body, srv.last_len < a->cap ? srv.last_len : a->cap );
    pthread_mutex_unlock( &srv.last_lock );
    return STATUS_SUCCESS;
}

static NTSTATUS xts_setresp( void *args )
{
    struct xts_setresp_args *a = args;
    uint8_t *copy = a->resp_len ? malloc( a->resp_len ) : NULL;
    uint8_t *old;

    if (a->resp_len) memcpy( copy, a->resp, a->resp_len );
    pthread_mutex_lock( &srv.last_lock );
    old = srv.resp;
    srv.resp = copy;
    srv.resp_len = a->resp_len;
    pthread_mutex_unlock( &srv.last_lock );
    free( old );
    return STATUS_SUCCESS;
}

static NTSTATUS xts_stop( void *args )
{
    struct xts_stop_args *a = args;

    a->status = 0;
    if (!atomic_load( &srv.running )) return STATUS_SUCCESS;
    atomic_store( &srv.stop, true );
    pthread_join( srv.thread, NULL );
    close( srv.listen_fd );
    unlink( srv.path );
    free( srv.resp );
    srv.resp = NULL;
    srv.listen_fd = -1;
    atomic_store( &srv.running, false );
    return STATUS_SUCCESS;
}

__attribute__((visibility("default")))
const unixlib_entry_t __wine_unix_call_funcs[] =
{
    xts_start,
    xts_stats,
    xts_stop,
    xts_last,
    xts_setresp,
};
