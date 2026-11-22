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

#include "../test_fixtures.h"

#include <windows.h>
#include <xgame.h>
#include <xgameerr.h>
#include <xgameruntimeinit.h>

#include <string>

namespace
{
    constexpr UINT32 kSentinel = 0xDEADBEEF;
    constexpr UINT32 kMinecraft = 896928775; /* 0x35760C07 */
}

/* Goes through the real DLL: wrapper -> QueryApiImpl -> IXGameImpl. The default
 * lookup finds the MicrosoftGame.config copied next to the test executable. */
class XGameIntegration : public XGameRuntimeTests
{
protected:
    void TearDown() override
    {
        /* Restore the default lookup so no test leaks its source into the next. */
        XGameRuntimeOptions options{ XGameRuntimeGameConfigSource::Default, nullptr };
        XGameRuntimeInitializeWithOptions( &options );
        XGameRuntimeUninitialize();
    }

    static HRESULT Init( XGameRuntimeGameConfigSource source, const char *config )
    {
        XGameRuntimeOptions options{ source, config };
        return XGameRuntimeInitializeWithOptions( &options );
    }

    static std::string FixturePath()
    {
        char path[MAX_PATH];
        GetModuleFileNameA( nullptr, path, MAX_PATH );
        std::string dir( path );
        return dir.substr( 0, dir.find_last_of( "\\/" ) + 1 ) + "MicrosoftGame.config";
    }
};

TEST_F( XGameIntegration, DefaultLookupFindsTheConfigBesideTheExecutable )
{
    ASSERT_EQ( XGameRuntimeInitialize(), S_OK );
    UINT32 id = kSentinel;
    ASSERT_EQ( XGameGetXboxTitleId( &id ), S_OK );
    EXPECT_EQ( id, kMinecraft );
}

TEST_F( XGameIntegration, InlineConfigOverridesTheFile )
{
    ASSERT_EQ( Init( XGameRuntimeGameConfigSource::Inline, "<Game><TitleId>0000ABCD</TitleId></Game>" ), S_OK );
    UINT32 id = kSentinel;
    ASSERT_EQ( XGameGetXboxTitleId( &id ), S_OK );
    EXPECT_EQ( id, 0xABCDu );
}

TEST_F( XGameIntegration, FileSourceReadsThePathItIsGiven )
{
    ASSERT_EQ( Init( XGameRuntimeGameConfigSource::File, FixturePath().c_str() ), S_OK );
    UINT32 id = kSentinel;
    ASSERT_EQ( XGameGetXboxTitleId( &id ), S_OK );
    EXPECT_EQ( id, kMinecraft );
}

TEST_F( XGameIntegration, ReinitializingWithDefaultRestoresTheLookup )
{
    ASSERT_EQ( Init( XGameRuntimeGameConfigSource::Inline, "<Game><TitleId>0000ABCD</TitleId></Game>" ), S_OK );
    UINT32 id = kSentinel;
    ASSERT_EQ( XGameGetXboxTitleId( &id ), S_OK );
    ASSERT_EQ( id, 0xABCDu );

    ASSERT_EQ( Init( XGameRuntimeGameConfigSource::Default, nullptr ), S_OK );
    ASSERT_EQ( XGameGetXboxTitleId( &id ), S_OK );
    EXPECT_EQ( id, kMinecraft );
}

TEST_F( XGameIntegration, NullOptionsLeaveTheSourceUnchanged )
{
    ASSERT_EQ( Init( XGameRuntimeGameConfigSource::Inline, "<Game><TitleId>0000ABCD</TitleId></Game>" ), S_OK );
    ASSERT_EQ( XGameRuntimeInitializeWithOptions( nullptr ), S_OK );
    UINT32 id = kSentinel;
    ASSERT_EQ( XGameGetXboxTitleId( &id ), S_OK );
    EXPECT_EQ( id, 0xABCDu );
}

TEST_F( XGameIntegration, MalformedInlineConfigIsBadFormat )
{
    ASSERT_EQ( Init( XGameRuntimeGameConfigSource::Inline, "<Game><TitleId>nothex</TitleId></Game>" ), S_OK );
    UINT32 id = kSentinel;
    EXPECT_EQ( XGameGetXboxTitleId( &id ), E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT );
    EXPECT_EQ( id, kSentinel );
}

TEST_F( XGameIntegration, MissingFileSourceIsMissingGameConfig )
{
    ASSERT_EQ( Init( XGameRuntimeGameConfigSource::File, "C:\\xgame-no-such-dir\\MicrosoftGame.config" ), S_OK );
    UINT32 id = kSentinel;
    EXPECT_EQ( XGameGetXboxTitleId( &id ), E_GAME_MISSING_GAME_CONFIG );
    EXPECT_EQ( id, kSentinel );
}

TEST_F( XGameIntegration, NullOutputIsInvalidArgument )
{
    ASSERT_EQ( XGameRuntimeInitialize(), S_OK );
    EXPECT_EQ( XGameGetXboxTitleId( nullptr ), E_INVALIDARG );
}
