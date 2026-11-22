/*
 * GameConfig Tests
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
#include <xgameerr.h>

#include "GameConfig.h"

#include <fstream>
#include <string>

namespace
{
    constexpr UINT32 kSentinel = 0xDEADBEEF;
    constexpr UINT32 kMinecraft = 896928775; /* 0x35760C07 */

    std::string Config( const std::string &titleIdElement )
    {
        return "<?xml version=\"1.0\" encoding=\"utf-8\"?><Game configVersion=\"1\">" + titleIdElement + "</Game>";
    }

    std::string Config( const char *titleIdText )
    {
        return Config( std::string( "<TitleId>" ) + titleIdText + "</TitleId>" );
    }

    HRESULT Parse( const std::string &xml, UINT32 *id )
    {
        return GameConfig::ParseTitleId( xml.data(), xml.size(), id );
    }

    void ExpectBad( const std::string &xml )
    {
        UINT32 id = kSentinel;
        EXPECT_EQ( Parse( xml, &id ), E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT ) << xml.substr( 0, 120 );
        EXPECT_EQ( id, kSentinel );
    }

    void ExpectId( const std::string &xml, UINT32 expected )
    {
        UINT32 id = kSentinel;
        EXPECT_EQ( Parse( xml, &id ), S_OK ) << xml.substr( 0, 120 );
        EXPECT_EQ( id, expected );
    }
}

TEST( GameConfigParse, MinecraftTitleIdIsHex )
{
    /* WineGDK read this as base 10 and got 35760. */
    ExpectId( Config( "35760C07" ), kMinecraft );
}

TEST( GameConfigParse, LowercaseAndBoundaryValues )
{
    ExpectId( Config( "35760c07" ), kMinecraft );
    ExpectId( Config( "00000001" ), 1 );
    ExpectId( Config( "1" ), 1 );
    ExpectId( Config( "FFFFFFFF" ), 0xFFFFFFFFu );
    ExpectId( Config( "abcdef12" ), 0xABCDEF12u );
}

TEST( GameConfigParse, SurroundingWhitespaceIsIgnored )
{
    ExpectId( Config( "  \r\n\t35760C07 \n" ), kMinecraft );
    ExpectId( Config( ( std::string( 100, ' ' ) + "35760C07" ).c_str() ), kMinecraft );
}

TEST( GameConfigParse, CdataValueAndSplitTextAreJoined )
{
    ExpectId( Config( std::string( "<TitleId><![CDATA[35760C07]]></TitleId>" ) ), kMinecraft );
    ExpectId( Config( std::string( "<TitleId>3576<![CDATA[0C]]>07</TitleId>" ) ), kMinecraft );
    /* A comment inside the element contributes no text. */
    ExpectId( Config( std::string( "<TitleId>3576<!-- note -->0C07</TitleId>" ) ), kMinecraft );
}

TEST( GameConfigParse, CommentsAndOtherElementsAreSkipped )
{
    ExpectId( Config( std::string( "<!-- <TitleId>00000001</TitleId> -->"
                                   "<Identity Name=\"x\"><TitleId>00000002</TitleId></Identity>"
                                   "<TitleId>35760C07</TitleId>"
                                   "<MSAAppId>0000000040159362</MSAAppId>" ) ),
              kMinecraft );
    /* Self-closing siblings must not disturb the depth tracking. */
    ExpectId( Config( std::string( "<MSAAppId/><Identity/><TitleId>35760C07</TitleId>" ) ), kMinecraft );
}

TEST( GameConfigParse, TitleIdOnlyCountsAsDirectChildOfRoot )
{
    ExpectBad( Config( std::string( "<Identity><TitleId>35760C07</TitleId></Identity>" ) ) );
    ExpectBad( Config( std::string( "<!-- <TitleId>35760C07</TitleId> -->" ) ) );
}

TEST( GameConfigParse, DuplicateTitleIdFirstWins )
{
    ExpectId( Config( std::string( "<TitleId>35760C07</TitleId><TitleId>00000001</TitleId>" ) ), kMinecraft );
    /* The first one decides, even when it is the bad one. */
    ExpectBad( Config( std::string( "<TitleId>zz</TitleId><TitleId>35760C07</TitleId>" ) ) );
}

TEST( GameConfigParse, Utf8ByteOrderMarkIsAccepted )
{
    ExpectId( "\xEF\xBB\xBF" + Config( "35760C07" ), kMinecraft );
}

