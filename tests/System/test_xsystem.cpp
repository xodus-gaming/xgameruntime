/*
 * XSystem Tests
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

#include <cstring>
#include <vector>

#define WIDL_EXPLICIT_AGGREGATE_RETURNS
#include <windows.h>
#include <inspectable.h>
#include <xgameruntimeinit.h>
#include <xsystem.h>

namespace
{
    constexpr unsigned char kCanary = 0xCC;
    constexpr SIZE_T kUsedSentinel = 0x5A5A5A5A;
    constexpr char kConsoleId[] = "00000000.00000000.00000000.00000000.00";
    constexpr char kSandboxId[] = "RETAIL";

    typedef HRESULT (__stdcall *IdFunction)( INT32, char *, SIZE_T * );

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

    // A buffer of the given size filled with the canary byte, to see what a call touched.
    std::vector<unsigned char> Canary( size_t size )
    {
        return std::vector<unsigned char>( size, kCanary );
    }

    bool AllCanary( const std::vector<unsigned char> &buf, size_t from )
    {
        for ( size_t i = from; i < buf.size(); ++i )
            if ( buf[i] != kCanary ) return false;
        return true;
    }

    const IID *const kAcceptedIids[] =
    {
        &__uuidof( IUnknown ),
        &__uuidof( IInspectable ),
        &__uuidof( IAgileObject ),
        &__uuidof( IXSystemImpl ),
        &__uuidof( IXSystemImpl2 ),
        &__uuidof( IXSystemImpl3 ),
        &__uuidof( IXSystemImpl4 ),
        &__uuidof( IXSystemImpl5 ),
    };
}

class XSystemTests : public XGameRuntimeTests
{
protected:
    // The id is returned, with its NUL, in every buffer of at least minSize bytes.
    void ExpectIdFits( IdFunction call, const char *id, INT32 size )
    {
        SCOPED_TRACE( size );
        std::vector<unsigned char> buf = Canary( size );
        SIZE_T used = kUsedSentinel;
        const size_t idBytes = std::strlen( id ) + 1;

        EXPECT_EQ( S_OK, call( size, reinterpret_cast<char *>( buf.data() ), &used ) );
        EXPECT_EQ( idBytes, used );
        EXPECT_EQ( 0, std::memcmp( buf.data(), id, idBytes ) );
        EXPECT_TRUE( AllCanary( buf, idBytes ) );
    }

    // Too small: nothing is written, used reports the minimum buffer.
    void ExpectIdTooSmall( IdFunction call, INT32 size, SIZE_T minSize )
    {
        SCOPED_TRACE( size );
        std::vector<unsigned char> buf = Canary( 64 );
        SIZE_T used = kUsedSentinel;

        EXPECT_EQ( HRESULT_FROM_WIN32( ERROR_INSUFFICIENT_BUFFER ),
                   call( size, reinterpret_cast<char *>( buf.data() ), &used ) );
        EXPECT_EQ( minSize, used );
        EXPECT_TRUE( AllCanary( buf, 0 ) );

        // The same, without a used pointer.
        EXPECT_EQ( HRESULT_FROM_WIN32( ERROR_INSUFFICIENT_BUFFER ),
                   call( size, reinterpret_cast<char *>( buf.data() ), nullptr ) );
        EXPECT_TRUE( AllCanary( buf, 0 ) );

        // A size query: too small is reported before a null buffer.
        used = kUsedSentinel;
        EXPECT_EQ( HRESULT_FROM_WIN32( ERROR_INSUFFICIENT_BUFFER ), call( size, nullptr, &used ) );
        EXPECT_EQ( minSize, used );
    }

    void ExpectNullBufferIsInvalid( IdFunction call, INT32 size )
    {
        SIZE_T used = kUsedSentinel;
        EXPECT_EQ( E_INVALIDARG, call( size, nullptr, &used ) );
        EXPECT_EQ( kUsedSentinel, used );
    }

    void ExpectNullUsedIsAccepted( IdFunction call, const char *id, INT32 size )
    {
        std::vector<unsigned char> buf = Canary( size );
        EXPECT_EQ( S_OK, call( size, reinterpret_cast<char *>( buf.data() ), nullptr ) );
        EXPECT_STREQ( id, reinterpret_cast<char *>( buf.data() ) );
    }
};

// --- XSystemGetConsoleId --- //

TEST_F( XSystemTests, ConsoleIdIsTheAllZeroPcId )
{
    for ( INT32 size : { 39, 40, 45, 4096 } )
        ExpectIdFits( XSystemGetConsoleId, kConsoleId, size );
}

TEST_F( XSystemTests, ConsoleIdIsTerminatedAtByte38 )
{
    std::vector<unsigned char> buf = Canary( 39 );
    SIZE_T used = 0;
    ASSERT_EQ( S_OK, XSystemGetConsoleId( 39, reinterpret_cast<char *>( buf.data() ), &used ) );
    EXPECT_EQ( 39u, used );
    EXPECT_EQ( 0, buf[38] );
    EXPECT_EQ( 38u, std::strlen( reinterpret_cast<char *>( buf.data() ) ) );
}

TEST_F( XSystemTests, ConsoleIdTooSmallReportsTheMinimum )
{
    for ( INT32 size : { 0, 1, 38, -1 } )
        ExpectIdTooSmall( XSystemGetConsoleId, size, 39 );
}

TEST_F( XSystemTests, ConsoleIdNullBufferIsInvalid )
{
    ExpectNullBufferIsInvalid( XSystemGetConsoleId, 39 );
    ExpectNullBufferIsInvalid( XSystemGetConsoleId, 4096 );
}

TEST_F( XSystemTests, ConsoleIdAcceptsNullUsed )
{
    ExpectNullUsedIsAccepted( XSystemGetConsoleId, kConsoleId, 39 );
}

TEST_F( XSystemTests, ConsoleIdRepeatedCallsAreIdentical )
{
    char first[39], second[39];
    SIZE_T used1 = 0, used2 = 0;
    ASSERT_EQ( S_OK, XSystemGetConsoleId( 39, first, &used1 ) );
    ASSERT_EQ( S_OK, XSystemGetConsoleId( 39, second, &used2 ) );
    EXPECT_EQ( used1, used2 );
    EXPECT_EQ( 0, std::memcmp( first, second, 39 ) );
}

// --- XSystemGetXboxLiveSandboxId --- //

TEST_F( XSystemTests, SandboxIdIsRetail )
{
    for ( INT32 size : { 16, 17, 4096 } )
        ExpectIdFits( XSystemGetXboxLiveSandboxId, kSandboxId, size );
}

TEST_F( XSystemTests, SandboxIdTooSmallReportsTheMinimum )
{
    for ( INT32 size : { 0, 1, 15, -1 } )
        ExpectIdTooSmall( XSystemGetXboxLiveSandboxId, size, 16 );
}

TEST_F( XSystemTests, SandboxIdNullBufferIsInvalid )
{
    ExpectNullBufferIsInvalid( XSystemGetXboxLiveSandboxId, 16 );
    ExpectNullBufferIsInvalid( XSystemGetXboxLiveSandboxId, 4096 );
}

TEST_F( XSystemTests, SandboxIdAcceptsNullUsed )
{
    ExpectNullUsedIsAccepted( XSystemGetXboxLiveSandboxId, kSandboxId, 16 );
}

TEST_F( XSystemTests, SandboxIdRepeatedCallsAreIdentical )
{
    char first[16], second[16];
    SIZE_T used1 = 0, used2 = 0;
    ASSERT_EQ( S_OK, XSystemGetXboxLiveSandboxId( 16, first, &used1 ) );
    ASSERT_EQ( S_OK, XSystemGetXboxLiveSandboxId( 16, second, &used2 ) );
    EXPECT_EQ( used1, used2 );
    EXPECT_EQ( 0, std::memcmp( first, second, used1 ) );
}

// --- Unimplemented functions --- //

TEST_F( XSystemTests, AppSpecificDeviceIdIsNotImplementedAndWritesNothing )
{
    for ( INT32 size : { 0, 45, 4096 } )
    {
        SCOPED_TRACE( size );
        std::vector<unsigned char> buf = Canary( 64 );
        SIZE_T used = kUsedSentinel;

        EXPECT_EQ( E_NOTIMPL, XSystemGetAppSpecificDeviceId( size, reinterpret_cast<char *>( buf.data() ), &used ) );
        EXPECT_EQ( kUsedSentinel, used );
        EXPECT_TRUE( AllCanary( buf, 0 ) );
    }
}

TEST_F( XSystemTests, HandleTrackIsNotImplemented )
{
    EXPECT_EQ( E_NOTIMPL, XSystemHandleTrack( nullptr, nullptr ) );
}

TEST_F( XSystemTests, NoHandleIsValid )
{
    EXPECT_FALSE( XSystemIsHandleValid( nullptr ) );
    EXPECT_FALSE( XSystemIsHandleValid( reinterpret_cast<XSystemHandle>( 0x1234 ) ) );
}

TEST_F( XSystemTests, AllowFullDownloadBandwidthIsHarmless )
{
    XSystemAllowFullDownloadBandwidth( TRUE );
    XSystemAllowFullDownloadBandwidth( FALSE );

    char id[39];
    SIZE_T used = 0;
    EXPECT_EQ( S_OK, XSystemGetConsoleId( 39, id, &used ) );
}

// --- QueryInterface and reference counting --- //

TEST_F( XSystemTests, EveryAcceptedIidReturnsTheSameObject )
{
    IUnknown *unknown = nullptr;
    ASSERT_EQ( S_OK, QueryImpl( __uuidof( XSystemImpl ), __uuidof( IUnknown ), reinterpret_cast<void **>( &unknown ) ) );
    ASSERT_NE( nullptr, unknown );

    for ( const IID *iid : kAcceptedIids )
    {
        void *out = reinterpret_cast<void *>( 0x1 );
        EXPECT_EQ( S_OK, unknown->QueryInterface( *iid, &out ) );
        EXPECT_EQ( static_cast<void *>( unknown ), out );
        if ( out != reinterpret_cast<void *>( 0x1 ) )
            static_cast<IUnknown *>( out )->Release();
    }

    unknown->Release();
}

TEST_F( XSystemTests, EveryAcceptedIidIsServedByTheClassFactoryEntry )
{
    for ( const IID *iid : kAcceptedIids )
    {
        void *out = nullptr;
        EXPECT_EQ( S_OK, QueryImpl( __uuidof( XSystemImpl ), *iid, &out ) );
        EXPECT_NE( nullptr, out );
        if ( out ) static_cast<IUnknown *>( out )->Release();
    }
}

TEST_F( XSystemTests, TheFullVtableIsReachableThroughTheGamesInterface )
{
    // The game asks for IXSystemImpl4 and calls methods of IXSystemImpl, 3 and 4 on it.
    IXSystemImpl4 *impl = nullptr;
    ASSERT_EQ( S_OK, QueryImpl( __uuidof( XSystemImpl ), __uuidof( IXSystemImpl4 ), reinterpret_cast<void **>( &impl ) ) );

    char id[16];
    SIZE_T used = 0;
    EXPECT_EQ( S_OK, impl->XSystemGetXboxLiveSandboxId( 16, id, &used ) );
    EXPECT_STREQ( "RETAIL", id );
    EXPECT_EQ( E_NOTIMPL, impl->XSystemGetAppSpecificDeviceId( 0, nullptr, nullptr ) );
    EXPECT_EQ( E_NOTIMPL, impl->XSystemHandleTrack( nullptr, nullptr ) );
    EXPECT_FALSE( impl->XSystemIsHandleValid( nullptr ) );
    impl->XSystemAllowFullDownloadBandwidth( TRUE );

    impl->Release();
}

TEST_F( XSystemTests, AnUnknownIidIsRefused )
{
    // IID of IXSystemAnalyticsImpl: a sibling object's interface is not served here.
    const IID unknownIids[] =
    {
        { 0x7a1e3c5d, 0x1111, 0x4222, { 0x93, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99 } },
        __uuidof( IXSystemAnalyticsImpl ),
    };

    IUnknown *unknown = nullptr;
    ASSERT_EQ( S_OK, QueryImpl( __uuidof( XSystemImpl ), __uuidof( IUnknown ), reinterpret_cast<void **>( &unknown ) ) );

    for ( const IID &iid : unknownIids )
    {
        void *out = reinterpret_cast<void *>( 0x1 );
        EXPECT_EQ( E_NOINTERFACE, unknown->QueryInterface( iid, &out ) );
        EXPECT_EQ( nullptr, out );

        out = reinterpret_cast<void *>( 0x1 );
        EXPECT_EQ( E_NOINTERFACE, QueryImpl( __uuidof( XSystemImpl ), iid, &out ) );
        EXPECT_EQ( nullptr, out );
    }

    unknown->Release();
}

TEST_F( XSystemTests, ANullOutPointerIsRefused )
{
    IUnknown *unknown = nullptr;
    ASSERT_EQ( S_OK, QueryImpl( __uuidof( XSystemImpl ), __uuidof( IUnknown ), reinterpret_cast<void **>( &unknown ) ) );

    EXPECT_EQ( E_POINTER, unknown->QueryInterface( __uuidof( IUnknown ), nullptr ) );
    EXPECT_EQ( E_POINTER, QueryImpl( __uuidof( XSystemImpl ), __uuidof( IUnknown ), nullptr ) );

    unknown->Release();
}

TEST_F( XSystemTests, ReferenceCountsGoUpAndDown )
{
    IUnknown *unknown = nullptr;
    ASSERT_EQ( S_OK, QueryImpl( __uuidof( XSystemImpl ), __uuidof( IUnknown ), reinterpret_cast<void **>( &unknown ) ) );

    // Count at rest, with the reference QueryApiImpl gave us handed back (the
    // object is static, so using the pointer afterwards is safe).
    ULONG base = unknown->Release();

    ULONG one = unknown->AddRef();
    EXPECT_EQ( base + 1, one );
    ULONG two = unknown->AddRef();
    EXPECT_EQ( one + 1, two );
    EXPECT_EQ( one, unknown->Release() );
    EXPECT_EQ( base, unknown->Release() );

    // A successful QueryInterface adds a reference, a refused one does not.
    IXSystemImpl4 *impl = nullptr;
    ASSERT_EQ( S_OK, unknown->QueryInterface( __uuidof( IXSystemImpl4 ), reinterpret_cast<void **>( &impl ) ) );
    EXPECT_EQ( base + 2, unknown->AddRef() );
    EXPECT_EQ( base + 1, unknown->Release() );
    EXPECT_EQ( base, impl->Release() );

    void *out = nullptr;
    EXPECT_EQ( E_NOINTERFACE, unknown->QueryInterface( __uuidof( IXSystemAnalyticsImpl ), &out ) );
    EXPECT_EQ( base + 1, unknown->AddRef() );
    EXPECT_EQ( base, unknown->Release() );
}

TEST_F( XSystemTests, AnUnregisteredClassIsNotRegistered )
{
    const CLSID unregistered = { 0x7a1e3c5d, 0x2222, 0x4333, { 0xa4, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa } };
    void *out = nullptr;
    EXPECT_EQ( REGDB_E_CLASSNOTREG, QueryImpl( unregistered, __uuidof( IUnknown ), &out ) );
}
