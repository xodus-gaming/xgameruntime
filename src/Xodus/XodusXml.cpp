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
            out.errorCode = std::move( error );
            return E_FAIL;
        }
        return INVALID_DATA;
    }
}