TEST( GameConfigParse, EmptyAndSelfClosingTitleIdAreBad )
{
    ExpectBad( Config( std::string( "<TitleId/>" ) ) );
    ExpectBad( Config( std::string( "<TitleId></TitleId>" ) ) );
    ExpectBad( Config( "   " ) );
}

TEST( GameConfigParse, TitleIdWithChildElementIsBad )
{
    ExpectBad( Config( std::string( "<TitleId>3576<x/>0C07</TitleId>" ) ) );
    ExpectBad( Config( std::string( "<TitleId><x>35760C07</x></TitleId>" ) ) );
}

TEST( GameConfigParse, NonHexAndOverlongValuesAreBad )
{
    ExpectBad( Config( "0x35760C07" ) );
    ExpectBad( Config( "-1" ) );
    ExpectBad( Config( "+1" ) );
    ExpectBad( Config( "3576 0C07" ) );
    ExpectBad( Config( "35760G07" ) );
    ExpectBad( Config( "035760C07" ) ); /* 9 digits */
    ExpectBad( Config( "FFFFFFFFF" ) );
    ExpectBad( Config( "35760C0g" ) );
    ExpectBad( Config( "\xEF\xBC\x93" ) ); /* full-width digit */
}

TEST( GameConfigParse, WrongRootOrMissingElementIsBad )
{
    ExpectBad( "<Other><TitleId>35760C07</TitleId></Other>" );
    ExpectBad( "<Game configVersion=\"1\"><MSAAppId>1</MSAAppId></Game>" );
    ExpectBad( "<Game/>" );
    ExpectBad( "<game><TitleId>35760C07</TitleId></game>" ); /* element names are case-sensitive */
}

TEST( GameConfigParse, EmptyInputIsBad )
{
    UINT32 id = kSentinel;
    EXPECT_EQ( GameConfig::ParseTitleId( "", 0, &id ), E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT );
    EXPECT_EQ( GameConfig::ParseTitleId( nullptr, 0, &id ), E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT );
    EXPECT_EQ( id, kSentinel );
}

TEST( GameConfigParse, MalformedDocumentsAreBad )
{
    ExpectBad( "<Game><TitleId>35760C07</Game>" );             /* mismatched */
    ExpectBad( "<Game><TitleId>35760C07" );                    /* truncated */
    ExpectBad( "not xml at all" );
    ExpectBad( Config( "35760C07" ) + "garbage" );             /* trailing content */
    ExpectBad( Config( "35760C07" ) + "<Game/>" );             /* second root */
}

TEST( GameConfigParse, DoctypeIsProhibited )
{
    ExpectBad( "<?xml version=\"1.0\"?><!DOCTYPE Game [<!ENTITY e \"35760C07\">]><Game><TitleId>&e;</TitleId></Game>" );
    ExpectBad( "<!DOCTYPE Game><Game><TitleId>35760C07</TitleId></Game>" );
}

TEST( GameConfigParse, SizeLimitIsSixtyFourKibibytes )
{
    const size_t limit = 64 * 1024;
    std::string base = Config( "35760C07" );
    std::string padded = base.substr( 0, base.size() - 7 ) + "<!--";
    std::string tail = "--></Game>";
    padded += std::string( limit - padded.size() - tail.size(), 'x' ) + tail;
    ASSERT_EQ( padded.size(), limit );
    ExpectId( padded, kMinecraft );

    padded.insert( padded.size() - tail.size(), "x" );
    ASSERT_EQ( padded.size(), limit + 1 );
    ExpectBad( padded );
}

namespace
{
    const std::string kAppIdSentinel = "sentinel";

    void ExpectMsaBad( const std::string &xml )
    {
        std::string id = kAppIdSentinel;
        EXPECT_EQ( GameConfig::ParseMsaAppId( xml.data(), xml.size(), id ), E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT )
            << xml.substr( 0, 120 );
        EXPECT_EQ( id, kAppIdSentinel );
    }

    void ExpectMsaId( const std::string &xml, const std::string &expected )
    {
        std::string id = kAppIdSentinel;
        EXPECT_EQ( GameConfig::ParseMsaAppId( xml.data(), xml.size(), id ), S_OK ) << xml.substr( 0, 120 );
        EXPECT_EQ( id, expected );
    }
}

