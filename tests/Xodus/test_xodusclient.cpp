/*
 * Xodus XodusClient Tests (fake transport)
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

#include <algorithm>
#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Xodus/IpcFraming.h"
#include "Xodus/XodusClient.h"
#include "tests/Xodus/xodus_test_support.h"

using namespace Xodus;
using namespace XodusTest;

namespace
{
    HRESULT Win32Hr( DWORD e ) { return HRESULT_FROM_WIN32( e ); }
    const HRESULT INVALID_DATA = HRESULT_FROM_WIN32( ERROR_INVALID_DATA );

    using Bytes = std::vector<uint8_t>;

    Bytes Pong() { return Encode( FRAME_PONG, nullptr, 0 ); }

    Bytes RawTokenFrame( const std::string &xml )
    {
        return Encode( FRAME_MSA_TOKEN_RESPONSE, (const uint8_t *)xml.data(), xml.size() );
    }

    Bytes TokenFrame( const std::string &token )
    {
        return RawTokenFrame( "<MSATokenResponse><Token>" + token + "</Token></MSATokenResponse>" );
    }

    Bytes Cat( Bytes a, const Bytes &b )
    {
        a.insert( a.end(), b.begin(), b.end() );
        return a;
    }

    /* One scripted Recv: returns hr, or delivers bytes; advances the fake clock by `advance` ms first. */
    struct Step
    {
        HRESULT hr = S_OK;
        Bytes bytes;
        uint64_t advance = 0;
        size_t claim = (size_t)-1;              /* report this many bytes instead of the real count */
        bool stale = false;                     /* bytes still in flight on the connection: Close() discards them */
    };

    class FakeTransport : public Transport
    {
    public:
        HRESULT connect_hr = S_OK;
        HRESULT send_hr = S_OK;
        uint64_t connect_advance = 0;
        uint64_t send_advance = 0;
        bool auto_pong = false;                 /* every Send queues a PONG */

        std::atomic<uint64_t> now{ 1000 };
        std::deque<Step> script;                /* consumed by Recv; persists across Close */

        int connects = 0, closes = 0, sends = 0, recvs = 0;
        std::vector<uint32_t> connect_timeouts, send_timeouts, recv_timeouts;
        std::vector<size_t> recv_caps;
        std::atomic<int> closes_inflight{ 0 };  /* Close() calls that arrived while a Send was running */
        Bytes last_send;
        std::atomic<int> inflight{ 0 };
        std::atomic<int> max_inflight{ 0 };
        std::chrono::milliseconds hold{ 0 };    /* sleep inside Send to let a second caller in */

        Clock Clk() { return [this]() -> uint64_t { return now.load(); }; }

        HRESULT Connect( uint32_t timeout_ms ) override
        {
            std::lock_guard<std::mutex> l( m );
            connects++;
            connect_timeouts.push_back( timeout_ms );
            now += connect_advance;
            return connect_hr;
        }

        HRESULT Send( const uint8_t *p, size_t n, uint32_t timeout_ms ) override
        {
            Enter();
            {
                std::lock_guard<std::mutex> l( m );
                sends++;
                send_timeouts.push_back( timeout_ms );
                last_send.assign( p, p + n );
                now += send_advance;
                if ( auto_pong && SUCCEEDED( send_hr ) ) script.push_back( Step{ S_OK, Pong(), 0 } );
            }
            if ( hold.count() ) std::this_thread::sleep_for( hold );
            Leave();
            return send_hr;
        }

        HRESULT Recv( uint8_t *buf, size_t cap, size_t &got, uint32_t timeout_ms ) override
        {
            std::lock_guard<std::mutex> l( m );
            recvs++;
            recv_timeouts.push_back( timeout_ms );
            recv_caps.push_back( cap );
            got = 0;
            if ( script.empty() ) return Win32Hr( ERROR_TIMEOUT );

            Step s = script.front();
            script.pop_front();
            now += s.advance;
            if ( FAILED( s.hr ) ) return s.hr;

            size_t n = std::min( cap, s.bytes.size() );
            memcpy( buf, s.bytes.data(), n );
            if ( n < s.bytes.size() ) script.push_front( Step{ S_OK, Bytes( s.bytes.begin() + n, s.bytes.end() ), 0 } );
            got = s.claim != (size_t)-1 ? s.claim : n;
            return S_OK;
        }

        void Close() override
        {
            std::lock_guard<std::mutex> l( m );
            closes++;
            if ( inflight.load() > 0 ) closes_inflight++;
            for ( auto it = script.begin(); it != script.end(); )
                it = it->stale ? script.erase( it ) : it + 1;
        }

    private:
        void Enter()
        {
            int v = ++inflight;
            int prev = max_inflight.load();
            while ( v > prev && !max_inflight.compare_exchange_weak( prev, v ) ) {}
        }
        void Leave() { --inflight; }

        std::mutex m;
    };

    class XodusClientFake : public ::testing::Test
    {
    protected:
        void SetUp() override { EnsureLogging(); }

        FakeTransport t;
        XodusClient c{ t, t.Clk() };

        void Reply( Bytes b, uint64_t advance = 0 ) { t.script.push_back( { S_OK, std::move( b ), advance } ); }

        /* The scripted failure of one Ping: expected hr, connection closed, next Ping reconnects and succeeds. */
        void PingFailsThenRecovers( HRESULT expected )
        {
            EXPECT_EQ( c.Ping(), expected );
            EXPECT_EQ( t.closes, 1 );

            t.send_hr = S_OK;
            Reply( Pong() );
            EXPECT_EQ( c.Ping(), S_OK );
            EXPECT_EQ( t.connects, 2 );
        }
    };
}

