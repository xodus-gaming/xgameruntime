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

TEST( XodusXml, ParseRejectsMalformedErrorCode )
{
    for ( const char *code : { "oops", "0x8004100", "0xZZZZZZZZ", "0x8004100c9", "0x8004100c\nSENTINEL" } )
    {
        MsaTokenResult r;
        EXPECT_EQ( ParseMsaTokenResponse( std::string( "<MSATokenResponse><Error>" ) + code + "</Error></MSATokenResponse>", r ), INVALID_DATA ) << code;
        EXPECT_TRUE( r.errorCode.empty() ) << code;
    }
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

/* ---- UserInfo / TokenAndSignature (xodus#216) ---- */

TEST( XodusXml, UserInfoRequestBytes )
{
    std::string xml;
    ASSERT_EQ( BuildUserInfoRequest( "cid&1", 1792830437, true, xml ), S_OK );
    EXPECT_EQ( xml, "<UserInfoRequest><ClientId>cid&amp;1</ClientId><TitleId>1792830437</TitleId>"
                    "<ForceRefresh>true</ForceRefresh></UserInfoRequest>" );
    ASSERT_EQ( BuildUserInfoRequest( "", 0, false, xml ), S_OK );
    EXPECT_EQ( xml, "<UserInfoRequest><ForceRefresh>false</ForceRefresh></UserInfoRequest>" );
    ASSERT_EQ( BuildUserInfoRequest( "c", 0, false, xml ), S_OK );
    EXPECT_EQ( xml, "<UserInfoRequest><ClientId>c</ClientId><ForceRefresh>false</ForceRefresh></UserInfoRequest>" );
    ASSERT_EQ( BuildUserInfoRequest( "", 5, false, xml ), S_OK );
    EXPECT_EQ( xml, "<UserInfoRequest><TitleId>5</TitleId><ForceRefresh>false</ForceRefresh></UserInfoRequest>" );
}

TEST( XodusXml, UserInfoRequestOverLimit )
{
    std::string probe;
    ASSERT_EQ( BuildUserInfoRequest( "a", 0, false, probe ), S_OK );
    const size_t fit = 0xFFFF - probe.size() + 1;   /* client id length that makes the request exactly 0xFFFF */
    std::string xml = "untouched";
    EXPECT_EQ( BuildUserInfoRequest( std::string( fit + 1, 'a' ), 0, false, xml ), E_INVALIDARG );
    EXPECT_EQ( xml, "untouched" );
    EXPECT_EQ( BuildUserInfoRequest( std::string( fit, 'a' ), 0, false, xml ), S_OK );
    EXPECT_EQ( xml.size(), 0xFFFFu );
}

TEST( XodusXml, TokenAndSignatureRequestBytes )
{
    std::string xml;
    ASSERT_EQ( BuildTokenAndSignatureRequest( "POST", "https://a.example/p?x=1&y=2", { { "Content-Type", "a<b" }, { "X", "" } },
                                              { 'f', 'o', 'o' }, "cid", 7, true, xml ), S_OK );
    EXPECT_EQ( xml, "<TokenAndSignatureRequest><ClientId>cid</ClientId><TitleId>7</TitleId>"
                    "<Url>https://a.example/p?x=1&amp;y=2</Url><Method>POST</Method>"
                    "<Headers><Header><Name>Content-Type</Name><Value>a&lt;b</Value></Header>"
                    "<Header><Name>X</Name><Value></Value></Header></Headers>"
                    "<Body>Zm9v</Body><ForceRefresh>true</ForceRefresh></TokenAndSignatureRequest>" );
    ASSERT_EQ( BuildTokenAndSignatureRequest( "", "https://u", {}, {}, "", 0, false, xml ), S_OK );
    EXPECT_EQ( xml, "<TokenAndSignatureRequest><Url>https://u</Url><ForceRefresh>false</ForceRefresh></TokenAndSignatureRequest>" );
}

TEST( XodusXml, TokenAndSignatureRequestBase64Padding )
{
    const std::pair<std::vector<uint8_t>, const char *> rows[] = {
        { { 'f' }, "Zg==" }, { { 'f', 'o' }, "Zm8=" }, { { 'f', 'o', 'o' }, "Zm9v" }, { { 0xFB, 0xFF, 0xBF }, "+/+/" },
    };
    for ( const auto &row : rows )
    {
        std::string xml;
        ASSERT_EQ( BuildTokenAndSignatureRequest( "", "u", {}, row.first, "", 0, false, xml ), S_OK );
        EXPECT_NE( xml.find( std::string( "<Body>" ) + row.second + "</Body>" ), std::string::npos ) << xml;
    }
}

TEST( XodusXml, TokenAndSignatureRequestRejectsEmptyUrlAndOversize )
{
    std::string xml = "untouched";
    EXPECT_EQ( BuildTokenAndSignatureRequest( "GET", "", {}, {}, "", 0, false, xml ), E_INVALIDARG );
    EXPECT_EQ( BuildTokenAndSignatureRequest( "GET", "u", {}, std::vector<uint8_t>( 0xFFFF, 1 ), "", 0, false, xml ), E_INVALIDARG );
    EXPECT_EQ( xml, "untouched" );
}

namespace
{
    const char *SIGNED_IN =
        "<UserInfoResponse><SignedIn>true</SignedIn><Xuid>2535405123456789</Xuid><Gamertag>Gamer Tag</Gamertag>"
        "<ModernGamertag>GamerTag</ModernGamertag><ModernGamertagSuffix>1234</ModernGamertagSuffix>"
        "<UniqueModernGamertag>GamerTag#1234</UniqueModernGamertag><AgeGroup>Teen</AgeGroup><Uhs>12345</Uhs></UserInfoResponse>";
}

TEST( XodusXml, UserInfoSignedIn )
{
    UserInfoResult r;
    ASSERT_EQ( ParseUserInfoResponse( SIGNED_IN, r ), S_OK );
    EXPECT_TRUE( r.signedIn );
    EXPECT_EQ( r.xuid, "2535405123456789" );
    EXPECT_EQ( r.gamertag, "Gamer Tag" );
    EXPECT_EQ( r.modernGamertag, "GamerTag" );
    EXPECT_EQ( r.modernGamertagSuffix, "1234" );
    EXPECT_EQ( r.uniqueModernGamertag, "GamerTag#1234" );
    EXPECT_EQ( r.ageGroup, AgeGroup::Teen );
    EXPECT_EQ( r.uhs, "12345" );
    EXPECT_TRUE( r.errorCode.empty() );
}

TEST( XodusXml, UserInfoSignedOutAndUnknownAgeGroup )
{
    UserInfoResult r;
    ASSERT_EQ( ParseUserInfoResponse( "<UserInfoResponse><SignedIn>false</SignedIn></UserInfoResponse>", r ), S_OK );
    EXPECT_FALSE( r.signedIn );
    EXPECT_TRUE( r.xuid.empty() );

    ASSERT_EQ( ParseUserInfoResponse( "<UserInfoResponse><SignedIn>true</SignedIn><AgeGroup>Martian</AgeGroup></UserInfoResponse>", r ), S_OK );
    EXPECT_EQ( r.ageGroup, AgeGroup::Unknown );
    EXPECT_TRUE( r.xuid.empty() );
}

TEST( XodusXml, UserInfoServiceError )
{
    UserInfoResult r;
    EXPECT_EQ( ParseUserInfoResponse(
        "<UserInfoResponse><Error>0x80070057</Error><Message>malformed &amp; bad</Message></UserInfoResponse>", r ), E_FAIL );
    EXPECT_EQ( r.errorCode, "0x80070057" );
    EXPECT_EQ( r.message, "malformed & bad" );
    EXPECT_FALSE( r.signedIn );

    /* An <Error> beside success elements wins and leaves them empty. */
    EXPECT_EQ( ParseUserInfoResponse(
        "<UserInfoResponse><SignedIn>true</SignedIn><Xuid>SENTINELXUID</Xuid><Error>0x89245102</Error></UserInfoResponse>", r ), E_FAIL );
    EXPECT_TRUE( r.xuid.empty() );
    EXPECT_FALSE( r.signedIn );
}

TEST( XodusXml, UserInfoRejectsBadInput )
{
    const char *bad[] = {
        "",
        "<UserInfoResponse>",
        "<UserInfoResponse></UserInfoResponse>",
        "<UserInfoResponse><SignedIn>true</SignedIn>",
        "<UserInfoResponse><SignedIn>True</SignedIn></UserInfoResponse>",
        "<UserInfoResponse><SignedIn>1</SignedIn></UserInfoResponse>",
        "<UserInfoResponse><SignedIn></SignedIn></UserInfoResponse>",
        "<UserInfoResponse><Error>oops</Error></UserInfoResponse>",
        "<UserInfoResponse><Error>0x8015DC1</Error></UserInfoResponse>",
        "<UserInfoResponse><Error>0xZZZZZZZZ</Error></UserInfoResponse>",
        "<UserInfoResponse><Error>0x8015DC123</Error></UserInfoResponse>",
        "<UserInfoResponse><SignedIn>true</SignedIn><Xuid>SENTINELXUID</Xuid><Error>0x8015DC12\nSENTINELXUID</Error></UserInfoResponse>",
        "<TokenAndSignatureResponse><SignedIn>true</SignedIn></TokenAndSignatureResponse>",
        "<UserInfoResponse><SignedIn>true</SignedIn></UserInfoResponse>trailing",
    };
    for ( const char *s : bad )
    {
        UserInfoResult r;
        r.signedIn = true;
        EXPECT_EQ( ParseUserInfoResponse( s, r ), INVALID_DATA ) << s;
        EXPECT_FALSE( r.signedIn ) << s;
        EXPECT_TRUE( r.xuid.empty() ) << s;
        EXPECT_TRUE( r.errorCode.empty() ) << s;
    }
}

TEST( XodusXml, UserInfoOversizedIsRejected )
{
    const std::string head = "<UserInfoResponse><SignedIn>true</SignedIn><Xuid>", tail = "</Xuid></UserInfoResponse>";
    UserInfoResult r;
    EXPECT_EQ( ParseUserInfoResponse( head + std::string( 0xFFFF - head.size() - tail.size(), '1' ) + tail, r ), S_OK );
    EXPECT_EQ( ParseUserInfoResponse( head + std::string( 0x10000 - head.size() - tail.size(), '1' ) + tail, r ), INVALID_DATA );
    EXPECT_TRUE( r.xuid.empty() );
}

TEST( XodusXml, TokenAndSignatureSuccess )
{
    TokenAndSignatureResult r;
    ASSERT_EQ( ParseTokenAndSignatureResponse(
        "<TokenAndSignatureResponse><Token>XBL3.0 x=1;tok&amp;en</Token><Signature>c2ln</Signature><Expiry>1790000000</Expiry>"
        "<Xuid>25</Xuid><Gamertag>G</Gamertag><AgeGroup>Adult</AgeGroup><Uhs>1</Uhs></TokenAndSignatureResponse>", r ), S_OK );
    EXPECT_EQ( r.token, "XBL3.0 x=1;tok&en" );
    EXPECT_EQ( r.signature, "c2ln" );
    EXPECT_EQ( r.expiry, 1790000000 );
    EXPECT_EQ( r.xuid, "25" );
    EXPECT_EQ( r.gamertag, "G" );
    EXPECT_EQ( r.ageGroup, AgeGroup::Adult );
    EXPECT_EQ( r.uhs, "1" );

    ASSERT_EQ( ParseTokenAndSignatureResponse(
        "<TokenAndSignatureResponse><Token>t</Token><Signature></Signature><Expiry>9223372036854775807</Expiry></TokenAndSignatureResponse>", r ), S_OK );
    EXPECT_TRUE( r.signature.empty() );
    EXPECT_EQ( r.expiry, INT64_MAX );

    ASSERT_EQ( ParseTokenAndSignatureResponse( "<TokenAndSignatureResponse><Token>t</Token></TokenAndSignatureResponse>", r ), S_OK );
    EXPECT_EQ( r.expiry, 0 );
}

TEST( XodusXml, TokenAndSignatureServiceError )
{
    TokenAndSignatureResult r;
    EXPECT_EQ( ParseTokenAndSignatureResponse(
        "<TokenAndSignatureResponse><Error>0x89245101</Error><Message>no user is signed in</Message></TokenAndSignatureResponse>", r ), E_FAIL );
    EXPECT_EQ( r.errorCode, "0x89245101" );
    EXPECT_EQ( r.message, "no user is signed in" );

    EXPECT_EQ( ParseTokenAndSignatureResponse(
        "<TokenAndSignatureResponse><Token>SENTINELTOKEN</Token><Signature>SENTINELSIG</Signature><Error>0x80004005</Error></TokenAndSignatureResponse>", r ), E_FAIL );
    EXPECT_TRUE( r.token.empty() );
    EXPECT_TRUE( r.signature.empty() );
}

TEST( XodusXml, TokenAndSignatureRejectsBadInput )
{
    const char *bad[] = {
        "",
        "<TokenAndSignatureResponse>",
        "<TokenAndSignatureResponse></TokenAndSignatureResponse>",
        "<TokenAndSignatureResponse><Token></Token></TokenAndSignatureResponse>",
        "<TokenAndSignatureResponse><Token>SENTINELTOKEN</Token><Signature>SENTINELSIG</Signature><Expiry>abc</Expiry></TokenAndSignatureResponse>",
        "<TokenAndSignatureResponse><Token>SENTINELTOKEN</Token><Expiry>-5</Expiry></TokenAndSignatureResponse>",
        "<TokenAndSignatureResponse><Token>SENTINELTOKEN</Token><Expiry>+5</Expiry></TokenAndSignatureResponse>",
        "<TokenAndSignatureResponse><Token>SENTINELTOKEN</Token><Expiry> 5</Expiry></TokenAndSignatureResponse>",
        "<TokenAndSignatureResponse><Token>SENTINELTOKEN</Token><Expiry></Expiry></TokenAndSignatureResponse>",
        "<TokenAndSignatureResponse><Token>SENTINELTOKEN</Token><Expiry>99999999999999999999</Expiry></TokenAndSignatureResponse>",
        "<TokenAndSignatureResponse><Token>SENTINELTOKEN</Token><Expiry>9223372036854775808</Expiry></TokenAndSignatureResponse>",
        "<TokenAndSignatureResponse><Error>oops</Error></TokenAndSignatureResponse>",
        "<TokenAndSignatureResponse><Error>0xZZZZZZZZ</Error></TokenAndSignatureResponse>",
        "<TokenAndSignatureResponse><Error>0x8015DC123</Error></TokenAndSignatureResponse>",
        "<TokenAndSignatureResponse><Token>SENTINELTOKEN</Token><Error>0x8015DC12\nSENTINELTOKEN</Error></TokenAndSignatureResponse>",
        "<UserInfoResponse><Token>t</Token></UserInfoResponse>",
    };
    for ( const char *s : bad )
    {
        TokenAndSignatureResult r;
        EXPECT_EQ( ParseTokenAndSignatureResponse( s, r ), INVALID_DATA ) << s;
        EXPECT_TRUE( r.token.empty() ) << s;
        EXPECT_TRUE( r.signature.empty() ) << s;
        EXPECT_TRUE( r.errorCode.empty() ) << s;
        EXPECT_EQ( r.expiry, 0 ) << s;
    }
}

TEST( XodusXml, TokenAndSignatureOversizedIsRejected )
{
    const std::string head = "<TokenAndSignatureResponse><Token>", tail = "</Token></TokenAndSignatureResponse>";
    TokenAndSignatureResult r;
    EXPECT_EQ( ParseTokenAndSignatureResponse( head + std::string( 0xFFFF - head.size() - tail.size(), 'a' ) + tail, r ), S_OK );
    EXPECT_EQ( ParseTokenAndSignatureResponse( head + std::string( 0x10000 - head.size() - tail.size(), 'a' ) + tail, r ), INVALID_DATA );
    EXPECT_TRUE( r.token.empty() );
}
