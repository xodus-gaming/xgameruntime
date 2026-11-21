/*
 * Xbox Game runtime Library
 *  Xodus Interopability Layer -> host tests for xgameruntime-unix.so
 *
 * Plain C, host compiler, no Wine and no gtest. Includes xodus_socket.c
 * directly with connect()/close() wrapped so the call counts can be checked.
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
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Wrappers around the real calls, defined before the macros below. */
enum wrap_mode { WRAP_PASS, WRAP_CONNECT_INPROGRESS, WRAP_CONNECT_EINTR, WRAP_CLOSE_EINTR, WRAP_CONNECT_STUCK };
static enum wrap_mode wrap_mode = WRAP_PASS;
static int connect_calls, close_calls, watch_fd = -1, fake_so_error;
static int eintr_next_send, eintr_next_recv, peer_cred_fails;

static int test_connect( int fd, const struct sockaddr *a, socklen_t len )
{
    int rc;

    connect_calls++;
    if (wrap_mode == WRAP_CONNECT_STUCK) { errno = EINPROGRESS; return -1; } /* never completes */
    rc = connect( fd, a, len );
    if (rc == 0 && wrap_mode == WRAP_CONNECT_INPROGRESS) { errno = EINPROGRESS; return -1; }
    if (rc < 0 && wrap_mode == WRAP_CONNECT_INPROGRESS) /* the failure is reported later, through SO_ERROR */
    {
        fake_so_error = errno;
        errno = EINPROGRESS;
        return -1;
    }
    if (rc == 0 && wrap_mode == WRAP_CONNECT_EINTR) { errno = EINTR; return -1; }
    return rc;
}

static int test_close( int fd )
{
    int rc = close( fd );

    if (fd != watch_fd) return rc; /* server threads close their own fds */
    close_calls++;
    if (wrap_mode == WRAP_CLOSE_EINTR) { errno = EINTR; return -1; }
    return rc;
}

static ssize_t test_send( int fd, const void *buf, size_t len, int flags )
{
    if (eintr_next_send) { eintr_next_send = 0; errno = EINTR; return -1; }
    return send( fd, buf, len, flags );
}

static ssize_t test_recv( int fd, void *buf, size_t len, int flags )
{
    if (eintr_next_recv) { eintr_next_recv = 0; errno = EINTR; return -1; }
    return recv( fd, buf, len, flags );
}

#ifdef __APPLE__
static int test_getpeereid( int fd, uid_t *uid, gid_t *gid )
{
    if (peer_cred_fails) { errno = EBADF; return -1; }
    return getpeereid( fd, uid, gid );
}
#define getpeereid test_getpeereid
#endif

static int test_getsockopt( int fd, int level, int name, void *val, socklen_t *len )
{
#ifdef SO_PEERCRED
    if (peer_cred_fails && level == SOL_SOCKET && name == SO_PEERCRED) { errno = EBADF; return -1; }
#endif
    if (wrap_mode == WRAP_CONNECT_INPROGRESS && level == SOL_SOCKET && name == SO_ERROR && fake_so_error)
    {
        *(int *)val = fake_so_error;
        *len = sizeof(int);
        fake_so_error = 0;
        return 0;
    }
    return getsockopt( fd, level, name, val, len );
}

static int test_poll( struct pollfd *fds, nfds_t n, int timeout )
{
    if (wrap_mode == WRAP_CONNECT_STUCK) return 0; /* the connect deadline passes */
    if (wrap_mode == WRAP_CONNECT_INPROGRESS && fake_so_error) /* the pending failure completes at once */
    {
        fds[0].revents = fds[0].events;
        return 1;
    }
    return poll( fds, n, timeout );
}

#define connect test_connect
#define close test_close
#define getsockopt test_getsockopt
#define poll test_poll
#define send test_send
#define recv test_recv
#include "xodus_socket.c"
#undef connect
#undef close
#undef getsockopt
#undef poll
#undef send
#undef recv
#ifdef __APPLE__
#undef getpeereid
#endif