TEST_F( XodusClientFake, PingSucceedsAndSendsAPingFrameAndReusesTheConnection )
{
    t.auto_pong = true;

    EXPECT_EQ( c.Ping(), S_OK );
    EXPECT_EQ( t.last_send, Encode( FRAME_PING, nullptr, 0 ) );
    EXPECT_EQ( c.Ping(), S_OK );
    EXPECT_EQ( t.connects, 1 );
    EXPECT_EQ( t.closes, 0 );
}

TEST_F( XodusClientFake, TokenRequestSucceeds )
{
    Reply( TokenFrame( "tok123" ) );
    MsaTokenResult r;

    ASSERT_EQ( c.MsaTokenRequest( "client-1", false, true, r ), S_OK );
    EXPECT_EQ( r.token, "tok123" );

    std::string xml;
    ASSERT_EQ( BuildMsaTokenRequest( "client-1", false, true, xml ), S_OK );
    EXPECT_EQ( t.last_send, Encode( FRAME_MSA_TOKEN_REQUEST, (const uint8_t *)xml.data(), xml.size() ) );
}

TEST_F( XodusClientFake, ServiceErrorIsEFailWithCode )
{
    Reply( RawTokenFrame( "<MSATokenResponse><Error>0x8004100c</Error><Debug>SENTINELDEBUGq7x</Debug></MSATokenResponse>" ) );
    MsaTokenResult r;
    LogMark mark;

    EXPECT_EQ( c.MsaTokenRequest( "x", false, false, r ), E_FAIL );
    EXPECT_EQ( r.errorCode, "0x8004100c" );
    EXPECT_NE( mark.Text().find( "service returned error code 0x8004100c" ), std::string::npos ) << mark.Text();
    EXPECT_EQ( mark.Text().find( "SENTINELDEBUGq7x" ), std::string::npos ) << "only the code is logged, never the payload";
    EXPECT_EQ( t.closes, 1 ) << "every failure closes the connection (design Errors)";
}

TEST_F( XodusClientFake, BadReplyIsInvalidDataAndCloses )
{
    struct Row { const char *name; Bytes reply; bool token; };
    const Row rows[] = {
        { "bad magic", Bytes( 8, 0xEE ), false },
        { "empty recv", Bytes(), false },
        { "trailing partial frame", Cat( Pong(), Bytes{ 0x58 } ), false },
        { "unparsable response", RawTokenFrame( "<MSATokenResponse><Token>abc" ), true },
    };
    for ( const Row &row : rows )
    {
        SCOPED_TRACE( row.name );
        FakeTransport lt;
        lt.script.push_back( { S_OK, row.reply } );
        XodusClient lc( lt, lt.Clk() );
        MsaTokenResult r;

        EXPECT_EQ( row.token ? lc.MsaTokenRequest( "x", false, false, r ) : lc.Ping(), INVALID_DATA );
        EXPECT_EQ( lt.closes, 1 );
    }
}

