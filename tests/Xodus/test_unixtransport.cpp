/*
 * Xodus UnixTransport Tests
 *
 * Fake-dispatcher cases cover the DLL side (symbol resolution, load, status
 * mapping, logging). Real cases load xgameruntime-unix.so and the test server
 * under Wine and drive UnixTransport directly (no XodusClient).
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

#include <windows.h>
#include <private/logging.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Xodus/IpcFraming.h"
#include "Xodus/UnixTransport.h"
#include "unix/xodus_abi.h"
#include "tests/Xodus/xodus_test_support.h"

using namespace Xodus;
using namespace XodusTest;

namespace
{
    HRESULT Win32Hr( DWORD e ) { return HRESULT_FROM_WIN32( e ); }

    const LONG NT_DLL_NOT_FOUND = (LONG)0xC0000135;
    const LONG NT_ENTRYPOINT_NOT_FOUND = (LONG)0xC0000139;

    /* ---- fake unix lib: resolver, NtQueryVirtualMemory and dispatcher doubles ---- */

    struct Fake
    {
        LONG load_status = 0;
        int load_class = 0;
        std::wstring load_name;
        LONG nt_status[4] = {};
        int32_t status[4] = {};
        int32_t err[4] = {};
        uint32_t got = 0;
        std::vector<unsigned> calls;
        xodus_connect_args connect = {};
        xodus_send_args send = {};
        xodus_recv_args recv = {};

        void Reset() { *this = Fake(); }
        int Count( unsigned entry ) const { return (int)std::count( calls.begin(), calls.end(), entry ); }
    } g_fake;

    LONG WINAPI FakeQuery( HANDLE, const void *name, int cls, void *out, SIZE_T, SIZE_T * )
    {
        struct S { USHORT len, max; PWSTR buf; };
        const S *s = (const S *)name;
        g_fake.load_class = cls;
        g_fake.load_name.assign( s->buf, s->len / sizeof( wchar_t ) );
        UINT64 *res = (UINT64 *)out;
        res[0] = 1;
        res[1] = 0x1234;
        return g_fake.load_status;
    }

    LONG WINAPI FakeDispatch( UINT64 handle, unsigned int entry, void *args )
    {
        EXPECT_EQ( handle, 0x1234u );
        g_fake.calls.push_back( entry );
        if ( entry > 3 ) return (LONG)0xC0000001;
        if ( g_fake.nt_status[entry] ) return g_fake.nt_status[entry];

        switch ( entry )
        {
            case XODUS_CALL_CONNECT:
                g_fake.connect = *(xodus_connect_args *)args;
                ((xodus_connect_args *)args)->status = g_fake.status[entry];
                ((xodus_connect_args *)args)->err = g_fake.err[entry];
                break;
            case XODUS_CALL_SEND:
                g_fake.send = *(xodus_send_args *)args;
                ((xodus_send_args *)args)->status = g_fake.status[entry];
                ((xodus_send_args *)args)->err = g_fake.err[entry];
                break;
            case XODUS_CALL_RECV:
                g_fake.recv = *(xodus_recv_args *)args;
                ((xodus_recv_args *)args)->got = g_fake.got;
                ((xodus_recv_args *)args)->status = g_fake.status[entry];
                ((xodus_recv_args *)args)->err = g_fake.err[entry];
                break;
            case XODUS_CALL_CLOSE:
                ((xodus_close_args *)args)->status = 0;
                break;
        }
        return 0;
    }

    LONG ( WINAPI *g_fake_dispatcher )( UINT64, unsigned int, void * ) = FakeDispatch;

    void *FakeResolve( const char *name )
    {
        if ( !strcmp( name, "NtQueryVirtualMemory" ) ) return (void *)FakeQuery;
        if ( !strcmp( name, "__wine_unix_call_dispatcher" ) ) return (void *)&g_fake_dispatcher;
        return nullptr;
    }

    const wchar_t *FAKE_SO = L"C:\\fake\\xgameruntime-unix.so";

    enum Op { OpConnect, OpSend, OpRecv };
    const char *OpName( Op op ) { return op == OpConnect ? "connect" : op == OpSend ? "send" : "recv"; }
    unsigned OpEntry( Op op ) { return op == OpConnect ? XODUS_CALL_CONNECT : op == OpSend ? XODUS_CALL_SEND : XODUS_CALL_RECV; }

    HRESULT RunOp( Transport &t, Op op, size_t cap = 64 )
    {
        static uint8_t buf[0x20000];
        size_t got = 12345;
        switch ( op )
        {
            case OpConnect: return t.Connect( 1000 );
            case OpSend: return t.Send( buf, 16, 1000 );
            default: return t.Recv( buf, cap, got, 1000 );
        }
    }

    class XodusUnixTransportFake : public ::testing::Test
    {
    protected:
        void SetUp() override { EnsureLogging(); g_fake.Reset(); }
    };

    class XodusUnixTransportReal : public ::testing::Test
    {
    protected:
        void SetUp() override { EnsureLogging(); }
    };
}

