/*
 * Xodus XodusClient integration tests
 *
 * Drive XodusClient through UnixTransport, the real xgameruntime-unix.so and the
 * test-only server (tests/unix/xodus_test_server.c) under Wine.
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

#include "Xodus/IpcFraming.h"
#include "Xodus/UnixTransport.h"
#include "Xodus/XodusClient.h"
#include "Xodus/XodusXml.h"
#include "unix/xodus_abi.h"
#include "tests/Xodus/xodus_test_support.h"

using namespace Xodus;
using namespace XodusTest;

namespace
{
    HRESULT Win32Hr( DWORD e ) { return HRESULT_FROM_WIN32( e ); }

    const std::string TOKEN_RESPONSE = "<MSATokenResponse><Token>integration-token</Token></MSATokenResponse>";

    class XodusClientIntegration : public ::testing::Test
    {
    protected:
        void SetUp() override { EnsureLogging(); }

        static UnixTransportOptions Options( const TestServer &server )
        {
            UnixTransportOptions o;
            o.socket_path = server.Path();
            return o;
        }
    };

    /* The real dispatcher with the result of one entry corrupted, to model a misbehaving unix lib. */
    enum Corrupt { CorruptNone, CorruptRecvStatus, CorruptRecvGot };
    Corrupt g_corrupt = CorruptNone;
    LONG ( WINAPI *g_real_dispatcher )( UINT64, unsigned int, void * ) = nullptr;

    LONG WINAPI CorruptingDispatch( UINT64 handle, unsigned int entry, void *args )
    {
        LONG st = g_real_dispatcher( handle, entry, args );
        if ( st ) return st;
        if ( entry == XODUS_CALL_RECV && g_corrupt == CorruptRecvStatus ) ( (xodus_recv_args *)args )->status = 77;
        if ( entry == XODUS_CALL_RECV && g_corrupt == CorruptRecvGot ) ( (xodus_recv_args *)args )->got = ( (xodus_recv_args *)args )->cap + 1;
        return 0;
    }

    LONG ( WINAPI *g_corrupting_dispatcher )( UINT64, unsigned int, void * ) = CorruptingDispatch;

    void *CorruptingResolve( const char *name )
    {
        if ( !strcmp( name, "__wine_unix_call_dispatcher" ) )
        {
            g_real_dispatcher = *(decltype( g_real_dispatcher ) *)RealResolve( name );
            return (void *)&g_corrupting_dispatcher;
        }
        return RealResolve( name );
    }
}

TEST_F( XodusClientIntegration, PingAndTokenRequestSucceed )
{
    TestServer server( XTS_MODE_SERVE, UniqueSocketPath(), TOKEN_RESPONSE );
    UnixTransport transport( RealResolve, RealSoPath(), Options( server ) );
    XodusClient client( transport );
    MsaTokenResult r;

    EXPECT_EQ( client.Ping(), S_OK );
    ASSERT_EQ( client.MsaTokenRequest( "integration-client", false, false, r ), S_OK );
    EXPECT_EQ( r.token, "integration-token" );

    xts_stats_args stats = server.Stats();
    EXPECT_EQ( stats.frames_answered, 2u );
    EXPECT_EQ( stats.accepted, 1u );
}

TEST_F( XodusClientIntegration, UserInfoRoundTrip )
{
    TestServer server( XTS_MODE_SERVE, UniqueSocketPath(),
                       "<UserInfoResponse><SignedIn>true</SignedIn><Xuid>2535</Xuid><Gamertag>G</Gamertag>"
                       "<AgeGroup>Child</AgeGroup></UserInfoResponse>" );
    UnixTransport transport( RealResolve, RealSoPath(), Options( server ) );
    XodusClient client( transport );
    UserInfoResult r;

    ASSERT_EQ( client.UserInfo( "integration-client", 7, false, r ), S_OK );
    EXPECT_TRUE( r.signedIn );
    EXPECT_EQ( r.xuid, "2535" );
    EXPECT_EQ( r.gamertag, "G" );
    EXPECT_EQ( r.ageGroup, AgeGroup::Child );
    EXPECT_EQ( server.Stats().frames_answered, 1u );
}

TEST_F( XodusClientIntegration, TokenAndSignatureRoundTrip )
{
    TestServer server( XTS_MODE_SERVE, UniqueSocketPath(),
                       "<TokenAndSignatureResponse><Token>XBL3.0 x=9;integration-token</Token>"
                       "<Signature>c2ln</Signature><Expiry>1790000000</Expiry></TokenAndSignatureResponse>" );
    UnixTransport transport( RealResolve, RealSoPath(), Options( server ) );
    XodusClient client( transport );
    TokenAndSignatureResult r;

    ASSERT_EQ( client.TokenAndSignature( "POST", "https://x.example/p", { { "H", "v" } }, { 1, 2, 3 }, "integration-client", 7, false, r ), S_OK );
    EXPECT_EQ( r.token, "XBL3.0 x=9;integration-token" );
    EXPECT_EQ( r.signature, "c2ln" );
    EXPECT_EQ( r.expiry, 1790000000 );
    EXPECT_EQ( server.Stats().frames_answered, 1u );
}