TEST_F( XodusClientFake, MaxSizeResponseParses )
{
    const std::string pre = "<MSATokenResponse><Token>", post = "</Token></MSATokenResponse>";
    std::string good = pre + std::string( 0xFFFF - pre.size() - post.size(), 'a' ) + post;
    ASSERT_EQ( good.size(), 0xFFFFu );
    Reply( RawTokenFrame( good ) );
    MsaTokenResult r;

    EXPECT_EQ( c.MsaTokenRequest( "x", false, false, r ), S_OK );
    EXPECT_EQ( r.token.size(), good.size() - pre.size() - post.size() );
}

TEST_F( XodusClientFake, OversizedRequestIsInvalidArgAndNothingIsSent )
{
    MsaTokenResult r;

    EXPECT_EQ( c.MsaTokenRequest( std::string( 0x10000, 'a' ), false, false, r ), E_INVALIDARG );
    EXPECT_EQ( t.connects, 0 );
    EXPECT_EQ( t.sends, 0 );
}

TEST_F( XodusClientFake, FrameSplitAcrossRecvsIsAssembled )
{
    Bytes f = TokenFrame( "split" );
    Reply( Bytes( f.begin(), f.begin() + 3 ) );
    Reply( Bytes( f.begin() + 3, f.begin() + 11 ) );
    Reply( Bytes( f.begin() + 11, f.end() ) );
    MsaTokenResult r;

    ASSERT_EQ( c.MsaTokenRequest( "x", false, false, r ), S_OK );
    EXPECT_EQ( r.token, "split" );
}

TEST_F( XodusClientFake, WrongFrameTypeClosesAndNextCallDoesNotSeeIt )
{
    Reply( TokenFrame( "t" ) );      /* answer to a Ping: wrong type */
    LogMark mark;

    EXPECT_EQ( c.Ping(), INVALID_DATA );
    EXPECT_EQ( t.closes, 1 );
    EXPECT_NE( mark.Text().find( "wrong type" ), std::string::npos ) << mark.Text();

    /* the next call reconnects and gets only its own (here: no) reply */
    EXPECT_EQ( c.Ping(), Win32Hr( ERROR_TIMEOUT ) );
    EXPECT_EQ( t.connects, 2 );
}

TEST_F( XodusClientFake, TwoPongsInOneRecvCloseAndSecondIsNotReturnedByNextCall )
{
    Reply( Cat( Pong(), Pong() ) );
    LogMark mark;

    EXPECT_EQ( c.Ping(), INVALID_DATA );
    EXPECT_EQ( t.closes, 1 );
    EXPECT_NE( mark.Text().find( "extra bytes" ), std::string::npos ) << mark.Text();

    /* the next call reconnects: with no reply it times out (never the stale PONG), with one reply it succeeds */
    EXPECT_EQ( c.Ping(), Win32Hr( ERROR_TIMEOUT ) );
    EXPECT_EQ( t.connects, 2 );
    Reply( Pong() );
    EXPECT_EQ( c.Ping(), S_OK );
}

TEST_F( XodusClientFake, CloseTwiceThenDestructionClosesTransportOnce )
{
    Reply( Pong() );
    {
        XodusClient lc( t, t.Clk() );
        ASSERT_EQ( lc.Ping(), S_OK );
        lc.Close();
        lc.Close();
        EXPECT_EQ( t.closes, 1 );
    }
    EXPECT_EQ( t.closes, 1 );
}

TEST_F( XodusClientFake, DestructorClosesAnOpenConnection )
{
    Reply( Pong() );
    {
        XodusClient lc( t, t.Clk() );
        ASSERT_EQ( lc.Ping(), S_OK );
    }
    EXPECT_EQ( t.closes, 1 );
}

TEST_F( XodusClientFake, ConnectFailureIsReturnedAndNextCallRetries )
{
    t.connect_hr = Win32Hr( ERROR_CONNECTION_REFUSED );

    EXPECT_EQ( c.Ping(), Win32Hr( ERROR_CONNECTION_REFUSED ) );
    EXPECT_EQ( t.sends, 0 );

    t.connect_hr = S_OK;
    Reply( Pong() );
    EXPECT_EQ( c.Ping(), S_OK );
    EXPECT_EQ( t.connects, 2 );
}

TEST_F( XodusClientFake, SendFailureIsReturnedAndCloses )
{
    t.send_hr = Win32Hr( ERROR_CONNECTION_ABORTED );
    PingFailsThenRecovers( Win32Hr( ERROR_CONNECTION_ABORTED ) );
}