TEST( GameConfigMsaAppId, ValueIsReturnedVerbatimAndTrimmed )
{
    ExpectMsaId( Config( std::string( "<MSAAppId>0000000040159362</MSAAppId>" ) ), "0000000040159362" );
    ExpectMsaId( Config( std::string( "<MSAAppId> \r\n\t0000000040159362 \n</MSAAppId>" ) ), "0000000040159362" );
    ExpectMsaId( Config( std::string( "<MSAAppId><![CDATA[0000000040159362]]></MSAAppId>" ) ), "0000000040159362" );
    ExpectMsaId( Config( std::string( "<TitleId>35760C07</TitleId><MSAAppId>0000000040159362</MSAAppId>" ) ), "0000000040159362" );
}

TEST( GameConfigMsaAppId, FirstWinsAndNonAsciiIsUtf8 )
{
    ExpectMsaId( Config( std::string( "<MSAAppId>first</MSAAppId><MSAAppId>second</MSAAppId>" ) ), "first" );
    ExpectMsaId( Config( std::string( "<MSAAppId>\xC3\xA9</MSAAppId>" ) ), "\xC3\xA9" );
}

TEST( GameConfigMsaAppId, MissingEmptyOrMisplacedIsBad )
{
    ExpectMsaBad( Config( std::string( "<TitleId>35760C07</TitleId>" ) ) );
    ExpectMsaBad( Config( std::string( "<MSAAppId/>" ) ) );
    ExpectMsaBad( Config( std::string( "<MSAAppId></MSAAppId>" ) ) );
    ExpectMsaBad( Config( std::string( "<MSAAppId>  \r\n </MSAAppId>" ) ) );
    ExpectMsaBad( Config( std::string( "<MSAAppId>12<x/>34</MSAAppId>" ) ) );
    ExpectMsaBad( Config( std::string( "<Identity><MSAAppId>1</MSAAppId></Identity>" ) ) );
    ExpectMsaBad( Config( std::string( "<msaappid>1</msaappid>" ) ) );
}

TEST( GameConfigMsaAppId, MalformedOversizedAndDoctypeAreBad )
{
    ExpectMsaBad( "" );
    ExpectMsaBad( "<Game><MSAAppId>1</Game>" );
    ExpectMsaBad( Config( std::string( "<MSAAppId>1</MSAAppId>" ) ) + "garbage" );
    ExpectMsaBad( "<!DOCTYPE Game [<!ENTITY e \"1\">]><Game><MSAAppId>&e;</MSAAppId></Game>" );
    std::string big = Config( std::string( "<MSAAppId>1</MSAAppId>" ) );
    big += std::string( 64 * 1024 + 1 - big.size(), ' ' );
    ExpectMsaBad( big );
}

namespace
{
    class TempTree
    {
    public:
        TempTree()
        {
            wchar_t tmp[MAX_PATH];
            GetTempPathW( MAX_PATH, tmp );
            root = std::wstring( tmp ) + L"xgame-config-" + std::to_wstring( GetCurrentProcessId() );
            Mkdir( root );
            a = root + L"\\a";
            b = a + L"\\b";
            c = b + L"\\c";
            Mkdir( a );
            Mkdir( b );
            Mkdir( c );
        }

        ~TempTree() { Remove( root ); }

        static void Mkdir( const std::wstring &p ) { CreateDirectoryW( p.c_str(), nullptr ); }

        static void Write( const std::wstring &path, const std::string &content )
        {
            std::ofstream f( std::string( path.begin(), path.end() ), std::ios::binary );
            f << content;
        }

        static void Remove( const std::wstring &dir )
        {
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW( ( dir + L"\\*" ).c_str(), &fd );
            if ( h != INVALID_HANDLE_VALUE )
            {
                do
                {
                    std::wstring name = fd.cFileName;
                    if ( name == L"." || name == L".." ) continue;
                    std::wstring full = dir + L"\\" + name;
                    if ( fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
                        Remove( full );
                    else
                        DeleteFileW( full.c_str() );
                } while ( FindNextFileW( h, &fd ) );
                FindClose( h );
            }
            RemoveDirectoryW( dir.c_str() );
        }

        std::wstring root, a, b, c;
    };