/* ---------------- fake unix lib ---------------- */

TEST_F( XodusUnixTransportFake, NullResolverIsEntrypointNotFound )
{
    LogMark mark;
    UnixTransport t( []( const char * ) -> void * { return nullptr; }, FAKE_SO );

    EXPECT_EQ( t.Connect( 100 ), HRESULT_FROM_NT( NT_ENTRYPOINT_NOT_FOUND ) );
    EXPECT_NE( mark.Text().find( "NtQueryVirtualMemory not found" ), std::string::npos ) << mark.Text();
}

TEST_F( XodusUnixTransportFake, NullDispatcherValueIsEntrypointNotFound )
{
    {
        LogMark mark;
        UnixTransport t( []( const char *n ) -> void * { return strcmp( n, "NtQueryVirtualMemory" ) ? nullptr : (void *)FakeQuery; }, FAKE_SO );

        EXPECT_EQ( t.Connect( 100 ), HRESULT_FROM_NT( NT_ENTRYPOINT_NOT_FOUND ) );
        EXPECT_NE( mark.Text().find( "__wine_unix_call_dispatcher not found" ), std::string::npos ) << mark.Text();
    }
    LogMark mark;
    static LONG ( WINAPI *null_dispatcher )( UINT64, unsigned int, void * ) = nullptr;
    UnixTransport t( []( const char *n ) -> void * { return strcmp( n, "NtQueryVirtualMemory" ) ? (void *)&null_dispatcher : (void *)FakeQuery; }, FAKE_SO );

    EXPECT_EQ( t.Connect( 100 ), HRESULT_FROM_NT( NT_ENTRYPOINT_NOT_FOUND ) );
    EXPECT_NE( mark.Text().find( "__wine_unix_call_dispatcher not found" ), std::string::npos ) << mark.Text();
}

TEST_F( XodusUnixTransportFake, LoadFailureIsDllNotFoundAndLogsPathAndRemedy )
{
    LogMark mark;
    g_fake.load_status = (LONG)0xC0000034;
    UnixTransport t( FakeResolve, FAKE_SO );

    EXPECT_EQ( t.Connect( 100 ), HRESULT_FROM_NT( NT_DLL_NOT_FOUND ) );
    EXPECT_EQ( t.Send( nullptr, 0, 100 ), HRESULT_FROM_NT( NT_DLL_NOT_FOUND ) );
    EXPECT_TRUE( g_fake.calls.empty() );
    std::string log = mark.Text();
    EXPECT_NE( log.find( "load of C:\\fake\\xgameruntime-unix.so failed" ), std::string::npos ) << log;
    EXPECT_NE( log.find( "remedy" ), std::string::npos ) << log;
}