TEST_F( XodusClientFake, MidFrameDisconnectIsGracefulDisconnectAndCloses )
{
    Bytes f = Pong();
    Reply( Bytes( f.begin(), f.begin() + 5 ) );
    t.script.push_back( { Win32Hr( ERROR_GRACEFUL_DISCONNECT ), Bytes() } );
    PingFailsThenRecovers( Win32Hr( ERROR_GRACEFUL_DISCONNECT ) );
}

TEST_F( XodusClientFake, TimeoutClosesAndAStaleReplyIsNotReturnedByTheNextCall )
{
    /* The reply arrives late: it is still in flight on the old connection when the call times out. */
    t.script.push_back( { Win32Hr( ERROR_TIMEOUT ), Bytes() } );
    Step late;
    late.bytes = Pong();
    late.stale = true;
    t.script.push_back( late );

    EXPECT_EQ( c.Ping(), Win32Hr( ERROR_TIMEOUT ) );
    EXPECT_EQ( t.closes, 1 );

    /* A new connection must not be satisfied by the old connection's late PONG. */
    EXPECT_EQ( c.Ping(), Win32Hr( ERROR_TIMEOUT ) );
    EXPECT_EQ( t.connects, 2 );
}

TEST_F( XodusClientFake, RecvTimeoutSelectionIs5000ExceptAllowUi620000 )
{
    Reply( Pong() );
    ASSERT_EQ( c.Ping(), S_OK );
    EXPECT_EQ( t.recv_timeouts, std::vector<uint32_t>{ 5000 } );
    EXPECT_EQ( t.connect_timeouts, std::vector<uint32_t>{ 5000 } );

    for ( bool allowUi : { false, true } )
    {
        FakeTransport lt;
        lt.script.push_back( { S_OK, TokenFrame( "t" ) } );
        XodusClient lc( lt, lt.Clk() );
        MsaTokenResult r;
        ASSERT_EQ( lc.MsaTokenRequest( "x", allowUi, false, r ), S_OK );
        EXPECT_EQ( lt.recv_timeouts, std::vector<uint32_t>{ allowUi ? 620000u : 5000u } ) << allowUi;
    }
}

TEST_F( XodusClientFake, BlockedSendReturnsTimeoutWithinTheOneDeadline )
{
    t.connect_advance = 3000;                       /* connect used 3 of the 5 s */
    t.send_advance = 2000;                          /* send blocks the rest */
    t.send_hr = Win32Hr( ERROR_TIMEOUT );

    EXPECT_EQ( c.Ping(), Win32Hr( ERROR_TIMEOUT ) );
    EXPECT_EQ( t.send_timeouts, std::vector<uint32_t>{ 2000 } );
    EXPECT_EQ( t.recvs, 0 );
    EXPECT_EQ( t.closes, 1 );
}

TEST_F( XodusClientFake, DeadlineSpentBeforeRecvIsTimeoutWithoutCallingRecv )
{
    t.send_advance = 5000;                          /* send returns S_OK exactly at the deadline */

    EXPECT_EQ( c.Ping(), Win32Hr( ERROR_TIMEOUT ) );
    EXPECT_EQ( t.recvs, 0 );
    EXPECT_EQ( t.closes, 1 );
}

TEST_F( XodusClientFake, SlowDripRecvReturnsTimeoutAndEachRecvGetsTheRemainder )
{
    Bytes f = Pong();
    for ( int i = 0; i < 8; i++ )
        Reply( Bytes( 1, f[i] ), 1500 );
    LogMark mark;

    EXPECT_EQ( c.Ping(), Win32Hr( ERROR_TIMEOUT ) );
    EXPECT_EQ( t.recv_timeouts, ( std::vector<uint32_t>{ 5000, 3500, 2000, 500 } ) );
    EXPECT_EQ( t.closes, 1 );
    EXPECT_NE( mark.Text().find( "request timed out" ), std::string::npos ) << mark.Text();
}

TEST_F( XodusClientFake, ConcurrentCallsAreSerialized )
{
    t.auto_pong = true;
    t.hold = std::chrono::milliseconds( 50 );       /* a second caller would enter Send meanwhile */
    XodusClient rc( t );
    std::atomic<int> ok{ 0 };

    auto work = [&]() { for ( int i = 0; i < 3; i++ ) if ( rc.Ping() == S_OK ) ok++; };
    std::thread a( work ), b( work );
    a.join();
    b.join();

    EXPECT_EQ( ok.load(), 6 );
    EXPECT_EQ( t.max_inflight.load(), 1 );
}

