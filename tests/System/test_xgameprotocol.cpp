/*
 * Xbox Game runtime Library
 *  Tests: XGameProtocol
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
#include <xgameprotocol.h>

namespace
{
    std::atomic_int g_callbackCalls{ 0 };

    void __stdcall CountingCallback( void *, const char * )
    {
        ++g_callbackCalls;
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

    // Not an interface this class serves.
    const IID kUnrelatedIid = { 0x6f1c2b7e, 0x4d3a, 0x4a52, { 0x9b, 0x10, 0x5c, 0x77, 0x0e, 0x21, 0xaa, 0x01 } };

    const IID *const kAcceptedIids[] =
    {
        &__uuidof( IUnknown ),
        &__uuidof( IInspectable ),
        &__uuidof( IAgileObject ),
        &__uuidof( IXGameProtocolImpl ),
    };
}

class XGameProtocolTests : public XGameRuntimeTests
{
protected:
    IXGameProtocolImpl *Protocol()
    {
        IXGameProtocolImpl *impl = nullptr;
        EXPECT_EQ( S_OK, QueryImpl( CLSID_XGameProtocolImpl, __uuidof( IXGameProtocolImpl ),
                                    reinterpret_cast<void **>( &impl ) ) );
        EXPECT_NE( nullptr, impl );
        return impl;
    }

    void SetUp() override
    {
        g_callbackCalls = 0;
    }
};

// --- QueryApiImpl --- //

TEST_F( XGameProtocolTests, ServesTheAcceptedInterfaces )
{
    for ( const IID *iid : kAcceptedIids )
    {
        void *out = nullptr;
        EXPECT_EQ( S_OK, QueryImpl( CLSID_XGameProtocolImpl, *iid, &out ) );
        ASSERT_NE( nullptr, out );
        static_cast<IUnknown *>( out )->Release();
    }
}

TEST_F( XGameProtocolTests, RefusesOtherInterfacesAndNullOut )
{
    void *out = reinterpret_cast<void *>( 1 );
    EXPECT_EQ( E_NOINTERFACE, QueryImpl( CLSID_XGameProtocolImpl, kUnrelatedIid, &out ) );
    EXPECT_EQ( nullptr, out );

    EXPECT_EQ( E_POINTER, QueryImpl( CLSID_XGameProtocolImpl, __uuidof( IUnknown ), nullptr ) );
}

TEST_F( XGameProtocolTests, QueryInterfaceAddsAReference )
{
    IXGameProtocolImpl *impl = Protocol();
    ASSERT_NE( nullptr, impl );

    // The current count: AddRef returns the new count and Release puts it back.
    auto count = [impl]() { impl->AddRef(); return impl->Release(); };

    ULONG before = count();
    void *again = nullptr;
    ASSERT_EQ( S_OK, QueryImpl( CLSID_XGameProtocolImpl, __uuidof( IUnknown ), &again ) );
    EXPECT_EQ( before + 1, count() );

    static_cast<IUnknown *>( again )->Release();
    EXPECT_EQ( before, count() );
    impl->Release();
}

// --- XGameProtocolRegisterForActivation --- //

TEST_F( XGameProtocolTests, RegisterRejectsNullCallbackAndNullToken )
{
    IXGameProtocolImpl *impl = Protocol();
    ASSERT_NE( nullptr, impl );

    XTaskQueueRegistrationToken token = { 0x5A5A5A5A };
    EXPECT_EQ( E_INVALIDARG, impl->XGameProtocolRegisterForActivation( nullptr, nullptr, nullptr, &token ) );
    EXPECT_EQ( 0x5A5A5A5Au, token.token );

    EXPECT_EQ( E_INVALIDARG, impl->XGameProtocolRegisterForActivation( nullptr, nullptr, CountingCallback, nullptr ) );
    impl->Release();
}

TEST_F( XGameProtocolTests, RegisterIssuesDistinctNonzeroTokensAndNeverCalls )
{
    IXGameProtocolImpl *impl = Protocol();
    ASSERT_NE( nullptr, impl );

    XTaskQueueRegistrationToken a = {}, b = {};
    EXPECT_EQ( S_OK, impl->XGameProtocolRegisterForActivation( nullptr, nullptr, CountingCallback, &a ) );
    EXPECT_EQ( S_OK, impl->XGameProtocolRegisterForActivation( nullptr, &a, CountingCallback, &b ) );
    EXPECT_NE( 0u, a.token );
    EXPECT_NE( 0u, b.token );
    EXPECT_NE( a.token, b.token );

    EXPECT_TRUE( impl->XGameProtocolUnregisterForActivation( a, FALSE ) );
    EXPECT_TRUE( impl->XGameProtocolUnregisterForActivation( b, TRUE ) );
    EXPECT_EQ( 0, g_callbackCalls.load() );
    impl->Release();
}

// --- XGameProtocolUnregisterForActivation --- //

TEST_F( XGameProtocolTests, UnregisterReturnsTrueOnceThenFalse )
{
    IXGameProtocolImpl *impl = Protocol();
    ASSERT_NE( nullptr, impl );

    XTaskQueueRegistrationToken token = {};
    ASSERT_EQ( S_OK, impl->XGameProtocolRegisterForActivation( nullptr, nullptr, CountingCallback, &token ) );

    EXPECT_TRUE( impl->XGameProtocolUnregisterForActivation( token, FALSE ) );
    EXPECT_FALSE( impl->XGameProtocolUnregisterForActivation( token, FALSE ) );
    impl->Release();
}

TEST_F( XGameProtocolTests, UnregisterRejectsZeroAndNeverIssuedTokens )
{
    IXGameProtocolImpl *impl = Protocol();
    ASSERT_NE( nullptr, impl );

    XTaskQueueRegistrationToken zero = { 0 };
    XTaskQueueRegistrationToken unknown = { 0xFFFFFFFFFFFFFFF0ull };
    EXPECT_FALSE( impl->XGameProtocolUnregisterForActivation( zero, FALSE ) );
    EXPECT_FALSE( impl->XGameProtocolUnregisterForActivation( unknown, TRUE ) );
    impl->Release();
}

TEST_F( XGameProtocolTests, UnregisterLeavesOtherRegistrationsAlone )
{
    IXGameProtocolImpl *impl = Protocol();
    ASSERT_NE( nullptr, impl );

    XTaskQueueRegistrationToken a = {}, b = {};
    ASSERT_EQ( S_OK, impl->XGameProtocolRegisterForActivation( nullptr, nullptr, CountingCallback, &a ) );
    ASSERT_EQ( S_OK, impl->XGameProtocolRegisterForActivation( nullptr, nullptr, CountingCallback, &b ) );

    EXPECT_TRUE( impl->XGameProtocolUnregisterForActivation( a, FALSE ) );
    EXPECT_TRUE( impl->XGameProtocolUnregisterForActivation( b, TRUE ) );
    impl->Release();
}

TEST_F( XGameProtocolTests, ATokenIsNotReusedAfterUnregister )
{
    IXGameProtocolImpl *impl = Protocol();
    ASSERT_NE( nullptr, impl );

    XTaskQueueRegistrationToken a = {}, b = {};
    ASSERT_EQ( S_OK, impl->XGameProtocolRegisterForActivation( nullptr, nullptr, CountingCallback, &a ) );
    ASSERT_TRUE( impl->XGameProtocolUnregisterForActivation( a, FALSE ) );
    ASSERT_EQ( S_OK, impl->XGameProtocolRegisterForActivation( nullptr, nullptr, CountingCallback, &b ) );

    EXPECT_NE( a.token, b.token );
    // The stale token must not remove the newer registration.
    EXPECT_FALSE( impl->XGameProtocolUnregisterForActivation( a, FALSE ) );
    EXPECT_TRUE( impl->XGameProtocolUnregisterForActivation( b, FALSE ) );
    impl->Release();
}

// --- Concurrency --- //

TEST_F( XGameProtocolTests, ConcurrentRegisterAndUnregisterAreConsistent )
{
    constexpr int kThreads = 8;
    constexpr int kPerThread = 2000;

    IXGameProtocolImpl *impl = Protocol();
    ASSERT_NE( nullptr, impl );

    std::vector<std::vector<UINT64>> tokens( kThreads );
    std::atomic_int failures{ 0 };

    // Runs fn on kThreads threads released together, so their calls overlap.
    auto runTogether = [&]( auto fn )
    {
        std::atomic_bool go{ false };
        std::vector<std::thread> threads;
        for ( int t = 0; t < kThreads; ++t )
        {
            threads.emplace_back( [&, t]()
            {
                while ( !go ) std::this_thread::yield();
                fn( t );
            } );
        }
        go = true;
        for ( std::thread &thread : threads ) thread.join();
    };

    // Every registration stays live, so the table grows while the threads overlap.
    runTogether( [&]( int t )
    {
        for ( int i = 0; i < kPerThread; ++i )
        {
            XTaskQueueRegistrationToken token = {};
            if ( impl->XGameProtocolRegisterForActivation( nullptr, nullptr, CountingCallback, &token ) != S_OK ||
                 token.token == 0 )
            {
                ++failures;
                continue;
            }
            tokens[t].push_back( token.token );
        }
    } );

    EXPECT_EQ( 0, failures.load() );
    std::set<UINT64> distinct;
    for ( const auto &list : tokens ) distinct.insert( list.begin(), list.end() );
    EXPECT_EQ( static_cast<size_t>( kThreads * kPerThread ), distinct.size() );

    runTogether( [&]( int t )
    {
        for ( UINT64 value : tokens[t] )
        {
            XTaskQueueRegistrationToken token = { value };
            if ( !impl->XGameProtocolUnregisterForActivation( token, FALSE ) ) ++failures;
        }
    } );

    EXPECT_EQ( 0, failures.load() );

    // Everything was unregistered, so nothing is left to remove.
    for ( UINT64 value : distinct )
    {
        XTaskQueueRegistrationToken again = { value };
        EXPECT_FALSE( impl->XGameProtocolUnregisterForActivation( again, FALSE ) );
    }
    impl->Release();
}

// --- Exported functions --- //

TEST_F( XGameProtocolTests, ExportedFunctionsReachTheClass )
{
    XTaskQueueRegistrationToken token = {};
    ASSERT_EQ( S_OK, XGameProtocolRegisterForActivation( nullptr, nullptr, CountingCallback, &token ) );
    EXPECT_NE( 0u, token.token );
    EXPECT_TRUE( XGameProtocolUnregisterForActivation( token, FALSE ) );
    EXPECT_FALSE( XGameProtocolUnregisterForActivation( token, FALSE ) );
}