TEST_F( XodusUnixTransportFake, LoadsByNtPathWithClass1002 )
{
    UnixTransport t( FakeResolve, FAKE_SO );

    ASSERT_EQ( t.Connect( 100 ), S_OK );
    EXPECT_EQ( g_fake.load_class, 1002 );
    EXPECT_EQ( g_fake.load_name, L"\\??\\C:\\fake\\xgameruntime-unix.so" );
    EXPECT_EQ( g_fake.connect.path, nullptr );
    EXPECT_EQ( g_fake.connect.expected_uid, -1 );
}

TEST_F( XodusUnixTransportFake, NonZeroNtStatusIsReturnedPerOperationAndLogged )
{
    for ( Op op : { OpConnect, OpSend, OpRecv } )
    {
        SCOPED_TRACE( OpName( op ) );
        g_fake.Reset();
        g_fake.nt_status[OpEntry( op )] = (LONG)0xC00000BB;
        LogMark mark;
        UnixTransport t( FakeResolve, FAKE_SO );

        EXPECT_EQ( RunOp( t, op ), HRESULT_FROM_NT( (LONG)0xC00000BB ) );
        std::string log = mark.Text();
        EXPECT_NE( log.find( std::string( "unix call " ) + OpName( op ) + " failed" ), std::string::npos ) << log;
        EXPECT_NE( log.find( "0xc00000bb" ), std::string::npos ) << log;
    }
}

TEST_F( XodusUnixTransportFake, StatusMapping )
{
    struct Case { int32_t status; int32_t err; HRESULT hr[3]; const char *log; };
    const Case cases[] =
    {
        { XODUS_OK, 0, { S_OK, S_OK, S_OK }, nullptr },
        { XODUS_TIMEOUT, 0, { Win32Hr( ERROR_TIMEOUT ), Win32Hr( ERROR_TIMEOUT ), Win32Hr( ERROR_TIMEOUT ) }, "timed out" },
        { XODUS_PEER_CLOSED, 0, { Win32Hr( ERROR_GRACEFUL_DISCONNECT ), Win32Hr( ERROR_GRACEFUL_DISCONNECT ), Win32Hr( ERROR_GRACEFUL_DISCONNECT ) }, "peer closed" },
        { XODUS_ERROR, 111, { Win32Hr( ERROR_CONNECTION_REFUSED ), Win32Hr( ERROR_CONNECTION_ABORTED ), Win32Hr( ERROR_CONNECTION_ABORTED ) }, "errno 111" },
    };

    for ( const Case &c : cases )
        for ( Op op : { OpConnect, OpSend, OpRecv } )
        {
            SCOPED_TRACE( std::string( OpName( op ) ) + " status " + std::to_string( c.status ) );
            g_fake.Reset();
            g_fake.status[OpEntry( op )] = c.status;
            g_fake.err[OpEntry( op )] = c.err;
            LogMark mark;
            UnixTransport t( FakeResolve, FAKE_SO );

            EXPECT_EQ( RunOp( t, op ), c.hr[op] );
            if ( c.log ) EXPECT_NE( mark.Text().find( c.log ), std::string::npos ) << mark.Text();
            EXPECT_EQ( g_fake.Count( XODUS_CALL_CLOSE ), 0 ) << "a plain failure does not close inside UnixTransport";
        }
}

TEST_F( XodusUnixTransportFake, ConnectFailureLogNamesPathAndRemedy )
{
    g_fake.status[XODUS_CALL_CONNECT] = XODUS_ERROR;
    UnixTransportOptions o;
    o.socket_path = "/tmp/some-test.sock";
    LogMark mark;
    UnixTransport t( FakeResolve, FAKE_SO, o );

    EXPECT_EQ( t.Connect( 100 ), Win32Hr( ERROR_CONNECTION_REFUSED ) );
    std::string log = mark.Text();
    EXPECT_NE( log.find( "connect to /tmp/some-test.sock failed" ), std::string::npos ) << log;
    EXPECT_NE( log.find( "start the xodus service" ), std::string::npos ) << log;
}