TEST_F( XodusClientFake, ConcurrentTokenRequestsAreSerialized )
{
    for ( int i = 0; i < 6; i++ ) Reply( TokenFrame( "t" ) );
    t.hold = std::chrono::milliseconds( 50 );
    XodusClient rc( t );
    std::atomic<int> ok{ 0 };

    auto work = [&]()
    {
        MsaTokenResult r;
        for ( int i = 0; i < 3; i++ ) if ( rc.MsaTokenRequest( "x", false, false, r ) == S_OK ) ok++;
    };
    std::thread a( work ), b( work );
    a.join();
    b.join();

    EXPECT_EQ( ok.load(), 6 );
    EXPECT_EQ( t.max_inflight.load(), 1 );
}

TEST_F( XodusClientFake, LogsNeverContainPayloadOrTokenText )
{
    const std::string clientSentinel = "SENTINELCLIENTq7x", tokenSentinel = "SENTINELTOKENq7x";
    LogMark mark;

    auto run = [&]( const Bytes &reply )
    {
        FakeTransport lt;
        lt.script.push_back( { S_OK, reply } );
        XodusClient lc( lt, lt.Clk() );
        MsaTokenResult r;
        lc.MsaTokenRequest( clientSentinel, false, false, r );
        lt.script.push_back( { S_OK, Cat( TokenFrame( tokenSentinel ), TokenFrame( tokenSentinel ) ) } );
        lc.MsaTokenRequest( clientSentinel, false, false, r );   /* leftover bytes */
    };
    run( TokenFrame( tokenSentinel ) );
    run( Pong() );                                              /* wrong type */
    run( RawTokenFrame( "<MSATokenResponse><Token>" + tokenSentinel ) );
    run( Bytes( 8, 0xEE ) );
    run( RawTokenFrame( "<MSATokenResponse><Error>SENTINELERRq7x</Error></MSATokenResponse>" ) );

    {
        FakeTransport lt;                                       /* send failure */
        lt.send_hr = Win32Hr( ERROR_CONNECTION_ABORTED );
        XodusClient lc( lt, lt.Clk() );
        MsaTokenResult r;
        lc.MsaTokenRequest( clientSentinel, false, false, r );
    }

    std::string log = mark.Text();
    EXPECT_FALSE( log.empty() ) << "the failure paths must log something";
    EXPECT_EQ( log.find( clientSentinel ), std::string::npos ) << log;
    EXPECT_EQ( log.find( tokenSentinel ), std::string::npos ) << log;
    EXPECT_EQ( log.find( "<MSATokenRe" ), std::string::npos ) << log;
}

TEST_F( XodusClientFake, RequestXmlIsEscaped )
{
    Reply( TokenFrame( "t" ) );
    MsaTokenResult r;

    ASSERT_EQ( c.MsaTokenRequest( "a&b<c>", false, false, r ), S_OK );
    std::string sent( t.last_send.begin() + FRAME_HEADER_SIZE, t.last_send.end() );
    EXPECT_NE( sent.find( "<ClientId>a&amp;b&lt;c&gt;</ClientId>" ), std::string::npos ) << sent;
}

TEST_F( XodusClientFake, RecvBufferIsOneMaximumFrame )
{
    Reply( Pong() );

    ASSERT_EQ( c.Ping(), S_OK );
    ASSERT_EQ( t.recv_caps.size(), 1u );
    EXPECT_EQ( t.recv_caps[0], (size_t)( FRAME_HEADER_SIZE + FRAME_MAX_PAYLOAD ) );
}

TEST_F( XodusClientFake, RecvReportingMoreThanTheBufferIsInvalidDataAndCloses )
{
    Step s{ S_OK, Pong() };
    s.claim = FRAME_HEADER_SIZE + FRAME_MAX_PAYLOAD + 1;
    t.script.push_back( s );
    LogMark mark;

    EXPECT_EQ( c.Ping(), INVALID_DATA );
    EXPECT_EQ( t.closes, 1 );
    EXPECT_NE( mark.Text().find( "oversized recv" ), std::string::npos ) << mark.Text();
}

TEST_F( XodusClientFake, CloseWaitsForTheCallInFlight )
{
    t.auto_pong = true;
    t.hold = std::chrono::milliseconds( 200 );
    XodusClient rc( t );
    HRESULT hr = E_FAIL;

    std::thread a( [&]() { hr = rc.Ping(); } );
    while ( t.inflight.load() == 0 ) std::this_thread::yield();
    rc.Close();                                 /* must block until Ping has finished */
    a.join();

    EXPECT_EQ( hr, S_OK );
    EXPECT_EQ( t.closes_inflight.load(), 0 );
    EXPECT_EQ( t.closes, 1 );
}