static int failures;

#define CHECK( cond ) \
    do { if (!(cond)) { printf( "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond ); failures++; } } while (0)

/* ------------------------------------------------------------------ */
/* Test server: accepts one connection per start; behavior set by mode. */

enum srv_mode { SRV_CLOSE, SRV_SEND_LATE, SRV_READ_SLOW };

struct server
{
    int listen_fd;
    char path[108];
    enum srv_mode mode;
    unsigned delay_ms;
    const uint8_t *data;
    size_t len;
    volatile size_t received;   /* SRV_READ_SLOW: bytes read until EOF */
    pthread_t thread;
};

static void *server_main( void *p )
{
    struct server *s = p;
    int c = accept( s->listen_fd, NULL, NULL );

    if (c < 0) return NULL;
#ifdef SO_NOSIGPIPE
    setsockopt( c, SOL_SOCKET, SO_NOSIGPIPE, &(int){ 1 }, sizeof(int) );
#endif
    if (s->mode == SRV_READ_SLOW)
    {
        char tmp[4096];
        ssize_t n;

        usleep( 200 * 1000 ); /* let the client fill the socket buffer first */
        while ((n = recv( c, tmp, sizeof(tmp), 0 )) > 0) s->received += (size_t)n;
    }
    if (s->mode == SRV_SEND_LATE)
    {
        setsockopt( c, SOL_SOCKET, SO_SNDBUF, &(int){ 1 << 20 }, sizeof(int) );
        usleep( s->delay_ms * 1000 );
        send( c, s->data, s->len, MSG_NOSIGNAL );
        usleep( 300 * 1000 ); /* hold the connection while the client reads */
    }
    close( c );
    return NULL;
}

static int server_start( struct server *s, const char *tag, enum srv_mode mode )
{
    struct sockaddr_un addr;

    memset( s, 0, sizeof(*s) );
    snprintf( s->path, sizeof(s->path), "/tmp/xodus-ht-%d-%s.sock", (int)getpid(), tag );
    unlink( s->path );
    s->mode = mode;
    s->listen_fd = socket( AF_UNIX, SOCK_STREAM, 0 );
    memset( &addr, 0, sizeof(addr) );
    addr.sun_family = AF_UNIX;
    strcpy( addr.sun_path, s->path );
    if (bind( s->listen_fd, (struct sockaddr *)&addr, sizeof(addr) ) < 0 || listen( s->listen_fd, 4 ) < 0)
    {
        printf( "server bind failed: %s\n", strerror( errno ) );
        return -1;
    }
    return 0;
}

static void server_run( struct server *s ) { pthread_create( &s->thread, NULL, server_main, s ); }

static void server_stop( struct server *s )
{
    pthread_join( s->thread, NULL );
    close( s->listen_fd );
    unlink( s->path );
}

static void do_connect( const char *path, int64_t uid, struct xodus_connect_args *a )
{
    memset( a, 0, sizeof(*a) );
    a->path = path;
    a->timeout_ms = 2000;
    a->expected_uid = uid;
    __wine_unix_call_funcs[XODUS_CALL_CONNECT]( a );
}

static void do_close( void )
{
    struct xodus_close_args c = {0};
    __wine_unix_call_funcs[XODUS_CALL_CLOSE]( &c );
}

/* Start a server and let it accept one connection. */
static void server_up( struct server *s, const char *tag, enum srv_mode mode, unsigned delay_ms,
                       const uint8_t *data, size_t len )
{
    CHECK( server_start( s, tag, mode ) == 0 );
    s->delay_ms = delay_ms;
    s->data = data;
    s->len = len;
    server_run( s );
}

static void connect_ok( const struct server *s )
{
    struct xodus_connect_args a;

    do_connect( s->path, -1, &a );
    CHECK( a.status == XODUS_OK );
}

static int count_open_fds( void )
{
    int n = 0;

    for (int fd = 0; fd < 256; fd++)
        if (fcntl( fd, F_GETFD ) != -1) n++;
    return n;
}

/* The connection is close-on-exec: the flag is set and a child exec'd by the host does not hold the fd. */
static void test_cloexec( void )
{
    struct server s;
    char cmd[32];
    int status = -1;
    pid_t pid;

    server_up( &s, "cloexec", SRV_CLOSE, 0, NULL, 0 );
    connect_ok( &s );
    CHECK( xodus_fd >= 0 && (fcntl( xodus_fd, F_GETFD ) & FD_CLOEXEC) );
    snprintf( cmd, sizeof(cmd), ": >&%d", xodus_fd ); /* sh fails when the fd is not open */
    pid = fork();
    if (pid == 0)
    {
        execl( "/bin/sh", "sh", "-c", cmd, (char *)NULL );
        _exit( 127 );
    }
    CHECK( pid > 0 && waitpid( pid, &status, 0 ) == pid && WIFEXITED( status ) && WEXITSTATUS( status ) != 0
           && WEXITSTATUS( status ) != 127 );
    do_close();
    server_stop( &s );
}

/* A connect that is refused (uid mismatch or failed credential lookup) leaves no open fd. */
static void expect_denied( const char *tag, int64_t uid, int cred_fail )
{
    struct server s;
    struct xodus_connect_args a;
    int base = count_open_fds();

    server_up( &s, tag, SRV_CLOSE, 0, NULL, 0 );
    peer_cred_fails = cred_fail;
    do_connect( s.path, uid, &a );
    peer_cred_fails = 0;
    CHECK( a.status == XODUS_ERROR && a.err == EACCES );
    CHECK( xodus_fd == -1 );
    server_stop( &s );
    CHECK( count_open_fds() == base );
}

static void install_handler( int sig, void (*fn)( int ) )
{
    struct sigaction sa;

    memset( &sa, 0, sizeof(sa) );
    sa.sa_handler = fn; /* no SA_RESTART */
    sigaction( sig, &sa, NULL );
}

/* ------------------------------------------------------------------ */

static void test_default_path( void )
{
    char out[64];
    char exact[sizeof("/xodus.sock") + 3];

    CHECK( xodus_peer_ok( 501, 501 ) );
    CHECK( !xodus_peer_ok( 501, 502 ) );

    CHECK( xodus_default_path_darwin( out, sizeof(out) ) && !strcmp( out, "/tmp/xodus.sock" ) );
    CHECK( xodus_default_path_xdg( "/run/user/1000", out, sizeof(out) ) && !strcmp( out, "/run/user/1000/xodus.sock" ) );
    CHECK( !xodus_default_path_xdg( NULL, out, sizeof(out) ) );
    CHECK( !xodus_default_path_xdg( "", out, sizeof(out) ) );
    CHECK( !xodus_default_path_xdg( "run/user/1000", out, sizeof(out) ) );
    /* length + 1 > out_size is rejected, exact fit accepted, nothing truncated. */
    CHECK( xodus_default_path_xdg( "/ab", exact, sizeof(exact) ) && !strcmp( exact, "/ab/xodus.sock" ) );
    memset( exact, 'Z', sizeof(exact) );
    CHECK( !xodus_default_path_xdg( "/abc", exact, sizeof(exact) ) );
    CHECK( exact[0] == 'Z' );
#ifdef __APPLE__
    CHECK( xodus_default_path( NULL, out, sizeof(out) ) && !strcmp( out, "/tmp/xodus.sock" ) );
#else
    CHECK( xodus_default_path( "/run/x", out, sizeof(out) ) && !strcmp( out, "/run/x/xodus.sock" ) );
    CHECK( !xodus_default_path( NULL, out, sizeof(out) ) );
#endif
}

static void test_connect_basics( void )
{
    struct server s;
    struct xodus_connect_args a;

    expect_denied( "uid", (int64_t)geteuid() + 1, 0 );

    /* matching explicit uid succeeds */
    server_up( &s, "ok", SRV_CLOSE, 0, NULL, 0 );
    do_connect( s.path, (int64_t)geteuid(), &a );
    CHECK( a.status == XODUS_OK && xodus_fd >= 0 );
    do_close();
    server_stop( &s );

    /* a path of exactly sizeof(sun_path) has no room for the NUL: rejected; one byte shorter is accepted */
    {
        char exact[sizeof(((struct sockaddr_un *)0)->sun_path) + 1];

        memset( exact, 'a', sizeof(exact) );
        exact[0] = '/';
        exact[sizeof(exact) - 1] = 0;
        do_connect( exact, -1, &a );
        CHECK( a.status == XODUS_ERROR && a.err == ENAMETOOLONG );
        exact[sizeof(exact) - 2] = 0;
        do_connect( exact, -1, &a );
        CHECK( a.status == XODUS_ERROR && a.err != ENAMETOOLONG );
    }
}

static void test_default_path_connect( void )
{
    struct xodus_connect_args n;

#ifdef __APPLE__
    struct xodus_connect_args e, x;

    /* The default is a fixed shared path: only check NULL and "" take the same route as it. */
    do_connect( NULL, -1, &n );
    do_connect( "", -1, &e );
    do_connect( "/tmp/xodus.sock", -1, &x );
    CHECK( n.status == x.status && n.err == x.err );
    CHECK( e.status == x.status && e.err == x.err );
    if (xodus_fd >= 0) do_close();
#else
    struct server s;
    char dir[64], path[128];

    snprintf( dir, sizeof(dir), "/tmp/xodus-ht-%d-xdg", (int)getpid() );
    mkdir( dir, 0700 );
    snprintf( path, sizeof(path), "%s/xodus.sock", dir );
    setenv( "XDG_RUNTIME_DIR", dir, 1 );
    for (int i = 0; i < 2; i++)
    {
        struct sockaddr_un addr;
        s.listen_fd = socket( AF_UNIX, SOCK_STREAM, 0 );
        memset( &addr, 0, sizeof(addr) );
        addr.sun_family = AF_UNIX;
        strcpy( addr.sun_path, path );
        unlink( path );
        CHECK( bind( s.listen_fd, (struct sockaddr *)&addr, sizeof(addr) ) == 0 && listen( s.listen_fd, 1 ) == 0 );
        s.mode = SRV_CLOSE;
        server_run( &s );
        do_connect( i ? "" : NULL, -1, &n );
        CHECK( n.status == XODUS_OK );
        do_close();
        pthread_join( s.thread, NULL );
        close( s.listen_fd );
        unlink( path );
    }
    unsetenv( "XDG_RUNTIME_DIR" );
    do_connect( NULL, -1, &n );
    CHECK( n.status == XODUS_ERROR );
    setenv( "XDG_RUNTIME_DIR", "relative", 1 );
    do_connect( "", -1, &n );
    CHECK( n.status == XODUS_ERROR );
    unsetenv( "XDG_RUNTIME_DIR" );
    rmdir( dir );
#endif
}

static volatile sig_atomic_t sigpipe_count;
static void on_sigpipe( int sig ) { (void)sig; sigpipe_count++; }

static void test_closed_peer_no_sigpipe( void )
{
    struct server s;
    struct xodus_send_args snd = {0};
    uint8_t buf[4096] = {0};
    sigset_t set;

    /* Count SIGPIPE deliveries (unblocked, in case the parent blocked it): none may arrive. */
    install_handler( SIGPIPE, on_sigpipe );
    sigemptyset( &set );
    sigaddset( &set, SIGPIPE );
    pthread_sigmask( SIG_UNBLOCK, &set, NULL );
    sigpipe_count = 0;

    server_up( &s, "peer", SRV_CLOSE, 0, NULL, 0 );
    connect_ok( &s );
    pthread_join( s.thread, NULL ); /* peer has closed */

    for (int i = 0; i < 20; i++)
    {
        snd.buf = buf;
        snd.len = sizeof(buf);
        snd.timeout_ms = 1000;
        __wine_unix_call_funcs[XODUS_CALL_SEND]( &snd );
        if (snd.status != XODUS_OK) break;
        usleep( 10000 );
    }
    CHECK( sigpipe_count == 0 );
    CHECK( snd.status == XODUS_PEER_CLOSED );
    do_close();
    close( s.listen_fd );
    unlink( s.path );
}

static volatile sig_atomic_t usr1_count;
static void on_usr1( int sig ) { (void)sig; usr1_count++; }

struct kicker { pthread_t target; unsigned delay_ms; };
static void *kicker_main( void *p )
{
    struct kicker *k = p;
    usleep( k->delay_ms * 1000 );
    pthread_kill( k->target, SIGUSR1 );
    return NULL;
}

static void test_recv_eintr( void )
{
    static const uint8_t frame[8] = { 0x58, 0x53, 0x44, 0x58, 2, 0, 0, 0 };
    struct server s;
    struct xodus_recv_args r;
    struct kicker k;
    pthread_t kt;
    uint8_t buf[64];

    install_handler( SIGUSR1, on_usr1 );
    server_up( &s, "eintr", SRV_SEND_LATE, 400, frame, sizeof(frame) );
    connect_ok( &s );

    k.target = pthread_self();
    k.delay_ms = 100;
    pthread_create( &kt, NULL, kicker_main, &k );
    memset( &r, 0, sizeof(r) );
    r.buf = buf;
    r.cap = sizeof(buf);
    r.timeout_ms = 5000;
    __wine_unix_call_funcs[XODUS_CALL_RECV]( &r );
    pthread_join( kt, NULL );
    CHECK( usr1_count >= 1 );
    CHECK( r.status == XODUS_OK && r.got == sizeof(frame) && !memcmp( buf, frame, sizeof(frame) ) );

    do_close();
    server_stop( &s );
}

static void test_connect_completion_never_reconnects( void )
{
    static const enum wrap_mode modes[] = { WRAP_CONNECT_INPROGRESS, WRAP_CONNECT_EINTR };
    struct server s;
    struct xodus_connect_args a;

    for (int i = 0; i < 2; i++)
    {
        server_up( &s, "inprog", SRV_CLOSE, 0, NULL, 0 );
        wrap_mode = modes[i];
        connect_calls = 0;
        do_connect( s.path, -1, &a );
        wrap_mode = WRAP_PASS;
        CHECK( a.status == XODUS_OK );
        CHECK( connect_calls == 1 );
        do_close();
        server_stop( &s );
    }

    /* an error completion is reported via SO_ERROR, still one connect() */
    wrap_mode = WRAP_CONNECT_INPROGRESS;
    connect_calls = 0;
    do_connect( "/tmp/xodus-ht-nonexistent.sock", -1, &a );
    wrap_mode = WRAP_PASS;
    CHECK( a.status == XODUS_ERROR && connect_calls == 1 );
    CHECK( a.err == ENOENT ); /* the SO_ERROR value, not a later peer-uid failure */

    /* a connect that never completes times out within the deadline, still one connect() */
    wrap_mode = WRAP_CONNECT_STUCK;
    connect_calls = 0;
    do_connect( "/tmp/xodus-ht-nonexistent.sock", -1, &a );
    wrap_mode = WRAP_PASS;
    CHECK( a.status == XODUS_TIMEOUT && connect_calls == 1 );
}

static void test_no_fd_leaks( void )
{
    struct server s, s2;
    int base = count_open_fds();

    /* a second connect closes the first connection */
    server_up( &s, "leak2", SRV_CLOSE, 0, NULL, 0 );
    server_up( &s2, "leak3", SRV_CLOSE, 0, NULL, 0 );
    connect_ok( &s );
    connect_ok( &s2 );
    do_close();
    server_stop( &s );
    server_stop( &s2 );
    CHECK( count_open_fds() == base );
}

/* Every failed connect (no socket file, refused, SO_ERROR completion, deadline) leaves no open fd. */
static void test_failed_connect_no_fd_leak( void )
{
    static const char *stale = "/tmp/xodus-ht-stale.sock";
    struct xodus_connect_args a;
    struct sockaddr_un addr;
    int base, fd;

    unlink( stale );
    fd = socket( AF_UNIX, SOCK_STREAM, 0 );
    memset( &addr, 0, sizeof(addr) );
    addr.sun_family = AF_UNIX;
    strcpy( addr.sun_path, stale );
    CHECK( fd >= 0 && bind( fd, (struct sockaddr *)&addr, sizeof(addr) ) == 0 ); /* bound, never listening */
    base = count_open_fds();

    do_connect( "/tmp/xodus-ht-nonexistent.sock", -1, &a );
    CHECK( a.status == XODUS_ERROR && a.err == ENOENT && xodus_fd == -1 );
    CHECK( count_open_fds() == base );

    do_connect( stale, -1, &a );
    CHECK( a.status == XODUS_ERROR && a.err == ECONNREFUSED && xodus_fd == -1 );
    CHECK( count_open_fds() == base );

    wrap_mode = WRAP_CONNECT_INPROGRESS;
    do_connect( "/tmp/xodus-ht-nonexistent.sock", -1, &a );
    CHECK( a.status == XODUS_ERROR && xodus_fd == -1 );
    CHECK( count_open_fds() == base );

    wrap_mode = WRAP_CONNECT_STUCK;
    do_connect( "/tmp/xodus-ht-nonexistent.sock", -1, &a );
    wrap_mode = WRAP_PASS;
    CHECK( a.status == XODUS_TIMEOUT && xodus_fd == -1 );
    CHECK( count_open_fds() == base );

    close( fd );
    unlink( stale );
}

static void test_send_edges( void )
{
    static uint8_t big[512 * 1024];
    struct server s;
    struct xodus_send_args snd;
    struct xodus_recv_args rcv;
    struct timespec t0, t1;
    long ms;
    uint8_t buf[16];

    /* no connection: ENOTCONN, not a send on a stale or invalid fd */
    do_close();
    snd = (struct xodus_send_args){ big, 4, 100, 0, 0 };
    __wine_unix_call_funcs[XODUS_CALL_SEND]( &snd );
    CHECK( snd.status == XODUS_ERROR && snd.err == ENOTCONN );
    rcv = (struct xodus_recv_args){ buf, sizeof(buf), 100, 0, 0, 0 };
    __wine_unix_call_funcs[XODUS_CALL_RECV]( &rcv );
    CHECK( rcv.status == XODUS_ERROR && rcv.err == ENOTCONN );

    /* a peer that does not read: a blocked send returns TIMEOUT within its deadline */
    server_up( &s, "sndto", SRV_SEND_LATE, 1500, big, 1 );
    connect_ok( &s );
    snd = (struct xodus_send_args){ big, sizeof(big), 200, 0, 0 };
    clock_gettime( CLOCK_MONOTONIC, &t0 );
    __wine_unix_call_funcs[XODUS_CALL_SEND]( &snd );
    clock_gettime( CLOCK_MONOTONIC, &t1 );
    ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
    CHECK( snd.status == XODUS_TIMEOUT );
    CHECK( ms >= 150 && ms < 1000 );
    do_close();
    server_stop( &s );

    /* a send larger than the socket buffer is written completely (partial writes continue) */
    for (size_t i = 0; i < sizeof(big); i++) big[i] = (uint8_t)i;
    server_up( &s, "sndall", SRV_READ_SLOW, 0, NULL, 0 );
    connect_ok( &s );
    snd = (struct xodus_send_args){ big, sizeof(big), 10000, 0, 0 };
    __wine_unix_call_funcs[XODUS_CALL_SEND]( &snd );
    CHECK( snd.status == XODUS_OK );
    do_close();
    server_stop( &s );
    CHECK( s.received == sizeof(big) );
}

static void test_syscall_edges( void )
{
    static uint8_t big[0x30000], rbuf[0x30000];
    static const uint8_t frame[8] = { 0x58, 0x53, 0x44, 0x58, 2, 0, 0, 0 };
    struct server s;
    struct xodus_send_args snd;
    struct xodus_recv_args r;

    /* send and recv retry an EINTR from the syscall itself; a recv is capped at the caller's buffer */
    server_up( &s, "eintr2", SRV_SEND_LATE, 100, frame, sizeof(frame) );
    connect_ok( &s );
    eintr_next_send = 1;
    snd = (struct xodus_send_args){ frame, sizeof(frame), 2000, 0, 0 };
    __wine_unix_call_funcs[XODUS_CALL_SEND]( &snd );
    CHECK( eintr_next_send == 0 );
    CHECK( snd.status == XODUS_OK );
    eintr_next_recv = 1;
    r = (struct xodus_recv_args){ rbuf, 4, 3000, 0, 0, 0 };
    __wine_unix_call_funcs[XODUS_CALL_RECV]( &r );
    CHECK( eintr_next_recv == 0 );
    CHECK( r.status == XODUS_OK && r.got > 0 && r.got <= 4 );
    do_close();
    server_stop( &s );

    /* a peer-credential lookup that fails closes the connection and fails the connect */
    expect_denied( "cred", -1, 1 );

    /* one recv returns at most XODUS_RECV_MAX bytes even when the caller offers more */
    memset( big, 0x5a, sizeof(big) );
    server_up( &s, "recvmax", SRV_SEND_LATE, 100, big, sizeof(big) );
    connect_ok( &s );
    setsockopt( xodus_fd, SOL_SOCKET, SO_RCVBUF, &(int){ 1 << 20 }, sizeof(int) );
    usleep( 500 * 1000 ); /* let the whole buffer arrive */
    r = (struct xodus_recv_args){ rbuf, sizeof(rbuf), 3000, 0, 0, 0 };
    __wine_unix_call_funcs[XODUS_CALL_RECV]( &r );
    CHECK( r.status == XODUS_OK && r.got > 0 && r.got <= XODUS_RECV_MAX );
    do_close();
    server_stop( &s );
}

static void test_close_once( void )
{
    struct server s;
    struct xodus_close_args c = {0};
    int before;

    server_up( &s, "close", SRV_CLOSE, 0, NULL, 0 );
    connect_ok( &s );

    wrap_mode = WRAP_CLOSE_EINTR;
    watch_fd = xodus_fd;
    before = close_calls;
    __wine_unix_call_funcs[XODUS_CALL_CLOSE]( &c );
    CHECK( close_calls == before + 1 );
    CHECK( c.status == XODUS_OK );
    CHECK( xodus_fd == -1 );
    __wine_unix_call_funcs[XODUS_CALL_CLOSE]( &c ); /* second close: no second close() */
    CHECK( close_calls == before + 1 );
    wrap_mode = WRAP_PASS;
    watch_fd = -1;
    server_stop( &s );
}

int main( void )
{
    setvbuf( stdout, NULL, _IONBF, 0 );
    signal( SIGPIPE, SIG_DFL );
    test_default_path();
    test_connect_basics();
    test_default_path_connect();
    test_closed_peer_no_sigpipe();
    test_recv_eintr();
    test_connect_completion_never_reconnects();
    test_close_once();
    test_no_fd_leaks();
    test_failed_connect_no_fd_leak();
    test_cloexec();
    test_send_edges();
    test_syscall_edges();
    if (failures) { printf( "%d failure(s)\n", failures ); return 1; }
    printf( "all xodus host tests passed\n" );
    return 0;
}