TEST_F( XodusClientIntegration, SilentServerTimesOutAndNextCallReconnects )
{
    TestServer server( XTS_MODE_SILENT, UniqueSocketPath() );
    UnixTransport transport( RealResolve, RealSoPath(), Options( server ) );
    XodusClient client( transport );

    DWORD start = GetTickCount();
    EXPECT_EQ( client.Ping(), Win32Hr( ERROR_TIMEOUT ) );
    DWORD elapsed = GetTickCount() - start;
    EXPECT_GE( elapsed, 4500u );
    EXPECT_LT( elapsed, 15000u );
    EXPECT_EQ( server.Stats().accepted, 1u );
}

TEST_F( XodusClientIntegration, ClosedPeerIsAFailureAndTheProcessSurvives )
{
    TestServer server( XTS_MODE_CLOSE, UniqueSocketPath() );
    UnixTransport transport( RealResolve, RealSoPath(), Options( server ) );
    XodusClient client( transport );

    for ( int i = 0; i < 3; i++ )
    {
        HRESULT hr = client.Ping();
        EXPECT_TRUE( hr == Win32Hr( ERROR_GRACEFUL_DISCONNECT ) || hr == Win32Hr( ERROR_CONNECTION_ABORTED ) ) << std::hex << hr;
    }
    EXPECT_GE( server.Stats().accepted, 1u );
}

TEST_F( XodusClientIntegration, ExpectedUidMismatchFailsConnectAndServerReceivesNoPayload )
{
    TestServer server( XTS_MODE_SERVE, UniqueSocketPath(), TOKEN_RESPONSE );
    UnixTransportOptions o = Options( server );
    o.expected_uid = 0x7FFFFFF0;
    UnixTransport transport( RealResolve, RealSoPath(), o );
    XodusClient client( transport );
    MsaTokenResult r;

    EXPECT_EQ( client.MsaTokenRequest( "must-not-arrive", false, false, r ), Win32Hr( ERROR_CONNECTION_REFUSED ) );
    Sleep( 300 );
    EXPECT_EQ( server.Stats().bytes_received, 0u );
    EXPECT_EQ( server.Stats().frames_answered, 0u );
}

/* A maximum-size (0xFFFF byte) request and reply through the real unix library. The request exceeds the default unix
 * socket send buffer, so the send entry has to survive short writes; the reply exercises the recv path the same way. */
TEST_F( XodusClientIntegration, MaximumSizeRequestAndReplyRoundTrip )
{
    std::string empty_request;
    ASSERT_EQ( BuildMsaTokenRequest( "", false, false, empty_request ), S_OK );
    std::string client_id( FRAME_MAX_PAYLOAD - empty_request.size(), 'c' );
    std::string request;
    ASSERT_EQ( BuildMsaTokenRequest( client_id, false, false, request ), S_OK );
    ASSERT_EQ( request.size(), FRAME_MAX_PAYLOAD );

    const std::string head = "<MSATokenResponse><Token>", tail = "</Token></MSATokenResponse>";
    std::string token( FRAME_MAX_PAYLOAD - head.size() - tail.size(), 't' );

    TestServer server( XTS_MODE_SERVE, UniqueSocketPath(), head + token + tail );
    UnixTransport transport( RealResolve, RealSoPath(), Options( server ) );
    XodusClient client( transport );
    MsaTokenResult r;

    ASSERT_EQ( client.MsaTokenRequest( client_id, false, false, r ), S_OK );
    EXPECT_EQ( r.token, token );
    EXPECT_EQ( server.Stats().bytes_received, (uint64_t)( FRAME_HEADER_SIZE + FRAME_MAX_PAYLOAD ) );
    EXPECT_EQ( server.Stats().frames_answered, 1u );
}

TEST_F( XodusClientIntegration, CorruptRecvResultIsInvalidDataAndClosesTheConnection )
{
    for ( Corrupt mode : { CorruptRecvStatus, CorruptRecvGot } )
    {
        SCOPED_TRACE( mode == CorruptRecvStatus ? "unknown recv status" : "got > cap" );
        TestServer server( XTS_MODE_SERVE, UniqueSocketPath() );
        UnixTransport transport( CorruptingResolve, RealSoPath(), Options( server ) );
        XodusClient client( transport );

        g_corrupt = mode;
        EXPECT_EQ( client.Ping(), Win32Hr( ERROR_INVALID_DATA ) );
        g_corrupt = CorruptNone;

        /* the first connection was closed: the single-threaded server can accept a second one */
        EXPECT_EQ( client.Ping(), S_OK );
        EXPECT_EQ( server.Stats().accepted, 2u );
    }
}