/* ---- UserInfo / TokenAndSignature (xodus#216) ---- */

namespace
{
    Bytes UserInfoFrame( const std::string &xml ) { return Encode( FRAME_USER_INFO_RESPONSE, (const uint8_t *)xml.data(), xml.size() ); }
    Bytes TokenSigFrame( const std::string &xml ) { return Encode( FRAME_TOKEN_AND_SIGNATURE_RESPONSE, (const uint8_t *)xml.data(), xml.size() ); }

    const char *USER_SIGNED_IN = "<UserInfoResponse><SignedIn>true</SignedIn><Xuid>2535</Xuid><Gamertag>G</Gamertag>"
                                 "<AgeGroup>Adult</AgeGroup><Uhs>9</Uhs></UserInfoResponse>";
    const char *TOKEN_OK = "<TokenAndSignatureResponse><Token>XBL3.0 x=9;SENTINELTOKENq7x</Token>"
                           "<Signature>SENTINELSIGq7x</Signature><Expiry>1790000000</Expiry><Xuid>2535</Xuid></TokenAndSignatureResponse>";
}

TEST_F( XodusClientFake, UserInfoSucceedsAndSendsType7 )
{
    Reply( UserInfoFrame( USER_SIGNED_IN ) );
    UserInfoResult r;

    ASSERT_EQ( c.UserInfo( "cid", 42, true, r ), S_OK );
    EXPECT_TRUE( r.signedIn );
    EXPECT_EQ( r.xuid, "2535" );
    EXPECT_EQ( r.ageGroup, AgeGroup::Adult );

    std::string xml;
    ASSERT_EQ( BuildUserInfoRequest( "cid", 42, true, xml ), S_OK );
    EXPECT_EQ( t.last_send, Encode( 7, (const uint8_t *)xml.data(), xml.size() ) );
    EXPECT_EQ( t.recv_timeouts, std::vector<uint32_t>{ 5000 } );
    EXPECT_EQ( t.closes, 0 );
}

TEST_F( XodusClientFake, UserInfoSignedOut )
{
    Reply( UserInfoFrame( "<UserInfoResponse><SignedIn>false</SignedIn></UserInfoResponse>" ) );
    UserInfoResult r;

    ASSERT_EQ( c.UserInfo( "", 0, false, r ), S_OK );
    EXPECT_FALSE( r.signedIn );
}

TEST_F( XodusClientFake, TokenAndSignatureSucceedsAndSendsType5 )
{
    Reply( TokenSigFrame( TOKEN_OK ) );
    TokenAndSignatureResult r;
    const std::vector<uint8_t> body = { 'b', 'o', 'd', 'y' };

    ASSERT_EQ( c.TokenAndSignature( "POST", "https://x.example/p", { { "H", "v" } }, body, "cid", 42, false, r ), S_OK );
    EXPECT_EQ( r.token, "XBL3.0 x=9;SENTINELTOKENq7x" );
    EXPECT_EQ( r.signature, "SENTINELSIGq7x" );
    EXPECT_EQ( r.expiry, 1790000000 );
    EXPECT_EQ( r.xuid, "2535" );

    std::string xml;
    ASSERT_EQ( BuildTokenAndSignatureRequest( "POST", "https://x.example/p", { { "H", "v" } }, body, "cid", 42, false, xml ), S_OK );
    EXPECT_EQ( t.last_send, Encode( 5, (const uint8_t *)xml.data(), xml.size() ) );
    EXPECT_EQ( t.recv_timeouts, std::vector<uint32_t>{ 5000 } );
}

