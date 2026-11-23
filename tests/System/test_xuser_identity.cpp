/*
 * XUser identity Tests
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
#include <string>
#include <thread>

#include <windows.h>
#include <xgameerr.h>
#include <xuser.h>

#include "../../src/System/XUserIdentity.h"
#include "../../src/Xodus/SharedClient.h"

namespace
{
    constexpr HRESULT NO_USER = (HRESULT)0x89245106;
    constexpr HRESULT INVALID_DATA = HRESULT_FROM_WIN32( ERROR_INVALID_DATA );
    constexpr HRESULT INSUFFICIENT = HRESULT_FROM_WIN32( ERROR_INSUFFICIENT_BUFFER );

    Xodus::UserInfoResult SignedIn( const std::string &xuid = "2535405123456789" )
    {
        Xodus::UserInfoResult r;
        r.signedIn = true;
        r.xuid = xuid;
        return r;
    }

    UserRecord Record( UINT64 xuid, const std::string &tag = "Tag" )
    {
        UserRecord r{};
        r.xuid = xuid;
        r.gamertag = tag;
        r.modernGamertag = tag + "M";
        r.modernGamertagSuffix = "1234";
        r.uniqueModernGamertag = tag + "M#1234";
        r.ageGroup = XUserAgeGroup::Adult;
        return r;
    }

    HRESULT Library( const wchar_t *path, DWORD len, DWORD size, std::wstring &out )
    {
        return Xodus::XodusLibraryPathFromModulePath( path, len, size, out );
    }
}

TEST( XUserIdentityLibraryPath, ReplacesFileNameInSameDirectory )
{
    const wchar_t path[] = L"C:\\a\\b\\xgameruntime.dll";
    std::wstring so;
    ASSERT_EQ( S_OK, Library( path, (DWORD)wcslen( path ), 260, so ) );
    EXPECT_EQ( L"C:\\a\\b\\xgameruntime-unix.so", so );
}

TEST( XUserIdentityLibraryPath, ZeroLengthIsError )
{
    std::wstring so;
    EXPECT_TRUE( FAILED( Library( L"C:\\a\\x.dll", 0, 260, so ) ) );
}

TEST( XUserIdentityLibraryPath, TruncatedIsError )
{
    const wchar_t path[] = L"C:\\a\\b\\xgameruntime.dll";
    std::wstring so;
    EXPECT_TRUE( FAILED( Library( path, 260, 260, so ) ) );
}

TEST( XUserIdentityLibraryPath, NoSeparatorIsError )
{
    const wchar_t path[] = L"xgameruntime.dll";
    std::wstring so;
    EXPECT_TRUE( FAILED( Library( path, (DWORD)wcslen( path ), 260, so ) ) );
}

TEST( XUserIdentityQuery, BothPresent )
{
    UserInfoQuery q = BuildUserInfoQuery( S_OK, 0x1234, S_OK, "app" );
    EXPECT_EQ( "app", q.clientId );
    EXPECT_EQ( 0x1234u, q.titleId );
}

TEST( XUserIdentityQuery, OnlyTitleId )
{
    UserInfoQuery q = BuildUserInfoQuery( S_OK, 0x1234, E_FAIL, "ignored" );
    EXPECT_EQ( "", q.clientId );
    EXPECT_EQ( 0x1234u, q.titleId );
}

TEST( XUserIdentityQuery, OnlyAppId )
{
    UserInfoQuery q = BuildUserInfoQuery( E_FAIL, 99, S_OK, "app" );
    EXPECT_EQ( "app", q.clientId );
    EXPECT_EQ( 0u, q.titleId );
}

TEST( XUserIdentityQuery, Neither )
{
    UserInfoQuery q = BuildUserInfoQuery( E_FAIL, 99, E_FAIL, "ignored" );
    EXPECT_EQ( "", q.clientId );
    EXPECT_EQ( 0u, q.titleId );
}

TEST( XUserIdentityMapping, SignedOut )
{
    Xodus::UserInfoResult r = SignedIn();
    r.signedIn = false;
    UserRecord u;
    EXPECT_EQ( NO_USER, IdentityFromUserInfo( r, u ) );
}

TEST( XUserIdentityMapping, ErrorCodePassesThrough )
{
    Xodus::UserInfoResult r = SignedIn();
    r.errorCode = "0x89245102";
    UserRecord u;
    EXPECT_EQ( (HRESULT)0x89245102, IdentityFromUserInfo( r, u ) );
}

/* The client parses a service error reply with signedIn left false. */
TEST( XUserIdentityMapping, ErrorReplyWithSignedInFalsePassesThrough )
{
    Xodus::UserInfoResult r;
    r.errorCode = "0x89245102";
    UserRecord u;
    EXPECT_EQ( (HRESULT)0x89245102, IdentityFromUserInfo( r, u ) );
}

