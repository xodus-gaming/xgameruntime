/*
 * Xbox Game runtime Library
 *  Xodus Interopability Layer -> xgameruntime-unix.so
 *
 * Host side of the xodus IPC socket. Exports __wine_unix_call_funcs; built
 * with the host compiler and no Wine headers. See design.md "Unix call ABI".
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

#ifndef __APPLE__
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "xodus_abi.h"
#include "xodus_path.h"

#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0 /* Darwin: FD_CLOEXEC is set with fcntl right after socket(). */
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0 /* Darwin: SO_NOSIGPIPE is set on the socket instead. */
#endif

typedef int32_t NTSTATUS;
typedef NTSTATUS (*unixlib_entry_t)( void *args );

#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000D)

static int xodus_fd = -1;

static uint64_t now_ms( void )
{
    struct timespec ts;

    clock_gettime( CLOCK_MONOTONIC, &ts );
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static int remaining_ms( uint64_t deadline )
{
    uint64_t now = now_ms();

    return now >= deadline ? 0 : (int)(deadline - now);
}

/* 1 = ready, 0 = deadline passed, -1 = error (errno set). EINTR is retried. */
static int wait_fd( int fd, short events, uint64_t deadline )
{
    struct pollfd pfd;
    int ret;

    pfd.fd = fd;
    pfd.events = events;
    for (;;)
    {
        pfd.revents = 0;
        ret = poll( &pfd, 1, remaining_ms( deadline ) );
        if (ret > 0) return 1;
        if (ret == 0) return 0;
        if (errno != EINTR) return -1;
    }
}

static void set_result( int32_t *status, int32_t *err, int s, int e )
{
    *status = s;
    *err = e;
}

static void set_errno_result( int32_t *status, int32_t *err, int e )
{
    set_result( status, err, (e == EPIPE || e == ECONNRESET) ? XODUS_PEER_CLOSED : XODUS_ERROR, e );
}

static void close_fd( void )
{
    if (xodus_fd < 0) return;
    close( xodus_fd ); /* once; EINTR counts as closed */
    xodus_fd = -1;
}

static bool get_peer_uid( int fd, uid_t *uid )
{
#ifdef __APPLE__
    gid_t gid;
    return getpeereid( fd, uid, &gid ) == 0;
#else
    struct ucred cred;
    socklen_t len = sizeof(cred);

    if (getsockopt( fd, SOL_SOCKET, SO_PEERCRED, &cred, &len ) != 0) return false;
    *uid = cred.uid;
    return true;
#endif
}

static NTSTATUS xodus_connect( void *args )
{
    struct xodus_connect_args *a = args;
    char default_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    struct sockaddr_un addr;
    const char *path = a->path;
    uint64_t deadline = now_ms() + a->timeout_ms;
    uid_t peer, expected;
    int fd, w, so_err = 0;
    socklen_t so_len = sizeof(so_err);

    close_fd();

    if (!path || !path[0])
    {
        if (!xodus_default_path( getenv( "XDG_RUNTIME_DIR" ), default_path, sizeof(default_path) ))
        {
            set_result( &a->status, &a->err, XODUS_ERROR, EINVAL );
            return STATUS_SUCCESS;
        }
        path = default_path;
    }
    if (strlen( path ) + 1 > sizeof(addr.sun_path))
    {
        set_result( &a->status, &a->err, XODUS_ERROR, ENAMETOOLONG );
        return STATUS_SUCCESS;
    }

    /* Close-on-exec from creation: a spawned child must never inherit the token-service connection. */
    fd = socket( AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0 );
    if (fd < 0)
    {
        set_result( &a->status, &a->err, XODUS_ERROR, errno );
        return STATUS_SUCCESS;
    }
    if (fcntl( fd, F_SETFD, FD_CLOEXEC ) < 0)
        goto fail_errno;
    if (fcntl( fd, F_SETFL, O_NONBLOCK ) < 0)
        goto fail_errno;
#ifdef SO_NOSIGPIPE
    {
        int one = 1;
        if (setsockopt( fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one) ) < 0) goto fail_errno;
    }
#endif

    memset( &addr, 0, sizeof(addr) );
    addr.sun_family = AF_UNIX;
    memcpy( addr.sun_path, path, strlen( path ) );

    if (connect( fd, (struct sockaddr *)&addr, sizeof(addr) ) < 0)
    {
        if (errno != EINPROGRESS && errno != EINTR) goto fail_errno;
        /* Never call connect() again: wait for completion, then read SO_ERROR. */
        w = wait_fd( fd, POLLOUT, deadline );
        if (w == 0)
        {
            close( fd );
            set_result( &a->status, &a->err, XODUS_TIMEOUT, 0 );
            return STATUS_SUCCESS;
        }
        if (w < 0) goto fail_errno;
        if (getsockopt( fd, SOL_SOCKET, SO_ERROR, &so_err, &so_len ) < 0) goto fail_errno;
        if (so_err)
        {
            close( fd );
            set_result( &a->status, &a->err, XODUS_ERROR, so_err );
            return STATUS_SUCCESS;
        }
    }

    expected = a->expected_uid == -1 ? geteuid() : (uid_t)a->expected_uid;
    if (!get_peer_uid( fd, &peer ) || !xodus_peer_ok( peer, expected ))
    {
        close( fd );
        set_result( &a->status, &a->err, XODUS_ERROR, EACCES );
        return STATUS_SUCCESS;
    }

    xodus_fd = fd;
    set_result( &a->status, &a->err, XODUS_OK, 0 );
    return STATUS_SUCCESS;

fail_errno:
    so_err = errno;
    close( fd );
    set_result( &a->status, &a->err, XODUS_ERROR, so_err );
    return STATUS_SUCCESS;
}