TEST_F( XodusClientFake, IdentityServiceErrorIsEFailWithCodeAndCloses )
{
    Reply( UserInfoFrame( "<UserInfoResponse><Error>0x89245102</Error><Message>SENTINELMSGq7x</Message></UserInfoResponse>" ) );
    UserInfoResult u;
    LogMark mark;
    EXPECT_EQ( c.UserInfo( "", 0, false, u ), E_FAIL );
    EXPECT_EQ( u.errorCode, "0x89245102" );
    EXPECT_EQ( t.closes, 1 );

    Reply( TokenSigFrame( "<TokenAndSignatureResponse><Token>SENTINELTOKENq7x</Token><Error>0x89245101</Error></TokenAndSignatureResponse>" ) );
    TokenAndSignatureResult r;
    EXPECT_EQ( c.TokenAndSignature( "", "u", {}, {}, "", 0, false, r ), E_FAIL );
    EXPECT_EQ( r.errorCode, "0x89245101" );
    EXPECT_TRUE( r.token.empty() );
    EXPECT_EQ( t.closes, 2 );
    EXPECT_NE( mark.Text().find( "service returned error code 0x89245101" ), std::string::npos ) << mark.Text();
    EXPECT_EQ( mark.Text().find( "SENTINEL" ), std::string::npos ) << mark.Text();
}

TEST_F( XodusClientFake, IdentityWrongReplyTypeIsInvalidDataAndCloses )
{
    UserInfoResult u;
    TokenAndSignatureResult r;

    Reply( TokenFrame( "t" ) );                 /* an MSA reply to a user info request */
    EXPECT_EQ( c.UserInfo( "", 0, false, u ), INVALID_DATA );
    EXPECT_EQ( t.closes, 1 );

    Reply( UserInfoFrame( USER_SIGNED_IN ) );   /* a user info reply to a token request */
    EXPECT_EQ( c.TokenAndSignature( "", "u", {}, {}, "", 0, false, r ), INVALID_DATA );
    EXPECT_EQ( t.closes, 2 );
    EXPECT_TRUE( r.token.empty() );
}

TEST_F( XodusClientFake, IdentityMalformedReplyIsInvalidDataAndCloses )
{
    UserInfoResult u;
    TokenAndSignatureResult r;

    Reply( UserInfoFrame( "<UserInfoResponse><SignedIn>maybe</SignedIn></UserInfoResponse>" ) );
    EXPECT_EQ( c.UserInfo( "", 0, false, u ), INVALID_DATA );
    EXPECT_EQ( t.closes, 1 );

    Reply( TokenSigFrame( "<TokenAndSignatureResponse><Token>SENTINELTOKENq7x</Token><Expiry>abc</Expiry></TokenAndSignatureResponse>" ) );
    EXPECT_EQ( c.TokenAndSignature( "", "u", {}, {}, "", 0, false, r ), INVALID_DATA );
    EXPECT_EQ( t.closes, 2 );
    EXPECT_TRUE( r.token.empty() );
}

TEST_F( XodusClientFake, IdentityOversizedRequestIsInvalidArgAndNothingIsSent )
{
    UserInfoResult u;
    TokenAndSignatureResult r;

    EXPECT_EQ( c.UserInfo( std::string( 0x10000, 'a' ), 0, false, u ), E_INVALIDARG );
    EXPECT_EQ( c.TokenAndSignature( "", "u", {}, std::vector<uint8_t>( 0xFFFF, 1 ), "", 0, false, r ), E_INVALIDARG );
    EXPECT_EQ( c.TokenAndSignature( "", "", {}, {}, "", 0, false, r ), E_INVALIDARG );
    EXPECT_EQ( t.sends, 0 );
    EXPECT_EQ( t.connects, 0 );
}

TEST_F( XodusClientFake, IdentityTimeoutClosesAndNextCallReconnects )
{
    UserInfoResult u;
    EXPECT_EQ( c.UserInfo( "", 0, false, u ), Win32Hr( ERROR_TIMEOUT ) );   /* empty script: Recv times out */
    EXPECT_EQ( t.closes, 1 );

    Reply( UserInfoFrame( USER_SIGNED_IN ) );
    EXPECT_EQ( c.UserInfo( "", 0, false, u ), S_OK );
    EXPECT_EQ( t.connects, 2 );
}