TEST_F( XodusUnixTransportFake, UnknownStatusIsInvalidDataAndCloses )
{
    for ( Op op : { OpConnect, OpSend, OpRecv } )
        for ( int32_t bad : { 99 } )
        {
            SCOPED_TRACE( std::string( OpName( op ) ) + " status " + std::to_string( bad ) );
            g_fake.Reset();
            g_fake.status[OpEntry( op )] = bad;
            LogMark mark;
            UnixTransport t( FakeResolve, FAKE_SO );

            EXPECT_EQ( RunOp( t, op ), Win32Hr( ERROR_INVALID_DATA ) );
            EXPECT_EQ( g_fake.Count( XODUS_CALL_CLOSE ), 1 );
            EXPECT_NE( mark.Text().find( "invalid unix-call result" ), std::string::npos ) << mark.Text();
        }
}

TEST_F( XodusUnixTransportFake, RecvGotGreaterThanCapIsInvalidDataAndCloses )
{
    g_fake.status[XODUS_CALL_RECV] = XODUS_OK;
    g_fake.got = 65;
    LogMark mark;
    UnixTransport t( FakeResolve, FAKE_SO );
    uint8_t buf[64];
    size_t got = 7;

    EXPECT_EQ( t.Recv( buf, sizeof( buf ), got, 100 ), Win32Hr( ERROR_INVALID_DATA ) );
    EXPECT_EQ( got, 0u );
    EXPECT_EQ( g_fake.Count( XODUS_CALL_CLOSE ), 1 );
    EXPECT_NE( mark.Text().find( "invalid unix-call result" ), std::string::npos ) << mark.Text();

    g_fake.Reset();
    g_fake.status[XODUS_CALL_RECV] = XODUS_OK;
    g_fake.got = 64;
    UnixTransport t2( FakeResolve, FAKE_SO );
    EXPECT_EQ( t2.Recv( buf, sizeof( buf ), got, 100 ), S_OK );
    EXPECT_EQ( got, 64u );
    EXPECT_EQ( g_fake.Count( XODUS_CALL_CLOSE ), 0 );
}

TEST_F( XodusUnixTransportFake, RecvCapIsLimitedToOneMaximumFrame )
{
    static uint8_t buf[0x20000];
    size_t got;
    UnixTransport t( FakeResolve, FAKE_SO );

    ASSERT_EQ( t.Recv( buf, sizeof( buf ), got, 100 ), S_OK );
    EXPECT_EQ( g_fake.recv.cap, 0x10007u );
    ASSERT_EQ( t.Recv( buf, 100, got, 100 ), S_OK );
    EXPECT_EQ( g_fake.recv.cap, 100u );
}