static NTSTATUS xodus_send( void *args )
{
    struct xodus_send_args *a = args;
    uint64_t deadline = now_ms() + a->timeout_ms;
    uint32_t sent = 0;
    ssize_t n;
    int w;

    if (xodus_fd < 0)
    {
        set_result( &a->status, &a->err, XODUS_ERROR, ENOTCONN );
        return STATUS_SUCCESS;
    }
    if (a->len && !a->buf) return STATUS_INVALID_PARAMETER;

    while (sent < a->len)
    {
        n = send( xodus_fd, a->buf + sent, a->len - sent, MSG_NOSIGNAL );
        if (n > 0) { sent += (uint32_t)n; continue; }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            w = wait_fd( xodus_fd, POLLOUT, deadline );
            if (w == 0) { set_result( &a->status, &a->err, XODUS_TIMEOUT, 0 ); return STATUS_SUCCESS; }
            if (w > 0) continue;
        }
        set_errno_result( &a->status, &a->err, n < 0 ? errno : EIO );
        return STATUS_SUCCESS;
    }
    set_result( &a->status, &a->err, XODUS_OK, 0 );
    return STATUS_SUCCESS;
}

static NTSTATUS xodus_recv( void *args )
{
    struct xodus_recv_args *a = args;
    uint64_t deadline = now_ms() + a->timeout_ms;
    uint32_t cap = a->cap < XODUS_RECV_MAX ? a->cap : XODUS_RECV_MAX;
    ssize_t n;
    int w;

    a->got = 0;
    if (xodus_fd < 0)
    {
        set_result( &a->status, &a->err, XODUS_ERROR, ENOTCONN );
        return STATUS_SUCCESS;
    }
    if (!cap || !a->buf) return STATUS_INVALID_PARAMETER;

    for (;;)
    {
        w = wait_fd( xodus_fd, POLLIN, deadline );
        if (w == 0) { set_result( &a->status, &a->err, XODUS_TIMEOUT, 0 ); return STATUS_SUCCESS; }
        if (w < 0) { set_errno_result( &a->status, &a->err, errno ); return STATUS_SUCCESS; }

        n = recv( xodus_fd, a->buf, cap, 0 );
        if (n > 0)
        {
            a->got = (uint32_t)n;
            set_result( &a->status, &a->err, XODUS_OK, 0 );
            return STATUS_SUCCESS;
        }
        if (n == 0) { set_result( &a->status, &a->err, XODUS_PEER_CLOSED, 0 ); return STATUS_SUCCESS; }
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
        set_errno_result( &a->status, &a->err, errno );
        return STATUS_SUCCESS;
    }
}

static NTSTATUS xodus_close( void *args )
{
    struct xodus_close_args *a = args;

    close_fd();
    set_result( &a->status, &a->err, XODUS_OK, 0 );
    return STATUS_SUCCESS;
}

__attribute__((visibility("default")))
const unixlib_entry_t __wine_unix_call_funcs[] =
{
    xodus_connect,
    xodus_send,
    xodus_recv,
    xodus_close,
};