TEST( XUserIdentityMapping, BadErrorCodeIsFail )
{
    for ( const char *code : { "0xZZ", "0x00000000", "0x00000001", "89245102", "0x", "0x123456789" } )
    {
        Xodus::UserInfoResult r = SignedIn();
        r.errorCode = code;
        UserRecord u;
        EXPECT_EQ( E_FAIL, IdentityFromUserInfo( r, u ) ) << code;
    }
}

TEST( XUserIdentityMapping, ParsesXuidAsDecimal )
{
    UserRecord u{};
    ASSERT_EQ( S_OK, IdentityFromUserInfo( SignedIn(), u ) );
    EXPECT_EQ( 2535405123456789ull, u.xuid );
}

TEST( XUserIdentityMapping, BadXuidIsInvalidData )
{
    for ( const char *xuid : { "", "abc", "0", "99999999999999999999", "-1", "+5", " 5", "5 " } )
    {
        UserRecord u;
        EXPECT_EQ( INVALID_DATA, IdentityFromUserInfo( SignedIn( xuid ), u ) ) << "[" << xuid << "]";
    }
}

TEST( XUserIdentityMapping, MaxXuidParses )
{
    UserRecord u{};
    ASSERT_EQ( S_OK, IdentityFromUserInfo( SignedIn( "18446744073709551615" ), u ) );
    EXPECT_EQ( UINT64_MAX, u.xuid );
}

TEST( XUserIdentityMapping, AgeGroups )
{
    struct { Xodus::AgeGroup in; XUserAgeGroup out; } cases[] = {
        { Xodus::AgeGroup::Unknown, XUserAgeGroup::Unknown },
        { Xodus::AgeGroup::Child, XUserAgeGroup::Child },
        { Xodus::AgeGroup::Teen, XUserAgeGroup::Teen },
        { Xodus::AgeGroup::Adult, XUserAgeGroup::Adult },
    };
    for ( auto &c : cases )
    {
        Xodus::UserInfoResult r = SignedIn();
        r.ageGroup = c.in;
        UserRecord u{};
        ASSERT_EQ( S_OK, IdentityFromUserInfo( r, u ) );
        EXPECT_EQ( c.out, u.ageGroup );
    }
}

TEST( XUserIdentityMapping, CopiesGamertags )
{
    Xodus::UserInfoResult r = SignedIn();
    r.gamertag = "g";
    r.modernGamertag = "m";
    r.modernGamertagSuffix = "s";
    r.uniqueModernGamertag = "u";
    UserRecord u{};
    ASSERT_EQ( S_OK, IdentityFromUserInfo( r, u ) );
    EXPECT_EQ( "g", u.gamertag );
    EXPECT_EQ( "m", u.modernGamertag );
    EXPECT_EQ( "s", u.modernGamertagSuffix );
    EXPECT_EQ( "u", u.uniqueModernGamertag );
}

