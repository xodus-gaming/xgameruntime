/*
 * Xodus XodusXml Tests
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

#include "Xodus/XodusXml.h"

#include <fstream>
#include <sstream>

using namespace Xodus;

namespace
{
    const HRESULT INVALID_DATA = HRESULT_FROM_WIN32( ERROR_INVALID_DATA );

    std::string ReadFixture( const char *name )
    {
        std::ifstream f( std::string( XODUS_FIXTURE_DIR ) + "/" + name, std::ios::binary );
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    /* A well-formed token response of exactly n bytes. */
    std::string MakeResp( size_t n )
    {
        const std::string head = "<MSATokenResponse><Token>", tail = "</Token></MSATokenResponse>";
        return head + std::string( n - head.size() - tail.size(), 'a' ) + tail;
    }
}

TEST( XodusXml, RequestMatchesFixture )
{
    std::string fixture = ReadFixture( "msa_request.xml" );
    ASSERT_FALSE( fixture.empty() );

    std::string xml;
    ASSERT_EQ( BuildMsaTokenRequest( "00000000-0000-0000-0000-000000000000", true, false, xml ), S_OK );
    EXPECT_EQ( xml, fixture );
}

TEST( XodusXml, RequestBooleans )
{
    std::string xml;
    ASSERT_EQ( BuildMsaTokenRequest( "x", false, true, xml ), S_OK );
    EXPECT_NE( xml.find( "<AllowUi>false</AllowUi>" ), std::string::npos );
    EXPECT_NE( xml.find( "<MSAFullTrust>true</MSAFullTrust>" ), std::string::npos );
}

TEST( XodusXml, RequestEscapesSpecials )
{
    std::string xml;
    ASSERT_EQ( BuildMsaTokenRequest( "a&b<c>d\"e'f", false, false, xml ), S_OK );
    EXPECT_NE( xml.find( "<ClientId>a&amp;b&lt;c&gt;d&quot;e&apos;f</ClientId>" ), std::string::npos );
}

TEST( XodusXml, RequestAtLimitAccepted )
{
    std::string probe;
    ASSERT_EQ( BuildMsaTokenRequest( "", false, false, probe ), S_OK );
    std::string xml;
    EXPECT_EQ( BuildMsaTokenRequest( std::string( 0xFFFF - probe.size(), 'a' ), false, false, xml ), S_OK );
    EXPECT_EQ( xml.size(), 0xFFFFu );
    EXPECT_EQ( BuildMsaTokenRequest( std::string( 0xFFFF - probe.size() + 1, 'a' ), false, false, xml ), E_INVALIDARG );
    xml = "untouched";
    EXPECT_EQ( BuildMsaTokenRequest( std::string( 0x10000, 'a' ), false, false, xml ), E_INVALIDARG );
    EXPECT_EQ( xml, "untouched" );
}

TEST( XodusXml, ParseTokenResponse )
{
    MsaTokenResult r;
    ASSERT_EQ( ParseMsaTokenResponse(
        "<MSATokenResponse><Token>t&amp;ok&lt;en</Token><Expiry>1</Expiry><DeviceRps>d</DeviceRps><DeviceExpiry>2</DeviceExpiry></MSATokenResponse>", r ), S_OK );
    EXPECT_EQ( r.token, "t&ok<en" );
    EXPECT_TRUE( r.errorCode.empty() );
}

TEST( XodusXml, ParseNumericEntities )
{
    MsaTokenResult r;
    ASSERT_EQ( ParseMsaTokenResponse( "<MSATokenResponse><Token>&#65;&#x42;&#xE9;</Token></MSATokenResponse>", r ), S_OK );
    EXPECT_EQ( r.token, "AB\xC3\xA9" );
}

TEST( XodusXml, ParseErrorResponse )
{
    MsaTokenResult r;
    EXPECT_EQ( ParseMsaTokenResponse( "<MSATokenResponse><Error>0x8004100c</Error></MSATokenResponse>", r ), E_FAIL );
    EXPECT_EQ( r.errorCode, "0x8004100c" );
    EXPECT_TRUE( r.token.empty() );
}

TEST( XodusXml, ParseRejectsBadInput )
{
    const char *bad[] = {
        "",
        "   ",
        "<MSATokenResponse>",
        "<MSATokenResponse><Token>abc",
        "<MSATokenResponse><Token>abc</Token>",
        "<MSATokenResponse><Token>abc</Tok></MSATokenResponse>",
        "<MSATokenResponse></MSATokenResponse>",
        "<MSATokenResponse><Token></Token></MSATokenResponse>",
        "<MSATokenResponse><Token>a&bogus;b</Token></MSATokenResponse>",
        "<MSATokenResponse><Token>a&amp</Token></MSATokenResponse>",
        "<MSATokenResponse><Token>a&#65</Token></MSATokenResponse>",
        "<MSATokenResponse><Token>a&#x41</Token></MSATokenResponse>",
        "<Other><Token>abc</Token></Other>",
        "<MSATokenResponse><Token>abc</Token></MSATokenResponse>trailing",
        "<MSATokenResponse><Token",
        "<",
    };
    for ( const char *s : bad )
    {
        MsaTokenResult r;
        EXPECT_EQ( ParseMsaTokenResponse( s, r ), INVALID_DATA ) << s;
        EXPECT_TRUE( r.token.empty() ) << s;
    }
}

TEST( XodusXml, ParseEmbeddedNulDoesNotOverread )
{
    std::string s( "<MSATokenResponse><Token>ab", 27 );
    s.push_back( '\0' );
    MsaTokenResult r;
    EXPECT_EQ( ParseMsaTokenResponse( s, r ), INVALID_DATA );
}

TEST( XodusXml, ParseOneByteOverLimitIsRejected )
{
    /* A well-formed response of exactly 0x10000 bytes: only the size bound rejects it. */
    std::string s = MakeResp( 0x10000 );
    ASSERT_EQ( s.size(), 0x10000u );
    MsaTokenResult r;
    EXPECT_EQ( ParseMsaTokenResponse( s, r ), INVALID_DATA );
    EXPECT_TRUE( r.token.empty() );
}

TEST( XodusXml, ParseAtLimitAccepted )
{
    std::string s = MakeResp( 0xFFFF );
    ASSERT_EQ( s.size(), 0xFFFFu );
    MsaTokenResult r;
    EXPECT_EQ( ParseMsaTokenResponse( s, r ), S_OK );
}
