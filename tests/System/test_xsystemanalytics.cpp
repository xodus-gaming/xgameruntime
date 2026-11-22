/*
 * XSystemAnalytics Tests
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
#include <string>
#include <thread>

#define WIDL_EXPLICIT_AGGREGATE_RETURNS
#include <windows.h>
#include <objbase.h>
#include <inspectable.h>
#include <xgameruntimeinit.h>
#include <xsystem.h>

#include "System/XSystemAnalytics.h"

namespace
{
    bool IsZero( const XSystemAnalyticsInfo &info )
    {
        static const XSystemAnalyticsInfo zero = {};
        return std::memcmp( &info, &zero, sizeof( info ) ) == 0;
    }

    XSystemAnalyticsInfo Dirty()
    {
        XSystemAnalyticsInfo info;
        std::memset( &info, 0xCC, sizeof( info ) );
        return info;
    }

    uint64_t Pack( UINT16 major, UINT16 minor, UINT16 build, UINT16 revision )
    {
        return ( uint64_t( major ) << 48 ) | ( uint64_t( minor ) << 32 ) | ( uint64_t( build ) << 16 ) | revision;
    }

    HRESULT QueryAnalytics( IXSystemAnalyticsImpl **out )
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

        return query( __uuidof( XSystemAnalyticsImpl ), __uuidof( IXSystemAnalyticsImpl ), reinterpret_cast<void **>( out ) );
    }

    // The real call, through the object, into a dirty buffer.
    XSystemAnalyticsInfo CallObject()
    {
        IXSystemAnalyticsImpl *impl = nullptr;
        EXPECT_EQ( S_OK, QueryAnalytics( &impl ) );

        XSystemAnalyticsInfo info = Dirty();
        if ( impl )
        {
            XSystemAnalyticsInfo *returned = impl->XSystemGetAnalyticsInfo( &info );
            EXPECT_EQ( &info, returned );
            impl->Release();
        }
        return info;
    }
}

class XSystemAnalyticsTests : public XGameRuntimeTests {};

// --- ParseAnalyticsInfo --- //

TEST_F( XSystemAnalyticsTests, ParsesTheWindowsDesktopAnswer )
{
    XSystemAnalyticsInfo info = Dirty();
    ASSERT_EQ( S_OK, ParseAnalyticsInfo( "Windows.Desktop", "2814750465147227", info ) );
    EXPECT_STREQ( "Windows", info.family );
    EXPECT_STREQ( "Desktop", info.form );
    EXPECT_EQ( 10, info.osVersion.major );
    EXPECT_EQ( 0, info.osVersion.minor );
    EXPECT_EQ( 10651, info.osVersion.build );
    EXPECT_EQ( 16731, info.osVersion.revision );
}

TEST_F( XSystemAnalyticsTests, SplitsTheVersionIntoFourFields )
{
    XSystemAnalyticsInfo info = Dirty();
    // 10.0.19045.3803
    ASSERT_EQ( S_OK, ParseAnalyticsInfo( "Windows.Desktop", std::to_string( Pack( 10, 0, 19045, 3803 ) ), info ) );
    EXPECT_EQ( 10, info.osVersion.major );
    EXPECT_EQ( 0, info.osVersion.minor );
    EXPECT_EQ( 19045, info.osVersion.build );
    EXPECT_EQ( 3803, info.osVersion.revision );
    EXPECT_EQ( 0, std::memcmp( &info.osVersion, &info.hostingOsVersion, sizeof( XVersion ) ) );
}

TEST_F( XSystemAnalyticsTests, EachVersionFieldIsIndependent )
{
    struct { uint64_t v; UINT16 a, b, c, d; } cases[] =
    {
        { Pack( 1, 0, 0, 0 ), 1, 0, 0, 0 },
        { Pack( 0, 2, 0, 0 ), 0, 2, 0, 0 },
        { Pack( 0, 0, 3, 0 ), 0, 0, 3, 0 },
        { Pack( 0, 0, 0, 4 ), 0, 0, 0, 4 },
        { Pack( 0, 0, 0, 0xFFFF ), 0, 0, 0, 0xFFFF },
        { Pack( 0, 0, 1, 0 ), 0, 0, 1, 0 },
        { Pack( 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF ), 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF },
        { 0, 0, 0, 0, },
    };

    for ( const auto &c : cases )
    {
        SCOPED_TRACE( c.v );
        XSystemAnalyticsInfo info = Dirty();
        ASSERT_EQ( S_OK, ParseAnalyticsInfo( "Windows.Desktop", std::to_string( c.v ), info ) );
        EXPECT_EQ( c.a, info.osVersion.major );
        EXPECT_EQ( c.b, info.osVersion.minor );
        EXPECT_EQ( c.c, info.osVersion.build );
        EXPECT_EQ( c.d, info.osVersion.revision );
        EXPECT_EQ( c.a, info.hostingOsVersion.major );
        EXPECT_EQ( c.d, info.hostingOsVersion.revision );
    }
}

TEST_F( XSystemAnalyticsTests, FamilyWithoutADotHasNoForm )
{
    XSystemAnalyticsInfo info = Dirty();
    ASSERT_EQ( S_OK, ParseAnalyticsInfo( "Windows", "1", info ) );
    EXPECT_STREQ( "Windows", info.family );
    EXPECT_STREQ( "", info.form );
}

TEST_F( XSystemAnalyticsTests, OnlyTheFirstDotSplits )
{
    XSystemAnalyticsInfo info = Dirty();
    ASSERT_EQ( S_OK, ParseAnalyticsInfo( "Windows.Desktop.Extra", "1", info ) );
    EXPECT_STREQ( "Windows", info.family );
    EXPECT_STREQ( "Desktop.Extra", info.form );

    ASSERT_EQ( S_OK, ParseAnalyticsInfo( "Windows.", "1", info ) );
    EXPECT_STREQ( "Windows", info.family );
    EXPECT_STREQ( "", info.form );
}

TEST_F( XSystemAnalyticsTests, UnusedBytesAreZero )
{
    XSystemAnalyticsInfo info = Dirty();
    ASSERT_EQ( S_OK, ParseAnalyticsInfo( "Windows.Desktop", "1", info ) );
    for ( size_t i = strlen( info.family ); i < sizeof( info.family ); ++i ) EXPECT_EQ( 0, info.family[i] );
    for ( size_t i = strlen( info.form ); i < sizeof( info.form ); ++i ) EXPECT_EQ( 0, info.form[i] );
}

TEST_F( XSystemAnalyticsTests, RejectsAnEmptyOrLeadingDotFamily )
{
    for ( const char *family : { "", ".", ".Desktop" } )
    {
        SCOPED_TRACE( family );
        XSystemAnalyticsInfo info = Dirty();
        EXPECT_EQ( E_INVALIDARG, ParseAnalyticsInfo( family, "1", info ) );
        EXPECT_TRUE( IsZero( info ) );
    }
}

TEST_F( XSystemAnalyticsTests, RejectsABadVersion )
{
    const char *bad[] =
    {
        "", " 1", "1 ", "-1", "+1", "1.0", "0x10", "abc", "1a", "１２",
        "18446744073709551616",   // UINT64_MAX + 1
        "99999999999999999999",
        "184467440737095516150",
    };

    for ( const char *version : bad )
    {
        SCOPED_TRACE( version );
        XSystemAnalyticsInfo info = Dirty();
        EXPECT_EQ( E_INVALIDARG, ParseAnalyticsInfo( "Windows.Desktop", version, info ) );
        EXPECT_TRUE( IsZero( info ) );
    }
}

TEST_F( XSystemAnalyticsTests, AcceptsTheLargestVersion )
{
    XSystemAnalyticsInfo info = Dirty();
    ASSERT_EQ( S_OK, ParseAnalyticsInfo( "Windows.Desktop", "18446744073709551615", info ) );
    EXPECT_EQ( 0xFFFF, info.osVersion.major );
    EXPECT_EQ( 0xFFFF, info.osVersion.revision );
}

TEST_F( XSystemAnalyticsTests, AcceptsLeadingZerosAndZero )
{
    XSystemAnalyticsInfo info = Dirty();
    ASSERT_EQ( S_OK, ParseAnalyticsInfo( "Windows.Desktop", "0", info ) );
    EXPECT_EQ( 0, info.osVersion.major );
    ASSERT_EQ( S_OK, ParseAnalyticsInfo( "Windows.Desktop", "0007", info ) );
    EXPECT_EQ( 7, info.osVersion.revision );
}

TEST_F( XSystemAnalyticsTests, LongStringsAreCutAt63Bytes )
{
    const std::string longName( 100, 'a' );
    XSystemAnalyticsInfo info = Dirty();

    ASSERT_EQ( S_OK, ParseAnalyticsInfo( longName + "." + longName, "1", info ) );
    EXPECT_EQ( 63u, strlen( info.family ) );
    EXPECT_EQ( 63u, strlen( info.form ) );
    EXPECT_EQ( 0, info.family[63] );
    EXPECT_EQ( 0, info.form[63] );

    // Exactly 63 fits, 64 is cut by one.
    ASSERT_EQ( S_OK, ParseAnalyticsInfo( std::string( 63, 'b' ), "1", info ) );
    EXPECT_EQ( 63u, strlen( info.family ) );
    ASSERT_EQ( S_OK, ParseAnalyticsInfo( std::string( 64, 'b' ), "1", info ) );
    EXPECT_EQ( 63u, strlen( info.family ) );

    // The same boundary for the form.
    ASSERT_EQ( S_OK, ParseAnalyticsInfo( "a." + std::string( 63, 'c' ), "1", info ) );
    EXPECT_EQ( 63u, strlen( info.form ) );
    ASSERT_EQ( S_OK, ParseAnalyticsInfo( "a." + std::string( 64, 'c' ), "1", info ) );
    EXPECT_EQ( 63u, strlen( info.form ) );
}

TEST_F( XSystemAnalyticsTests, ACutNeverSplitsAUtf8Character )
{
    // 62 ASCII bytes then a 2 byte character: byte 63 would be half of it.
    std::string name = std::string( 62, 'a' ) + "\xC3\xA9" "tail";
    XSystemAnalyticsInfo info = Dirty();
    ASSERT_EQ( S_OK, ParseAnalyticsInfo( name, "1", info ) );
    EXPECT_EQ( 62u, strlen( info.family ) );

    // A 3 byte character straddling the limit by one and by two bytes.
    for ( size_t ascii : { 61u, 62u } )
    {
        SCOPED_TRACE( ascii );
        name = std::string( ascii, 'a' ) + "\xE2\x82\xAC" "tail";
        ASSERT_EQ( S_OK, ParseAnalyticsInfo( name, "1", info ) );
        EXPECT_EQ( ascii, strlen( info.family ) );
    }

    // A character that ends exactly on the limit is kept whole.
    name = std::string( 61, 'a' ) + "\xC3\xA9" "tail";
    ASSERT_EQ( S_OK, ParseAnalyticsInfo( name, "1", info ) );
    EXPECT_EQ( 63u, strlen( info.family ) );

    // The same rule for the form.
    name = "F." + std::string( 62, 'a' ) + "\xC3\xA9";
    ASSERT_EQ( S_OK, ParseAnalyticsInfo( name, "1", info ) );
    EXPECT_EQ( 62u, strlen( info.form ) );

    // A string of nothing but continuation bytes backs off to empty rather than below it.
    ASSERT_EQ( S_OK, ParseAnalyticsInfo( std::string( 70, '\x80' ), "1", info ) );
    EXPECT_EQ( 0u, strlen( info.family ) );
}

// --- FillAnalyticsInfo --- //

TEST_F( XSystemAnalyticsTests, FillUsesTheSource )
{
    XSystemAnalyticsInfo info = Dirty();
    AnalyticsSource source = []( std::string &family, std::string &version )
    {
        family = "Windows.Desktop";
        version = std::to_string( Pack( 10, 0, 22631, 1 ) );
        return S_OK;
    };

    ASSERT_EQ( S_OK, FillAnalyticsInfo( source, info ) );
    EXPECT_STREQ( "Windows", info.family );
    EXPECT_STREQ( "Desktop", info.form );
    EXPECT_EQ( 22631, info.osVersion.build );
}

TEST_F( XSystemAnalyticsTests, FillReturnsTheSourceFailureAndAZeroStruct )
{
    XSystemAnalyticsInfo info = Dirty();
    bool called = false;
    AnalyticsSource source = [&]( std::string &family, std::string &version )
    {
        called = true;
        family = "Windows.Desktop";   // must not leak into the result
        version = "1";
        return HRESULT_FROM_WIN32( ERROR_ACCESS_DENIED );
    };

    EXPECT_EQ( HRESULT_FROM_WIN32( ERROR_ACCESS_DENIED ), FillAnalyticsInfo( source, info ) );
    EXPECT_TRUE( called );
    EXPECT_TRUE( IsZero( info ) );
}

TEST_F( XSystemAnalyticsTests, FillZeroesWhenTheAnswerCannotBeParsed )
{
    XSystemAnalyticsInfo info = Dirty();
    AnalyticsSource source = []( std::string &family, std::string &version )
    {
        family = "Windows.Desktop";
        version = "not a number";
        return S_OK;
    };

    EXPECT_EQ( E_INVALIDARG, FillAnalyticsInfo( source, info ) );
    EXPECT_TRUE( IsZero( info ) );
}

// --- The object, against the real Windows runtime --- //

TEST_F( XSystemAnalyticsTests, ReportsWindowsDesktop )
{
    XSystemAnalyticsInfo info = CallObject();
    EXPECT_STREQ( "Windows", info.family );
    EXPECT_STREQ( "Desktop", info.form );
    EXPECT_GE( info.osVersion.major, 10 );
    for ( size_t i = strlen( info.family ); i < sizeof( info.family ); ++i ) EXPECT_EQ( 0, info.family[i] );
    for ( size_t i = strlen( info.form ); i < sizeof( info.form ); ++i ) EXPECT_EQ( 0, info.form[i] );
    EXPECT_EQ( 0, std::memcmp( &info.osVersion, &info.hostingOsVersion, sizeof( XVersion ) ) );
}

TEST_F( XSystemAnalyticsTests, ThePublicWrapperAgrees )
{
    XSystemAnalyticsInfo viaObject = CallObject();
    XSystemAnalyticsInfo viaWrapper = XSystemGetAnalyticsInfo();
    EXPECT_EQ( 0, std::memcmp( &viaObject, &viaWrapper, sizeof( viaObject ) ) );
}

TEST_F( XSystemAnalyticsTests, RepeatedCallsAreIdentical )
{
    XSystemAnalyticsInfo a = CallObject();
    XSystemAnalyticsInfo b = CallObject();
    EXPECT_EQ( 0, std::memcmp( &a, &b, sizeof( a ) ) );
}

TEST_F( XSystemAnalyticsTests, WorksOnAThreadWithNoApartmentAndLeavesItThatWay )
{
    XSystemAnalyticsInfo info = Dirty();
    APTTYPE type = APTTYPE_CURRENT;
    APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
    HRESULT after = S_OK;

    std::thread( [&]
    {
        info = CallObject();
        after = CoGetApartmentType( &type, &qualifier );
    } ).join();

    EXPECT_STREQ( "Windows", info.family );
    EXPECT_STREQ( "Desktop", info.form );
    EXPECT_EQ( CO_E_NOTINITIALIZED, after );
}

TEST_F( XSystemAnalyticsTests, WorksOnAnStaThreadAndLeavesItSta )
{
    XSystemAnalyticsInfo info = Dirty();
    HRESULT init = E_FAIL, after = E_FAIL;
    APTTYPE type = APTTYPE_CURRENT;
    APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;

    std::thread( [&]
    {
        init = CoInitializeEx( nullptr, COINIT_APARTMENTTHREADED );
        info = CallObject();
        after = CoGetApartmentType( &type, &qualifier );
        CoUninitialize();
    } ).join();

    EXPECT_EQ( S_OK, init );
    EXPECT_STREQ( "Windows", info.family );
    EXPECT_STREQ( "Desktop", info.form );
    EXPECT_EQ( S_OK, after );
    EXPECT_TRUE( type == APTTYPE_STA || type == APTTYPE_MAINSTA );
}

TEST_F( XSystemAnalyticsTests, WorksOnAnMtaThread )
{
    XSystemAnalyticsInfo info = Dirty();
    HRESULT init = E_FAIL;
    HRESULT after = S_OK;

    std::thread( [&]
    {
        init = CoInitializeEx( nullptr, COINIT_MULTITHREADED );
        info = CallObject();
        CoUninitialize();

        // The call must have released its own RoInitialize: one CoUninitialize ends the apartment.
        APTTYPE type;
        APTTYPEQUALIFIER qualifier;
        after = CoGetApartmentType( &type, &qualifier );
    } ).join();

    EXPECT_EQ( S_OK, init );
    EXPECT_STREQ( "Windows", info.family );
    EXPECT_EQ( CO_E_NOTINITIALIZED, after );
}

// --- QueryInterface --- //

TEST_F( XSystemAnalyticsTests, QueryInterfaceServesTheKnownIidsOnly )
{
    IXSystemAnalyticsImpl *impl = nullptr;
    ASSERT_EQ( S_OK, QueryAnalytics( &impl ) );

    for ( const IID *iid : { &__uuidof( IUnknown ), &__uuidof( IInspectable ), &__uuidof( IAgileObject ),
                             &__uuidof( IXSystemAnalyticsImpl ) } )
    {
        void *out = nullptr;
        EXPECT_EQ( S_OK, impl->QueryInterface( *iid, &out ) );
        EXPECT_EQ( static_cast<void *>( impl ), out );
        if ( out ) static_cast<IUnknown *>( out )->Release();
    }

    void *out = reinterpret_cast<void *>( 0x1 );
    EXPECT_EQ( E_NOINTERFACE, impl->QueryInterface( __uuidof( IXSystemImpl ), &out ) );
    EXPECT_EQ( nullptr, out );
    EXPECT_EQ( E_POINTER, impl->QueryInterface( __uuidof( IUnknown ), nullptr ) );

    impl->Release();
}

TEST_F( XSystemAnalyticsTests, ReferenceCountsGoUpAndDown )
{
    IXSystemAnalyticsImpl *impl = nullptr;
    ASSERT_EQ( S_OK, QueryAnalytics( &impl ) );

    ULONG base = impl->Release();
    EXPECT_EQ( base + 1, impl->AddRef() );
    EXPECT_EQ( base, impl->Release() );

    // A successful QueryInterface adds one reference, a refused one none.
    IXSystemAnalyticsImpl *again = nullptr;
    ASSERT_EQ( S_OK, impl->QueryInterface( __uuidof( IXSystemAnalyticsImpl ), reinterpret_cast<void **>( &again ) ) );
    EXPECT_EQ( base + 2, impl->AddRef() );
    EXPECT_EQ( base + 1, impl->Release() );
    EXPECT_EQ( base, again->Release() );

    void *out = nullptr;
    EXPECT_EQ( E_NOINTERFACE, impl->QueryInterface( __uuidof( IXSystemImpl ), &out ) );
    EXPECT_EQ( base + 1, impl->AddRef() );
    EXPECT_EQ( base, impl->Release() );
}
