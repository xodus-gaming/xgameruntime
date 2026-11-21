/*
 * Xbox Game runtime Library
 *  Xodus Interopability Layer -> XodusXml
 *  Request format as accepted by xodus-service.
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

#include "XodusXml.h"

#include "IpcFraming.h"

#include <cstring>

namespace Xodus
{
    static const HRESULT INVALID_DATA = HRESULT_FROM_WIN32( ERROR_INVALID_DATA );

    static void Escape( const std::string &in, std::string &out )
    {
        for ( char c : in )
        {
            switch ( c )
            {
                case '&':  out += "&amp;";  break;
                case '<':  out += "&lt;";   break;
                case '>':  out += "&gt;";   break;
                case '"':  out += "&quot;"; break;
                case '\'': out += "&apos;"; break;
                default:   out += c;
            }
        }
    }

    static void AppendUtf8( std::string &out, uint32_t cp )
    {
        if ( cp < 0x80 ) out += static_cast<char>( cp );
        else if ( cp < 0x800 )
        {
            out += static_cast<char>( 0xC0 | ( cp >> 6 ) );
            out += static_cast<char>( 0x80 | ( cp & 0x3F ) );
        }
        else if ( cp < 0x10000 )
        {
            out += static_cast<char>( 0xE0 | ( cp >> 12 ) );
            out += static_cast<char>( 0x80 | ( ( cp >> 6 ) & 0x3F ) );
            out += static_cast<char>( 0x80 | ( cp & 0x3F ) );
        }
        else
        {
            out += static_cast<char>( 0xF0 | ( cp >> 18 ) );
            out += static_cast<char>( 0x80 | ( ( cp >> 12 ) & 0x3F ) );
            out += static_cast<char>( 0x80 | ( ( cp >> 6 ) & 0x3F ) );
            out += static_cast<char>( 0x80 | ( cp & 0x3F ) );
        }
    }

    static bool Unescape( const std::string &in, std::string &out )
    {
        out.clear();
        for ( size_t i = 0; i < in.size(); i++ )
        {
            if ( in[i] != '&' )
            {
                out += in[i];
                continue;
            }

            size_t semi = in.find( ';', i );
            if ( semi == std::string::npos ) return false;
            std::string ent = in.substr( i + 1, semi - i - 1 );
            i = semi;

            if ( ent == "amp" ) out += '&';
            else if ( ent == "lt" ) out += '<';
            else if ( ent == "gt" ) out += '>';
            else if ( ent == "quot" ) out += '"';
            else if ( ent == "apos" ) out += '\'';
            else if ( ent.size() > 1 && ent[0] == '#' )
            {
                bool hex = ent[1] == 'x';
                size_t start = hex ? 2 : 1;
                if ( start >= ent.size() || ent.size() - start > 8 ) return false;

                uint32_t cp = 0;
                for ( size_t k = start; k < ent.size(); k++ )
                {
                    char c = ent[k];
                    uint32_t d;
                    if ( c >= '0' && c <= '9' ) d = c - '0';
                    else if ( hex && c >= 'a' && c <= 'f' ) d = c - 'a' + 10;
                    else if ( hex && c >= 'A' && c <= 'F' ) d = c - 'A' + 10;
                    else return false;
                    cp = cp * ( hex ? 16 : 10 ) + d;
                }
                if ( !cp || cp > 0x10FFFF ) return false;
                AppendUtf8( out, cp );
            }
            else return false;
        }
        return true;
    }

    static void SkipWs( const std::string &s, size_t &i )
    {
        while ( i < s.size() && ( s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n' ) ) i++;
    }

    static bool Consume( const std::string &s, size_t &i, const char *lit )
    {
        size_t n = strlen( lit );
        if ( s.compare( i, n, lit ) != 0 ) return false;
        i += n;
        return true;
    }

    static bool IsNameChar( char c )
    {
        return ( c >= 'A' && c <= 'Z' ) || ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_' || c == '-' || c == '.';
    }

    /* Reads <name>text</name> or <name/> at i; leaves i after it. */
    static bool ReadLeaf( const std::string &s, size_t &i, std::string &name, std::string &text )
    {
        if ( i >= s.size() || s[i] != '<' ) return false;
        i++;
        size_t start = i;
        while ( i < s.size() && IsNameChar( s[i] ) ) i++;
        if ( i == start ) return false;
        name = s.substr( start, i - start );
        text.clear();

        if ( Consume( s, i, "/>" ) ) return true;
        if ( !Consume( s, i, ">" ) ) return false;

        size_t end = s.find( '<', i );
        if ( end == std::string::npos ) return false;
        if ( !Unescape( s.substr( i, end - i ), text ) ) return false;
        i = end;
        return Consume( s, i, "</" ) && Consume( s, i, name.c_str() ) && Consume( s, i, ">" );
    }

    HRESULT BuildMsaTokenRequest( const std::string &clientId, bool allowUi, bool fullTrust, std::string &xml )
    {
        std::string out;
        out += "<MSATokenRequest><ClientId>";
        Escape( clientId, out );
        out += "</ClientId><AllowUi>";
        out += allowUi ? "true" : "false";
        out += "</AllowUi><MSAFullTrust>";
        out += fullTrust ? "true" : "false";
        out += "</MSAFullTrust></MSATokenRequest>";

        if ( out.size() > FRAME_MAX_PAYLOAD ) return E_INVALIDARG;
        xml = std::move( out );
        return S_OK;
    }

    static bool ValidErrorCode( const std::string &s );

    HRESULT ParseMsaTokenResponse( const std::string &xml, MsaTokenResult &out )
    {
        out = MsaTokenResult{};
        if ( xml.size() > FRAME_MAX_PAYLOAD ) return INVALID_DATA;

        size_t i = 0;
        SkipWs( xml, i );
        if ( xml.compare( i, 5, "<?xml" ) == 0 )
        {
            size_t end = xml.find( "?>", i );
            if ( end == std::string::npos ) return INVALID_DATA;
            i = end + 2;
            SkipWs( xml, i );
        }
        if ( !Consume( xml, i, "<MSATokenResponse>" ) ) return INVALID_DATA;

        bool haveToken = false, haveError = false;
        std::string token, error;
        for ( ;; )
        {
            SkipWs( xml, i );
            if ( Consume( xml, i, "</MSATokenResponse>" ) ) break;

            std::string name, text;
            if ( !ReadLeaf( xml, i, name, text ) ) return INVALID_DATA;
            if ( name == "Token" && !haveToken ) { haveToken = true; token = text; }
            else if ( name == "Error" && !haveError ) { haveError = true; error = text; }
        }
        SkipWs( xml, i );
        if ( i != xml.size() ) return INVALID_DATA;

        if ( haveToken && !token.empty() )
        {
            out.token = std::move( token );
            return S_OK;
        }
        if ( haveError && !error.empty() )
        {
            if ( !ValidErrorCode( error ) ) return INVALID_DATA;
            out.errorCode = std::move( error );
            return E_FAIL;
        }
        return INVALID_DATA;
    }

    static void AppendLeaf( std::string &out, const char *name, const std::string &text )
    {
        out += '<';
        out += name;
        out += '>';
        Escape( text, out );
        out += "</";
        out += name;
        out += '>';
    }

    static void AppendBool( std::string &out, const char *name, bool v )
    {
        AppendLeaf( out, name, v ? "true" : "false" );
    }

    static void AppendTitleIdentity( std::string &out, const std::string &clientId, uint32_t titleId )
    {
        if ( !clientId.empty() ) AppendLeaf( out, "ClientId", clientId );
        if ( titleId ) AppendLeaf( out, "TitleId", std::to_string( titleId ) );
    }

    /* RFC 4648 standard alphabet, '=' padding, no line breaks. */
    static std::string Base64( const std::vector<uint8_t> &in )
    {
        static const char abc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve( ( in.size() + 2 ) / 3 * 4 );
        for ( size_t i = 0; i < in.size(); i += 3 )
        {
            uint32_t v = in[i] << 16;
            if ( i + 1 < in.size() ) v |= in[i + 1] << 8;
            if ( i + 2 < in.size() ) v |= in[i + 2];
            out += abc[( v >> 18 ) & 63];
            out += abc[( v >> 12 ) & 63];
            out += i + 1 < in.size() ? abc[( v >> 6 ) & 63] : '=';
            out += i + 2 < in.size() ? abc[v & 63] : '=';
        }
        return out;
    }

    HRESULT BuildUserInfoRequest( const std::string &clientId, uint32_t titleId, bool forceRefresh, std::string &xml )
    {
        std::string out = "<UserInfoRequest>";
        AppendTitleIdentity( out, clientId, titleId );
        AppendBool( out, "ForceRefresh", forceRefresh );
        out += "</UserInfoRequest>";

        if ( out.size() > FRAME_MAX_PAYLOAD ) return E_INVALIDARG;
        xml = std::move( out );
        return S_OK;
    }

    HRESULT BuildTokenAndSignatureRequest( const std::string &method, const std::string &url, const HeaderList &headers,
                                           const std::vector<uint8_t> &body, const std::string &clientId, uint32_t titleId,
                                           bool forceRefresh, std::string &xml )
    {
        if ( url.empty() ) return E_INVALIDARG;

        std::string out = "<TokenAndSignatureRequest>";
        AppendTitleIdentity( out, clientId, titleId );
        AppendLeaf( out, "Url", url );
        if ( !method.empty() ) AppendLeaf( out, "Method", method );
        if ( !headers.empty() )
        {
            out += "<Headers>";
            for ( const auto &h : headers )
            {
                out += "<Header>";
                AppendLeaf( out, "Name", h.first );
                AppendLeaf( out, "Value", h.second );
                out += "</Header>";
            }
            out += "</Headers>";
        }
        if ( !body.empty() ) AppendLeaf( out, "Body", Base64( body ) );
        AppendBool( out, "ForceRefresh", forceRefresh );
        out += "</TokenAndSignatureRequest>";

        if ( out.size() > FRAME_MAX_PAYLOAD ) return E_INVALIDARG;
        xml = std::move( out );
        return S_OK;
    }

    using Leaves = std::vector<std::pair<std::string, std::string>>;

    /* Reads <root> with leaf children only. */
    static HRESULT ReadResponse( const std::string &xml, const char *root, Leaves &leaves )
    {
        if ( xml.size() > FRAME_MAX_PAYLOAD ) return INVALID_DATA;

        const std::string open = std::string( "<" ) + root + ">", close = std::string( "</" ) + root + ">";
        size_t i = 0;
        SkipWs( xml, i );
        if ( xml.compare( i, 5, "<?xml" ) == 0 )
        {
            size_t end = xml.find( "?>", i );
            if ( end == std::string::npos ) return INVALID_DATA;
            i = end + 2;
            SkipWs( xml, i );
        }
        if ( !Consume( xml, i, open.c_str() ) ) return INVALID_DATA;

        for ( ;; )
        {
            SkipWs( xml, i );
            if ( Consume( xml, i, close.c_str() ) ) break;

            std::string name, text;
            if ( !ReadLeaf( xml, i, name, text ) ) return INVALID_DATA;
            leaves.emplace_back( std::move( name ), std::move( text ) );
        }
        SkipWs( xml, i );
        return i == xml.size() ? S_OK : INVALID_DATA;
    }

    /* First occurrence of name, or null. */
    static const std::string *Find( const Leaves &leaves, const char *name )
    {
        for ( const auto &l : leaves )
            if ( l.first == name ) return &l.second;
        return nullptr;
    }

    static std::string Get( const Leaves &leaves, const char *name )
    {
        const std::string *v = Find( leaves, name );
        return v ? *v : std::string();
    }

    /* "0x" plus exactly 8 hex digits. */
    static bool ValidErrorCode( const std::string &s )
    {
        if ( s.size() != 10 || s[0] != '0' || s[1] != 'x' ) return false;
        for ( size_t i = 2; i < s.size(); i++ )
        {
            char c = s[i];
            if ( !( ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'f' ) || ( c >= 'A' && c <= 'F' ) ) ) return false;
        }
        return true;
    }

    /* 1 to 19 ASCII digits that fit int64. */
    static bool ParseExpiry( const std::string &s, int64_t &v )
    {
        if ( s.empty() || s.size() > 19 ) return false;
        uint64_t n = 0;
        for ( char c : s )
        {
            if ( c < '0' || c > '9' ) return false;
            n = n * 10 + ( c - '0' );
        }
        if ( n > (uint64_t)INT64_MAX ) return false;
        v = (int64_t)n;
        return true;
    }

    static AgeGroup ParseAgeGroup( const std::string &s )
    {
        if ( s == "Child" ) return AgeGroup::Child;
        if ( s == "Teen" ) return AgeGroup::Teen;
        if ( s == "Adult" ) return AgeGroup::Adult;
        return AgeGroup::Unknown;
    }

    static void ReadIdentity( const Leaves &l, UserIdentity &id )
    {
        id.xuid = Get( l, "Xuid" );
        id.gamertag = Get( l, "Gamertag" );
        id.modernGamertag = Get( l, "ModernGamertag" );
        id.modernGamertagSuffix = Get( l, "ModernGamertagSuffix" );
        id.uniqueModernGamertag = Get( l, "UniqueModernGamertag" );
        id.uhs = Get( l, "Uhs" );
        id.ageGroup = ParseAgeGroup( Get( l, "AgeGroup" ) );
    }

    /* S_FALSE: no <Error>. E_FAIL: valid <Error>, code and message set. INVALID_DATA: <Error> out of grammar. */
    static HRESULT ReadServiceError( const Leaves &l, std::string &code, std::string &message )
    {
        const std::string *e = Find( l, "Error" );
        if ( !e ) return S_FALSE;
        if ( !ValidErrorCode( *e ) ) return INVALID_DATA;
        code = *e;
        message = Get( l, "Message" );
        return E_FAIL;
    }

    HRESULT ParseUserInfoResponse( const std::string &xml, UserInfoResult &out )
    {
        out = UserInfoResult{};

        Leaves l;
        HRESULT hr = ReadResponse( xml, "UserInfoResponse", l );
        if ( FAILED( hr ) ) return hr;

        UserInfoResult r;
        hr = ReadServiceError( l, r.errorCode, r.message );
        if ( hr == E_FAIL ) { out = std::move( r ); return E_FAIL; }
        if ( hr != S_FALSE ) return hr;

        const std::string *si = Find( l, "SignedIn" );
        if ( !si || ( *si != "true" && *si != "false" ) ) return INVALID_DATA;
        r.signedIn = *si == "true";
        if ( r.signedIn ) ReadIdentity( l, r );
        out = std::move( r );
        return S_OK;
    }

    HRESULT ParseTokenAndSignatureResponse( const std::string &xml, TokenAndSignatureResult &out )
    {
        out = TokenAndSignatureResult{};

        Leaves l;
        HRESULT hr = ReadResponse( xml, "TokenAndSignatureResponse", l );
        if ( FAILED( hr ) ) return hr;

        TokenAndSignatureResult r;
        hr = ReadServiceError( l, r.errorCode, r.message );
        if ( hr == E_FAIL ) { out = std::move( r ); return E_FAIL; }
        if ( hr != S_FALSE ) return hr;

        r.token = Get( l, "Token" );
        if ( r.token.empty() ) return INVALID_DATA;
        r.signature = Get( l, "Signature" );
        const std::string *ex = Find( l, "Expiry" );
        if ( ex && !ParseExpiry( *ex, r.expiry ) ) return INVALID_DATA;
        ReadIdentity( l, r );
        out = std::move( r );
        return S_OK;
    }
}