TEST_F( XodusUnixTransportFake, ArgumentsReachTheUnixCalls )
{
    UnixTransportOptions o;
    o.socket_path = "/tmp/arg-test.sock";
    o.expected_uid = 4242;
    UnixTransport t( FakeResolve, FAKE_SO, o );
    uint8_t out[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    ASSERT_EQ( t.Connect( 777 ), S_OK );
    EXPECT_STREQ( g_fake.connect.path, "/tmp/arg-test.sock" );
    EXPECT_EQ( g_fake.connect.timeout_ms, 777u );
    EXPECT_EQ( g_fake.connect.expected_uid, 4242 );

    ASSERT_EQ( t.Send( out, sizeof( out ), 555 ), S_OK );
    EXPECT_EQ( g_fake.send.buf, out );
    EXPECT_EQ( g_fake.send.len, 8u );
    EXPECT_EQ( g_fake.send.timeout_ms, 555u );

    uint8_t in[16];
    size_t got;
    g_fake.got = 3;
    ASSERT_EQ( t.Recv( in, sizeof( in ), got, 333 ), S_OK );
    EXPECT_EQ( g_fake.recv.buf, in );
    EXPECT_EQ( g_fake.recv.timeout_ms, 333u );
    EXPECT_EQ( got, 3u );

    EXPECT_EQ( g_fake.calls, ( std::vector<unsigned>{ 0, 1, 2 } ) );
}

TEST_F( XodusUnixTransportFake, CloseCallsTheCloseEntryAndDestructorCloses )
{
    {
        UnixTransport t( FakeResolve, FAKE_SO );
        ASSERT_EQ( t.Connect( 100 ), S_OK );
        t.Close();
        EXPECT_EQ( g_fake.Count( XODUS_CALL_CLOSE ), 1 );
    }
    EXPECT_EQ( g_fake.Count( XODUS_CALL_CLOSE ), 2 );
}

/* ---------------- real xgameruntime-unix.so and test server under Wine ---------------- */

TEST_F( XodusUnixTransportReal, PingPongThroughTheRealLibrary )
{
    TestServer server( XTS_MODE_SERVE, UniqueSocketPath() );
    UnixTransportOptions o;
    o.socket_path = server.Path();
    UnixTransport t( RealResolve, RealSoPath(), o );

    ASSERT_EQ( t.Connect( 2000 ), S_OK );
    const uint8_t body[] = { 9, 8, 7 };
    auto ping = Encode( FRAME_PING, body, sizeof( body ) );
    ASSERT_EQ( t.Send( ping.data(), ping.size(), 2000 ), S_OK );

    uint8_t buf[256];
    size_t got = 0;
    FrameDecoder d;
    Frame f;
    HRESULT next;
    for ( int i = 0; ( next = d.Next( f ) ) == S_FALSE && i < 10; i++ )
    {
        ASSERT_EQ( t.Recv( buf, sizeof( buf ), got, 2000 ), S_OK );
        d.Feed( buf, got );
    }
    ASSERT_EQ( next, S_OK );
    EXPECT_EQ( f.type, FRAME_PONG );
    EXPECT_EQ( f.payload, std::vector<uint8_t>( body, body + sizeof( body ) ) );
    EXPECT_EQ( server.Stats().frames_answered, 1u );
}

TEST_F( XodusUnixTransportReal, MismatchedExpectedUidFailsConnectAndServerSeesNoPayload )
{
    TestServer server( XTS_MODE_SERVE, UniqueSocketPath() );
    UnixTransportOptions o;
    o.socket_path = server.Path();
    o.expected_uid = 0x7FFFFFF0; /* no real user */
    UnixTransport t( RealResolve, RealSoPath(), o );

    EXPECT_EQ( t.Connect( 2000 ), Win32Hr( ERROR_CONNECTION_REFUSED ) );

    /* nothing can be sent on the failed connection */
    uint8_t b = 0;
    EXPECT_NE( t.Send( &b, 1, 500 ), S_OK );
    Sleep( 200 );
    EXPECT_EQ( server.Stats().bytes_received, 0u );
}

TEST_F( XodusUnixTransportReal, ClosedPeerGivesFailureAndNoSigpipe )
{
    TestServer server( XTS_MODE_CLOSE, UniqueSocketPath() );
    UnixTransportOptions o;
    o.socket_path = server.Path();
    UnixTransport t( RealResolve, RealSoPath(), o );

    ASSERT_EQ( t.Connect( 2000 ), S_OK );

    uint8_t buf[256];
    size_t got = 0;
    EXPECT_EQ( t.Recv( buf, sizeof( buf ), got, 2000 ), Win32Hr( ERROR_GRACEFUL_DISCONNECT ) );

    /* sends to the closed peer fail; a SIGPIPE would have killed this process */
    std::vector<uint8_t> big( 4096, 0x55 );
    HRESULT hr = S_OK;
    for ( int i = 0; i < 20 && SUCCEEDED( hr ); i++ )
    {
        hr = t.Send( big.data(), big.size(), 1000 );
        Sleep( 10 );
    }
    EXPECT_TRUE( FAILED( hr ) );
    EXPECT_TRUE( hr == Win32Hr( ERROR_GRACEFUL_DISCONNECT ) || hr == Win32Hr( ERROR_CONNECTION_ABORTED ) ) << std::hex << hr;
}