    std::string Narrow( const std::wstring &w )
    {
        return std::string( w.begin(), w.end() ); /* temp paths are ASCII */
    }
}

class GameConfigFind : public ::testing::Test
{
protected:
    TempTree tree;
};

TEST_F( GameConfigFind, FoundInTheStartDirectoryAndEachParent )
{
    const std::wstring name = L"\\MicrosoftGame.config";
    for ( const std::wstring *dir : { &tree.c, &tree.b, &tree.a, &tree.root } )
    {
        TempTree::Write( *dir + name, Config( "35760C07" ) );
        std::wstring path;
        EXPECT_EQ( GameConfig::FindConfigFile( tree.c, path ), S_OK );
        EXPECT_EQ( path, *dir + name );
        DeleteFileW( ( *dir + name ).c_str() );
    }
}

TEST_F( GameConfigFind, FoundAtTheDriveRoot )
{
    /* Walk-up includes the drive root itself (REQ-002). */
    const std::wstring rootConfig = tree.root.substr( 0, 2 ) + L"\\MicrosoftGame.config";
    if ( GetFileAttributesW( rootConfig.c_str() ) != INVALID_FILE_ATTRIBUTES )
        GTEST_SKIP() << "a config already exists at the drive root";
    TempTree::Write( rootConfig, "x" );
    if ( GetFileAttributesW( rootConfig.c_str() ) == INVALID_FILE_ATTRIBUTES )
        GTEST_SKIP() << "cannot create a file at the drive root";

    std::wstring path;
    HRESULT hr = GameConfig::FindConfigFile( tree.c, path );
    DeleteFileW( rootConfig.c_str() );
    EXPECT_EQ( hr, S_OK );
    EXPECT_EQ( path, rootConfig );
}

TEST_F( GameConfigFind, NearestWins )
{
    TempTree::Write( tree.a + L"\\MicrosoftGame.config", "far" );
    TempTree::Write( tree.b + L"\\MicrosoftGame.config", "near" );
    std::wstring path;
    EXPECT_EQ( GameConfig::FindConfigFile( tree.c, path ), S_OK );
    EXPECT_EQ( path, tree.b + L"\\MicrosoftGame.config" );
}

TEST_F( GameConfigFind, TrailingSeparatorOnTheStartDirectoryIsFine )
{
    TempTree::Write( tree.c + L"\\MicrosoftGame.config", "x" );
    std::wstring path;
    EXPECT_EQ( GameConfig::FindConfigFile( tree.c + L"\\", path ), S_OK );
    EXPECT_EQ( path, tree.c + L"\\MicrosoftGame.config" );
}

TEST_F( GameConfigFind, DirectoryWithTheNameIsSkipped )
{
    TempTree::Mkdir( tree.c + L"\\MicrosoftGame.config" );
    TempTree::Write( tree.a + L"\\MicrosoftGame.config", "x" );
    std::wstring path;
    EXPECT_EQ( GameConfig::FindConfigFile( tree.c, path ), S_OK );
    EXPECT_EQ( path, tree.a + L"\\MicrosoftGame.config" );
}

TEST_F( GameConfigFind, FileNameMatchIgnoresCase )
{
    /* The shipped game uses MicrosoftGame.Config. */
    TempTree::Write( tree.b + L"\\MicrosoftGame.Config", "x" );
    std::wstring path;
    EXPECT_EQ( GameConfig::FindConfigFile( tree.c, path ), S_OK );
}

TEST_F( GameConfigFind, NoConfigAnywhereIsMissingGameConfig )
{
    std::wstring path = L"untouched";
    EXPECT_EQ( GameConfig::FindConfigFile( tree.c, path ), E_GAME_MISSING_GAME_CONFIG );
    EXPECT_EQ( path, L"untouched" );
}

TEST_F( GameConfigFind, NonNotFoundErrorFailsClosed )
{
    /* A config in an ancestor must not be used when a nearer level cannot be checked. */
    TempTree::Write( tree.a + L"\\MicrosoftGame.config", "x" );
    std::wstring path;
    HRESULT hr = GameConfig::FindConfigFile( tree.c + L"\\bad|name", path );
    EXPECT_EQ( hr, HRESULT_FROM_WIN32( ERROR_INVALID_NAME ) );
    EXPECT_TRUE( path.empty() );
}

class GameConfigSource : public ::testing::Test
{
protected:
    void TearDown() override { GameConfig::SetSource( nullptr, false ); }
    TempTree tree;
};

TEST_F( GameConfigSource, InlineSource )
{
    GameConfig::SetSource( Config( "35760C07" ).c_str(), true );
    UINT32 id = kSentinel;
    EXPECT_EQ( GameConfig::GetTitleId( &id ), S_OK );
    EXPECT_EQ( id, kMinecraft );
}

TEST_F( GameConfigSource, InlineBadAndEmptyAreBadFormat )
{
    UINT32 id = kSentinel;
    GameConfig::SetSource( "<Game/>", true );
    EXPECT_EQ( GameConfig::GetTitleId( &id ), E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT );
    GameConfig::SetSource( "", true );
    EXPECT_EQ( GameConfig::GetTitleId( &id ), E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT );
    EXPECT_EQ( id, kSentinel );
}

TEST_F( GameConfigSource, InlineFlagFalseTreatsTheStringAsAPath )
{
    UINT32 id = kSentinel;
    GameConfig::SetSource( Config( "35760C07" ).c_str(), false );
    /* The XML is not a usable file name. */
    EXPECT_EQ( GameConfig::GetTitleId( &id ), HRESULT_FROM_WIN32( ERROR_INVALID_NAME ) );
    EXPECT_EQ( id, kSentinel );
}

TEST_F( GameConfigSource, PathSource )
{
    std::wstring file = tree.b + L"\\game.cfg";
    TempTree::Write( file, Config( "35760C07" ) );
    std::string path = Narrow( file );
    GameConfig::SetSource( path.c_str(), false );
    UINT32 id = kSentinel;
    EXPECT_EQ( GameConfig::GetTitleId( &id ), S_OK );
    EXPECT_EQ( id, kMinecraft );
}

TEST_F( GameConfigSource, MissingPathIsMissingGameConfig )
{
    std::string path = Narrow( tree.b + L"\\nope.cfg" );
    GameConfig::SetSource( path.c_str(), false );
    UINT32 id = kSentinel;
    EXPECT_EQ( GameConfig::GetTitleId( &id ), E_GAME_MISSING_GAME_CONFIG );
    EXPECT_EQ( id, kSentinel );
}

TEST_F( GameConfigSource, PathWithBadContentIsBadFormat )
{
    std::wstring file = tree.b + L"\\bad.cfg";
    TempTree::Write( file, "<Game><TitleId>nothex</TitleId></Game>" );
    std::string path = Narrow( file );
    GameConfig::SetSource( path.c_str(), false );
    UINT32 id = kSentinel;
    EXPECT_EQ( GameConfig::GetTitleId( &id ), E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT );
    EXPECT_EQ( id, kSentinel );
}

TEST_F( GameConfigSource, OversizeFileIsBadFormat )
{
    std::wstring file = tree.b + L"\\big.cfg";
    TempTree::Write( file, std::string( 64 * 1024 + 1, ' ' ) );
    std::string path = Narrow( file );
    GameConfig::SetSource( path.c_str(), false );
    UINT32 id = kSentinel;
    EXPECT_EQ( GameConfig::GetTitleId( &id ), E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT );
    EXPECT_EQ( id, kSentinel );
}

TEST_F( GameConfigSource, InvalidUtf8PathIsBadFormat )
{
    GameConfig::SetSource( "C:\\\xC3\x28.cfg", false );
    UINT32 id = kSentinel;
    EXPECT_EQ( GameConfig::GetTitleId( &id ), E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT );
    EXPECT_EQ( id, kSentinel );
}

TEST_F( GameConfigSource, DirectoryAsPathIsAccessDenied )
{
    std::string path = Narrow( tree.b );
    GameConfig::SetSource( path.c_str(), false );
    UINT32 id = kSentinel;
    EXPECT_EQ( GameConfig::GetTitleId( &id ), HRESULT_FROM_WIN32( ERROR_ACCESS_DENIED ) );
    EXPECT_EQ( id, kSentinel );
}

TEST_F( GameConfigSource, DefaultSourceWalksUpFromTheExecutable )
{
    /* tests/System/fixtures/MicrosoftGame.config is copied beside the test exe. */
    GameConfig::SetSource( nullptr, false );
    UINT32 id = kSentinel;
    EXPECT_EQ( GameConfig::GetTitleId( &id ), S_OK );
    EXPECT_EQ( id, kMinecraft );
}

TEST_F( GameConfigSource, SuccessIsCachedUntilTheSourceChanges )
{
    std::wstring file = tree.b + L"\\game.cfg";
    TempTree::Write( file, Config( "35760C07" ) );
    std::string path = Narrow( file );
    GameConfig::SetSource( path.c_str(), false );
    UINT32 id = kSentinel;
    ASSERT_EQ( GameConfig::GetTitleId( &id ), S_OK );

    DeleteFileW( file.c_str() );
    id = kSentinel;
    EXPECT_EQ( GameConfig::GetTitleId( &id ), S_OK ); /* cached */
    EXPECT_EQ( id, kMinecraft );

    GameConfig::SetSource( Config( "00000042" ).c_str(), true );
    EXPECT_EQ( GameConfig::GetTitleId( &id ), S_OK );
    EXPECT_EQ( id, 0x42u );

    GameConfig::SetSource( path.c_str(), false ); /* same source again: re-read, now missing */
    id = kSentinel;
    EXPECT_EQ( GameConfig::GetTitleId( &id ), E_GAME_MISSING_GAME_CONFIG );
    EXPECT_EQ( id, kSentinel );
}

TEST_F( GameConfigSource, MsaAppIdInlineAndPath )
{
    const std::string xml = Config( std::string( "<MSAAppId>0000000040159362</MSAAppId>" ) );
    GameConfig::SetSource( xml.c_str(), true );
    std::string id = kAppIdSentinel;
    EXPECT_EQ( GameConfig::GetMsaAppId( id ), S_OK );
    EXPECT_EQ( id, "0000000040159362" );

    std::wstring file = tree.b + L"\\msa.cfg";
    TempTree::Write( file, xml );
    std::string path = Narrow( file );
    GameConfig::SetSource( path.c_str(), false );
    id = kAppIdSentinel;
    EXPECT_EQ( GameConfig::GetMsaAppId( id ), S_OK );
    EXPECT_EQ( id, "0000000040159362" );
}

TEST_F( GameConfigSource, MsaAppIdFailuresLeaveTheOutputUntouched )
{
    std::string id = kAppIdSentinel;
    GameConfig::SetSource( "<Game/>", true );
    EXPECT_EQ( GameConfig::GetMsaAppId( id ), E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT );
    GameConfig::SetSource( "<Game><MSAAppId>1</Game>", true ); /* malformed */
    EXPECT_EQ( GameConfig::GetMsaAppId( id ), E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT );
    GameConfig::SetSource( Config( "35760C07" ).c_str(), true ); /* TitleId only */
    EXPECT_EQ( GameConfig::GetMsaAppId( id ), E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT );
    std::string path = Narrow( tree.b + L"\\nope.cfg" );
    GameConfig::SetSource( path.c_str(), false );
    EXPECT_EQ( GameConfig::GetMsaAppId( id ), E_GAME_MISSING_GAME_CONFIG );
    EXPECT_EQ( id, kAppIdSentinel );
}

TEST_F( GameConfigSource, MsaAppIdIsCachedUntilTheSourceChangesAndFailureIsRetried )
{
    std::wstring file = tree.b + L"\\msa.cfg";
    std::string path = Narrow( file );
    GameConfig::SetSource( path.c_str(), false );
    std::string id = kAppIdSentinel;
    ASSERT_EQ( GameConfig::GetMsaAppId( id ), E_GAME_MISSING_GAME_CONFIG ); /* failure not cached */

    TempTree::Write( file, Config( std::string( "<MSAAppId>one</MSAAppId>" ) ) );
    ASSERT_EQ( GameConfig::GetMsaAppId( id ), S_OK );
    EXPECT_EQ( id, "one" );

    DeleteFileW( file.c_str() );
    id = kAppIdSentinel;
    EXPECT_EQ( GameConfig::GetMsaAppId( id ), S_OK ); /* cached */
    EXPECT_EQ( id, "one" );

    GameConfig::SetSource( Config( std::string( "<MSAAppId>two</MSAAppId>" ) ).c_str(), true );
    EXPECT_EQ( GameConfig::GetMsaAppId( id ), S_OK );
    EXPECT_EQ( id, "two" );
}

TEST_F( GameConfigSource, FailureIsNotCached )
{
    std::wstring file = tree.b + L"\\late.cfg";
    std::string path = Narrow( file );
    GameConfig::SetSource( path.c_str(), false );
    UINT32 id = kSentinel;
    ASSERT_EQ( GameConfig::GetTitleId( &id ), E_GAME_MISSING_GAME_CONFIG );

    TempTree::Write( file, Config( "35760C07" ) );
    EXPECT_EQ( GameConfig::GetTitleId( &id ), S_OK );
    EXPECT_EQ( id, kMinecraft );
}
