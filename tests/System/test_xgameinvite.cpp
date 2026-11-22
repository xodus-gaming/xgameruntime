/*
 * XGameInvite Tests
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

#include "../test_fixtures.h"

#include <atomic>
#include <set>
#include <thread>
#include <vector>

#define WIDL_EXPLICIT_AGGREGATE_RETURNS
#include <windows.h>
#include <inspectable.h>
#include <xgameruntimeinit.h>
#include <xgameinvite.h>

namespace
{
    std::atomic_int g_callbacks{ 0 };

    void __stdcall CountingCallback( void *, const char * )
    {
        ++g_callbacks;
    }

    // Asks xgameruntime.dll directly, bypassing the wrappers.
    HRESULT QueryImpl( REFCLSID clsid, REFIID iid, void **out )
    {
        static HRESULT (WINAPI *query)( REFCLSID, REFIID, void ** ) = nullptr;

        if ( !query )
        {
            EXPECT_EQ( S_OK, XGameRuntimeInitialize() );
            HMODULE dll = LoadLibraryA( "xgameruntime.dll" );
            EXPECT_NE( nullptr, dll );
            query = reinterpret_cast<HRESULT (WINAPI *)( REFCLSID, REFIID, void ** )>
                    ( GetProcAddress( dll, "QueryApiImpl" ) );
            EXPECT_NE( nullptr, query );
        }

        return query( clsid, iid, out );
    }

    const IID *const kAcceptedIids[] =
    {
        &__uuidof( IUnknown ),
        &__uuidof( IInspectable ),
        &__uuidof( IAgileObject ),
        &__uuidof( IXGameInviteImpl ),
        &__uuidof( IXGameInviteImpl2 ),
    };

    constexpr UINT64 kSentinel = 0x5A5A5A5A5A5A5A5Aull;
    constexpr char kUri[] = "ms-xbl-0000://inviteHandleAccept?x=1";
}

class XGameInviteTests : public XGameRuntimeTests
{
protected:
    IXGameInviteImpl2 *Get()
    {
        void *out = nullptr;
        EXPECT_EQ( S_OK, QueryImpl( CLSID_XGameInviteImpl, __uuidof( IXGameInviteImpl2 ), &out ) );
        EXPECT_NE( nullptr, out );
        IXGameInviteImpl2 *invite = static_cast<IXGameInviteImpl2 *>( out );
        if ( invite ) invite->Release();
        return invite;
    }
};

TEST_F( XGameInviteTests, QueryInterfaceAcceptsAllInterfaces )
{
    for ( const IID *iid : kAcceptedIids )
    {
        void *out = nullptr;
        EXPECT_EQ( S_OK, QueryImpl( CLSID_XGameInviteImpl, *iid, &out ) );
        EXPECT_NE( nullptr, out );
        if ( out ) static_cast<IUnknown *>( out )->Release();
    }
}

TEST_F( XGameInviteTests, QueryInterfaceReturnsSameObject )
{
    void *a = nullptr, *b = nullptr;
    ASSERT_EQ( S_OK, QueryImpl( CLSID_XGameInviteImpl, __uuidof( IXGameInviteImpl ), &a ) );
    ASSERT_EQ( S_OK, QueryImpl( CLSID_XGameInviteImpl, __uuidof( IXGameInviteImpl2 ), &b ) );
    EXPECT_EQ( a, b );
    static_cast<IUnknown *>( a )->Release();
    static_cast<IUnknown *>( b )->Release();
}

TEST_F( XGameInviteTests, QueryInterfaceRejectsUnknownIid )
{
    void *out = reinterpret_cast<void *>( 1 );
    EXPECT_EQ( E_NOINTERFACE, QueryImpl( CLSID_XGameInviteImpl, IID_IClassFactory, &out ) );
    EXPECT_EQ( nullptr, out );
}

TEST_F( XGameInviteTests, QueryInterfaceNullOut )
{
    EXPECT_EQ( E_POINTER, QueryImpl( CLSID_XGameInviteImpl, __uuidof( IXGameInviteImpl2 ), nullptr ) );
}

TEST_F( XGameInviteTests, RefCountTracksAddRefAndRelease )
{
    IXGameInviteImpl2 *invite = Get();
    ASSERT_NE( nullptr, invite );
    ULONG base = invite->AddRef();
    EXPECT_EQ( base + 1, invite->AddRef() );
    EXPECT_EQ( base, invite->Release() );
    EXPECT_EQ( base - 1, invite->Release() );
}

TEST_F( XGameInviteTests, RegisterReturnsDistinctNonZeroTokens )
{
    IXGameInviteImpl2 *invite = Get();
    ASSERT_NE( nullptr, invite );
    std::set<UINT64> seen;
    std::vector<XTaskQueueRegistrationToken> normal, pending;

    for ( int i = 0; i < 4; ++i )
    {
        XTaskQueueRegistrationToken a{}, b{};
        EXPECT_EQ( S_OK, invite->XGameInviteRegisterForEvent( nullptr, nullptr, CountingCallback, &a ) );
        EXPECT_EQ( S_OK, invite->XGameInviteRegisterForPendingEvent( nullptr, nullptr, CountingCallback, &b ) );
        EXPECT_NE( 0u, a.token );
        EXPECT_NE( 0u, b.token );
        EXPECT_TRUE( seen.insert( a.token ).second );
        EXPECT_TRUE( seen.insert( b.token ).second );
        normal.push_back( a );
        pending.push_back( b );
    }

    for ( auto t : normal ) EXPECT_TRUE( invite->XGameInviteUnregisterForEvent( t, FALSE ) );
    for ( auto t : pending ) EXPECT_TRUE( invite->XGameInviteUnregisterForPendingEvent( t, FALSE ) );
}

TEST_F( XGameInviteTests, RegisterRejectsNullArguments )
{
    IXGameInviteImpl2 *invite = Get();
    ASSERT_NE( nullptr, invite );
    XTaskQueueRegistrationToken token;

    token.token = kSentinel;
    EXPECT_EQ( E_INVALIDARG, invite->XGameInviteRegisterForEvent( nullptr, nullptr, nullptr, &token ) );
    EXPECT_EQ( kSentinel, token.token );
    EXPECT_EQ( E_INVALIDARG, invite->XGameInviteRegisterForEvent( nullptr, nullptr, CountingCallback, nullptr ) );

    token.token = kSentinel;
    EXPECT_EQ( E_INVALIDARG, invite->XGameInviteRegisterForPendingEvent( nullptr, nullptr, nullptr, &token ) );
    EXPECT_EQ( kSentinel, token.token );
    EXPECT_EQ( E_INVALIDARG, invite->XGameInviteRegisterForPendingEvent( nullptr, nullptr, CountingCallback, nullptr ) );
}

TEST_F( XGameInviteTests, CallbackIsNeverInvoked )
{
    IXGameInviteImpl2 *invite = Get();
    ASSERT_NE( nullptr, invite );
    g_callbacks = 0;
    XTaskQueueRegistrationToken a{}, b{};
    ASSERT_EQ( S_OK, invite->XGameInviteRegisterForEvent( nullptr, nullptr, CountingCallback, &a ) );
    ASSERT_EQ( S_OK, invite->XGameInviteRegisterForPendingEvent( nullptr, nullptr, CountingCallback, &b ) );
    Sleep( 200 );
    EXPECT_EQ( 0, g_callbacks.load() );
    EXPECT_TRUE( invite->XGameInviteUnregisterForEvent( a, TRUE ) );
    EXPECT_TRUE( invite->XGameInviteUnregisterForPendingEvent( b, TRUE ) );
}

TEST_F( XGameInviteTests, UnregisterRemovesTokenOnce )
{
    IXGameInviteImpl2 *invite = Get();
    ASSERT_NE( nullptr, invite );
    XTaskQueueRegistrationToken a{}, b{};
    ASSERT_EQ( S_OK, invite->XGameInviteRegisterForEvent( nullptr, nullptr, CountingCallback, &a ) );
    ASSERT_EQ( S_OK, invite->XGameInviteRegisterForPendingEvent( nullptr, nullptr, CountingCallback, &b ) );

    EXPECT_TRUE( invite->XGameInviteUnregisterForEvent( a, FALSE ) );
    EXPECT_FALSE( invite->XGameInviteUnregisterForEvent( a, FALSE ) );
    EXPECT_TRUE( invite->XGameInviteUnregisterForPendingEvent( b, FALSE ) );
    EXPECT_FALSE( invite->XGameInviteUnregisterForPendingEvent( b, FALSE ) );
}

TEST_F( XGameInviteTests, UnregisterUnissuedTokenFails )
{
    IXGameInviteImpl2 *invite = Get();
    ASSERT_NE( nullptr, invite );
    XTaskQueueRegistrationToken bogus;
    bogus.token = kSentinel;
    EXPECT_FALSE( invite->XGameInviteUnregisterForEvent( bogus, FALSE ) );
    EXPECT_FALSE( invite->XGameInviteUnregisterForPendingEvent( bogus, FALSE ) );
}

TEST_F( XGameInviteTests, TokensAreNotInterchangeableBetweenKinds )
{
    IXGameInviteImpl2 *invite = Get();
    ASSERT_NE( nullptr, invite );
    XTaskQueueRegistrationToken a{}, b{};
    ASSERT_EQ( S_OK, invite->XGameInviteRegisterForEvent( nullptr, nullptr, CountingCallback, &a ) );
    ASSERT_EQ( S_OK, invite->XGameInviteRegisterForPendingEvent( nullptr, nullptr, CountingCallback, &b ) );

    EXPECT_FALSE( invite->XGameInviteUnregisterForPendingEvent( a, FALSE ) );
    EXPECT_FALSE( invite->XGameInviteUnregisterForEvent( b, FALSE ) );
    EXPECT_TRUE( invite->XGameInviteUnregisterForEvent( a, FALSE ) );
    EXPECT_TRUE( invite->XGameInviteUnregisterForPendingEvent( b, FALSE ) );
}

TEST_F( XGameInviteTests, AcceptPendingInviteResults )
{
    IXGameInviteImpl2 *invite = Get();
    ASSERT_NE( nullptr, invite );
    EXPECT_EQ( E_INVALIDARG, invite->XGameInviteAcceptPendingInvite( nullptr ) );
    EXPECT_EQ( E_INVALIDARG, invite->XGameInviteAcceptPendingInvite( "" ) );
    EXPECT_EQ( HRESULT_FROM_WIN32( ERROR_NOT_FOUND ), invite->XGameInviteAcceptPendingInvite( kUri ) );
}

TEST_F( XGameInviteTests, ConcurrentRegistersGiveDistinctTokens )
{
    IXGameInviteImpl2 *invite = Get();
    ASSERT_NE( nullptr, invite );

    constexpr int kThreads = 8, kPerThread = 100;
    std::vector<std::vector<XTaskQueueRegistrationToken>> tokens( kThreads );
    std::vector<std::thread> threads;

    for ( int t = 0; t < kThreads; ++t )
        threads.emplace_back( [&, t]
        {
            for ( int i = 0; i < kPerThread; ++i )
            {
                XTaskQueueRegistrationToken token{};
                if ( invite->XGameInviteRegisterForEvent( nullptr, nullptr, CountingCallback, &token ) == S_OK )
                    tokens[t].push_back( token );
            }
        } );
    for ( auto &th : threads ) th.join();

    std::set<UINT64> seen;
    for ( auto &v : tokens )
        for ( auto token : v )
            seen.insert( token.token );
    EXPECT_EQ( size_t( kThreads * kPerThread ), seen.size() );

    for ( auto &v : tokens )
        for ( auto token : v )
            EXPECT_TRUE( invite->XGameInviteUnregisterForEvent( token, FALSE ) );
}