TEST( XUserIdentityMapping, AbsentGamertagsAreEmpty )
{
    UserRecord u = Record( 1 );
    ASSERT_EQ( S_OK, IdentityFromUserInfo( SignedIn(), u ) );
    EXPECT_TRUE( u.gamertag.empty() && u.modernGamertag.empty() && u.modernGamertagSuffix.empty() && u.uniqueModernGamertag.empty() );
}

TEST( XUserIdentityTransport, OkIsNotFailure )
{
    EXPECT_FALSE( UserInfoIsTransportFailure( S_OK, SignedIn() ) );
}

TEST( XUserIdentityTransport, FailWithErrorCodeIsServiceReply )
{
    Xodus::UserInfoResult r;
    r.errorCode = "0x87dd0006";
    EXPECT_FALSE( UserInfoIsTransportFailure( E_FAIL, r ) );
}

TEST( XUserIdentityTransport, FailWithoutErrorCodeIsTransport )
{
    EXPECT_TRUE( UserInfoIsTransportFailure( E_FAIL, Xodus::UserInfoResult() ) );
    EXPECT_TRUE( UserInfoIsTransportFailure( E_OUTOFMEMORY, Xodus::UserInfoResult() ) );
}

TEST( XUserIdentityGamertag, EachComponent )
{
    UserRecord u{};
    u.gamertag = "classic";
    u.modernGamertag = "modern";
    u.modernGamertagSuffix = "1234";
    u.uniqueModernGamertag = "modern#1234";

    struct { XUserGamertagComponent c; const char *expect; } cases[] = {
        { XUserGamertagComponent::Classic, "classic" },
        { XUserGamertagComponent::Modern, "modern" },
        { XUserGamertagComponent::ModernSuffix, "1234" },
        { XUserGamertagComponent::UniqueModern, "modern#1234" },
    };
    for ( auto &c : cases )
    {
        char buf[32];
        SIZE_T used = 0;
        ASSERT_EQ( S_OK, GamertagComponent( u, c.c, sizeof( buf ), buf, &used ) );
        EXPECT_STREQ( c.expect, buf );
        EXPECT_EQ( strlen( c.expect ) + 1, used );
    }
}

TEST( XUserIdentityGamertag, ExactFitCopiesTerminator )
{
    UserRecord u{};
    u.gamertag = "abc";
    char buf[4];
    memset( buf, 'x', sizeof( buf ) );
    SIZE_T used = 0;
    ASSERT_EQ( S_OK, GamertagComponent( u, XUserGamertagComponent::Classic, 4, buf, &used ) );
    EXPECT_EQ( 4u, used );
    EXPECT_STREQ( "abc", buf );
}

TEST( XUserIdentityGamertag, OneShortReportsSizeAndLeavesBuffer )
{
    UserRecord u{};
    u.gamertag = "abc";
    char buf[4];
    memset( buf, 'x', sizeof( buf ) );
    SIZE_T used = 0;
    EXPECT_EQ( INSUFFICIENT, GamertagComponent( u, XUserGamertagComponent::Classic, 3, buf, &used ) );
    EXPECT_EQ( 4u, used );
    EXPECT_EQ( 0, memcmp( buf, "xxxx", 4 ) );
}

TEST( XUserIdentityGamertag, NullBufferSizeZero )
{
    UserRecord u{};
    u.gamertag = "abc";
    SIZE_T used = 0;
    EXPECT_EQ( INSUFFICIENT, GamertagComponent( u, XUserGamertagComponent::Classic, 0, nullptr, &used ) );
    EXPECT_EQ( 4u, used );
}

TEST( XUserIdentityGamertag, UnknownComponent )
{
    UserRecord u{};
    char buf[8];
    SIZE_T used = 77;
    EXPECT_EQ( E_INVALIDARG, GamertagComponent( u, (XUserGamertagComponent)9, sizeof( buf ), buf, &used ) );
    EXPECT_EQ( 77u, used );
}