TEST_F( XodusClientFake, IdentityLogsNeverContainPayloadOrSecrets )
{
    LogMark mark;
    const Bytes replies[] = {
        TokenSigFrame( TOKEN_OK ),
        TokenSigFrame( "<TokenAndSignatureResponse><Token>SENTINELTOKENq7x</Token><Signature>SENTINELSIGq7x</Signature><Error>0x80004005</Error></TokenAndSignatureResponse>" ),
        TokenSigFrame( "<TokenAndSignatureResponse><Token>SENTINELTOKENq7x</Token><Signature>SENTINELSIGq7x</Signature><Expiry>-1</Expiry></TokenAndSignatureResponse>" ),
        TokenFrame( "SENTINELTOKENq7x" ),
    };
    for ( const Bytes &reply : replies )
    {
        FakeTransport lt;
        lt.script.push_back( { S_OK, reply } );
        XodusClient lc( lt, lt.Clk() );
        TokenAndSignatureResult r;
        HRESULT hr = lc.TokenAndSignature( "POST", "https://u.example/SENTINELURLq7x", { { "H", "SENTINELHDRq7x" } },
                                           { 'S', 'E', 'N', 'T', 'I', 'N', 'E', 'L', 'B', 'O', 'D', 'Y' }, "SENTINELCLIENTq7x", 1, false, r );
        if ( FAILED( hr ) )
        {
            EXPECT_TRUE( r.token.empty() );
            EXPECT_TRUE( r.signature.empty() );
        }
    }
    const Bytes userReplies[] = {
        UserInfoFrame( "<UserInfoResponse><SignedIn>true</SignedIn><Xuid>SENTINELXUIDq7x</Xuid><Gamertag>SENTINELTAGq7x</Gamertag></UserInfoResponse>" ),
        UserInfoFrame( "<UserInfoResponse><Error>0x89245102</Error><Message>SENTINELMSGq7x</Message></UserInfoResponse>" ),
        UserInfoFrame( "<UserInfoResponse><SignedIn>maybe</SignedIn><Xuid>SENTINELXUIDq7x</Xuid></UserInfoResponse>" ),
        UserInfoFrame( "<UserInfoResponse><SignedIn>true</SignedIn><Xuid>SENTINELXUIDq7x</Xuid><Error>SENTINELERRq7x</Error></UserInfoResponse>" ),
        TokenFrame( "SENTINELTOKENq7x" ),
    };
    for ( const Bytes &reply : userReplies )
    {
        FakeTransport lt;
        lt.script.push_back( { S_OK, reply } );
        XodusClient lc( lt, lt.Clk() );
        UserInfoResult u;
        HRESULT hr = lc.UserInfo( "SENTINELCLIENTq7x", 1, false, u );
        if ( FAILED( hr ) )
        {
            EXPECT_TRUE( u.xuid.empty() );
            EXPECT_TRUE( u.gamertag.empty() );
            EXPECT_TRUE( u.message.empty() || hr == E_FAIL );
        }
    }
    {
        FakeTransport lt;
        lt.send_hr = Win32Hr( ERROR_CONNECTION_ABORTED );
        XodusClient lc( lt, lt.Clk() );
        UserInfoResult u;
        lc.UserInfo( "SENTINELCLIENTq7x", 1, false, u );
    }

    std::string log = mark.Text();
    EXPECT_FALSE( log.empty() ) << "the failure paths must log something";
    for ( const char *s : { "SENTINEL", "<TokenAndSignature", "<UserInfo", "XBL3.0" } )
        EXPECT_EQ( log.find( s ), std::string::npos ) << s << "\n" << log;
}

TEST_F( XodusClientFake, ConcurrentIdentityRequestsAreSerialized )
{
    t.hold = std::chrono::milliseconds( 50 );
    XodusClient rc( t );

    /* Replies are consumed in order, so each call type is exercised from two threads on its own. */
    for ( int i = 0; i < 6; i++ ) Reply( UserInfoFrame( USER_SIGNED_IN ) );
    std::atomic<int> okUser{ 0 };
    auto userWork = [&]()
    {
        UserInfoResult r;
        for ( int i = 0; i < 3; i++ ) if ( rc.UserInfo( "", 0, false, r ) == S_OK ) okUser++;
    };
    std::thread a( userWork ), b( userWork );
    a.join();
    b.join();
    EXPECT_EQ( okUser.load(), 6 );
    EXPECT_EQ( t.max_inflight.load(), 1 );

    for ( int i = 0; i < 6; i++ ) Reply( TokenSigFrame( TOKEN_OK ) );
    std::atomic<int> okTok{ 0 };
    auto tokWork = [&]()
    {
        TokenAndSignatureResult r;
        for ( int i = 0; i < 3; i++ ) if ( rc.TokenAndSignature( "GET", "https://u.example/", {}, {}, "", 0, false, r ) == S_OK ) okTok++;
    };
    std::thread c( tokWork ), d( tokWork );
    c.join();
    d.join();
    EXPECT_EQ( okTok.load(), 6 );
    EXPECT_EQ( t.max_inflight.load(), 1 );
}
