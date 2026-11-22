/*
 * XGame Tests
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

#include <gtest/gtest.h>

#include <windows.h>
#include <private/logging.h>
#include <xgameerr.h>

#include "private.h"
#include "GameConfig.h"

namespace
{
    constexpr UINT32 kSentinel = 0xDEADBEEF;
    constexpr UINT32 kMinecraft = 896928775; /* 0x35760C07 */
    constexpr const char *kInline = "<Game><TitleId>35760C07</TitleId></Game>";
}

class XGameObject : public ::testing::Test
{
protected:
    void TearDown() override { GameConfig::SetSource( nullptr, false ); }
};

TEST_F( XGameObject, QueryInterfaceReturnsTheSameObjectForEverySupportedIid )
{
    const IID *iids[] = { &IID_IUnknown, &IID_IXGameImpl, &IID_IXGameImpl2, &IID_IXGameImpl3 };
    void *first = nullptr;
    for ( const IID *iid : iids )
    {
        void *out = nullptr;
        ULONG before = x_game_impl->AddRef() - 1;
        x_game_impl->Release();
        ASSERT_EQ( x_game_impl->QueryInterface( *iid, &out ), S_OK );
        ASSERT_NE( out, nullptr );
        if ( !first ) first = out;
        /* Same identity for every interface, and the reference returned is counted. */
        EXPECT_EQ( out, first );
        ULONG during = x_game_impl->AddRef() - 1;
        x_game_impl->Release();
        EXPECT_EQ( during, before + 1 );
        static_cast<IUnknown *>( out )->Release();
    }
}

TEST_F( XGameObject, QueryInterfaceRejectsUnknownIidAndNullOutput )
{
    void *out = reinterpret_cast<void *>( 0x1 );
    EXPECT_EQ( x_game_impl->QueryInterface( IID_IXThreadingImpl, &out ), E_NOINTERFACE );
    EXPECT_EQ( out, nullptr );
    EXPECT_EQ( x_game_impl->QueryInterface( IID_IXGameImpl, nullptr ), E_POINTER );
}

TEST_F( XGameObject, ReferenceCountsBalance )
{
    ULONG before = x_game_impl->AddRef();
    EXPECT_EQ( x_game_impl->AddRef(), before + 1 );
    EXPECT_EQ( x_game_impl->Release(), before );
    EXPECT_EQ( x_game_impl->Release(), before - 1 );
}

TEST_F( XGameObject, TitleIdIsReturnedAsHex )
{
    GameConfig::SetSource( kInline, true );
    UINT32 id = kSentinel;
    ASSERT_EQ( x_game_impl->XGameGetXboxTitleId( &id ), S_OK );
    EXPECT_EQ( id, kMinecraft );

    UINT32 again = kSentinel;
    ASSERT_EQ( x_game_impl->XGameGetXboxTitleId( &again ), S_OK );
    EXPECT_EQ( again, id ); /* the second call returns the same value */
}

TEST_F( XGameObject, NullOutputIsInvalidArgument )
{
    GameConfig::SetSource( kInline, true );
    EXPECT_EQ( x_game_impl->XGameGetXboxTitleId( nullptr ), E_INVALIDARG );
}

TEST_F( XGameObject, BadConfigIsAnErrorAndLeavesTheOutputUntouched )
{
    GameConfig::SetSource( "<Game><TitleId>nothex</TitleId></Game>", true );
    UINT32 id = kSentinel;
    EXPECT_EQ( x_game_impl->XGameGetXboxTitleId( &id ), E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT );
    EXPECT_EQ( id, kSentinel );
}

TEST_F( XGameObject, MissingConfigIsAnError )
{
    GameConfig::SetSource( "C:\\xgame-no-such-dir\\MicrosoftGame.config", false );
    UINT32 id = kSentinel;
    EXPECT_EQ( x_game_impl->XGameGetXboxTitleId( &id ), E_GAME_MISSING_GAME_CONFIG );
    EXPECT_EQ( id, kSentinel );
}

TEST_F( XGameObject, LaunchEntryPointsAreStubs )
{
    EXPECT_EQ( x_game_impl->XLaunchRestartOnCrash( nullptr, 0 ), E_NOTIMPL );
    x_game_impl->XLaunchNewGame( nullptr, nullptr, nullptr ); /* must not crash */
}