/* g_user is process-wide and never freed; each test works against whatever the previous left. */
TEST( XUserIdentityUser, CreateOrRefreshReturnsSingleObjectWithIdentity )
{
    XUser *a = UserCreateOrRefresh( Record( 111, "A" ) );
    ASSERT_NE( nullptr, a );
    EXPECT_TRUE( UserIsValid( a ) );
    EXPECT_EQ( 1u, a->localId.value );

    LONG before = a->refs.load();
    XUser *b = UserCreateOrRefresh( Record( 222, "B" ) );
    EXPECT_EQ( a, b );
    EXPECT_EQ( before + 1, a->refs.load() );

    UserRecord s{};
    UserSnapshot( a, s );
    EXPECT_EQ( 222u, s.xuid );
    EXPECT_EQ( "B", s.gamertag );
    EXPECT_EQ( "BM", s.modernGamertag );
    EXPECT_EQ( "1234", s.modernGamertagSuffix );
    EXPECT_EQ( "BM#1234", s.uniqueModernGamertag );
    EXPECT_EQ( XUserAgeGroup::Adult, s.ageGroup );

    UserRelease( a );
    UserRelease( b );
}

TEST( XUserIdentityUser, TwoThreadsGetSamePointer )
{
    XUser *base = UserCreateOrRefresh( Record( 5 ) );
    UserRelease( base );
    LONG before = base->refs.load();

    XUser *got[2] = {};
    std::thread t0( [&] { got[0] = UserCreateOrRefresh( Record( 5 ) ); } );
    std::thread t1( [&] { got[1] = UserCreateOrRefresh( Record( 5 ) ); } );
    t0.join();
    t1.join();

    EXPECT_EQ( base, got[0] );
    EXPECT_EQ( base, got[1] );
    EXPECT_EQ( before + 2, base->refs.load() );
    UserRelease( got[0] );
    UserRelease( got[1] );
}

TEST( XUserIdentityUser, FindByIdMatchAndMiss )
{
    XUser *u = UserCreateOrRefresh( Record( 777 ) );
    LONG before = u->refs.load();

    XUser *hit = UserFindById( 777 );
    EXPECT_EQ( u, hit );
    EXPECT_EQ( before + 1, u->refs.load() );
    UserRelease( hit );

    EXPECT_EQ( nullptr, UserFindById( 778 ) );
    EXPECT_EQ( before, u->refs.load() );
    UserRelease( u );
}

TEST( XUserIdentityUser, FindByLocalIdMatchAndMiss )
{
    XUser *u = UserCreateOrRefresh( Record( 777 ) );
    LONG before = u->refs.load();

    XUser *hit = UserFindByLocalId( XUserLocalId{ 1 } );
    EXPECT_EQ( u, hit );
    EXPECT_EQ( before + 1, u->refs.load() );
    UserRelease( hit );

    EXPECT_EQ( nullptr, UserFindByLocalId( XUserLocalId{ 2 } ) );
    EXPECT_EQ( before, u->refs.load() );
    UserRelease( u );
}

TEST( XUserIdentityUser, SnapshotNeverTearsAgainstRefresh )
{
    XUser *u = UserCreateOrRefresh( Record( 1, "one" ) );
    std::atomic<bool> stop{ false };

    std::thread writer( [&] {
        for ( int i = 0; !stop; i++ )
            UserRelease( UserCreateOrRefresh( i & 1 ? Record( 1, "one" ) : Record( 2, "two" ) ) );
    } );

    for ( int i = 0; i < 20000; i++ )
    {
        UserRecord s{};
        UserSnapshot( u, s );
        bool one = s.xuid == 1 && s.gamertag == "one" && s.modernGamertag == "oneM";
        bool two = s.xuid == 2 && s.gamertag == "two" && s.modernGamertag == "twoM";
        ASSERT_TRUE( one || two ) << s.xuid << " " << s.gamertag << " " << s.modernGamertag;
    }

    stop = true;
    writer.join();
    UserRelease( u );
}
