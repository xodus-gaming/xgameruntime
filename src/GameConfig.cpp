/*
 * Xbox Game runtime Library
 *  MicrosoftGame.config reader
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

#include "GameConfig.h"

#include <private/logging.h>
#include <xgameerr.h>

#include <objbase.h>
#include <xmllite.h>

#include <mutex>
#include <string>
#include <vector>

namespace
{
    constexpr size_t kMaxConfigSize = 64 * 1024;
    constexpr wchar_t kConfigName[] = L"MicrosoftGame.config";

    template <typename T>
    struct ComPtr
    {
        T *p = nullptr;
        ~ComPtr() { if ( p ) p->Release(); }
        T **operator&() { return &p; }
        T *operator->() { return p; }
    };

    HRESULT LastErrorHr()
    {
        DWORD err = GetLastError();
        return err ? HRESULT_FROM_WIN32( err ) : E_FAIL;
    }

    /* The value of a hex digit, or -1 for any other character. */
    int HexDigit( wchar_t c )
    {
        if ( c >= L'0' && c <= L'9' ) return c - L'0';
        if ( c >= L'a' && c <= L'f' ) return c - L'a' + 10;
        if ( c >= L'A' && c <= L'F' ) return c - L'A' + 10;
        return -1;
    }

    /* 1 to 8 hex digits, nothing else once surrounding whitespace is trimmed. */
    bool ParseHex( const std::wstring &text, UINT32 *out )
    {
        size_t begin = text.find_first_not_of( L" \t\r\n" );
        if ( begin == std::wstring::npos ) return false;
        size_t end = text.find_last_not_of( L" \t\r\n" ) + 1;
        if ( end - begin > 8 ) return false;

        UINT32 value = 0;
        for ( size_t i = begin; i < end; i++ )
        {
            int d = HexDigit( text[i] );
            if ( d < 0 ) return false;
            value = ( value << 4 ) | (UINT32)d;
        }
        *out = value;
        return true;
    }

    HRESULT ReadFileBytes( const std::wstring &path, std::string &bytes )
    {
        HANDLE file = CreateFileW( path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
        if ( file == INVALID_HANDLE_VALUE )
        {
            DWORD err = GetLastError();
            if ( err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND ) return E_GAME_MISSING_GAME_CONFIG;
            return LastErrorHr();
        }

        HRESULT hr = S_OK;
        LARGE_INTEGER size;
        if ( !GetFileSizeEx( file, &size ) )
            hr = LastErrorHr();
        else if ( size.QuadPart > static_cast<LONGLONG>( kMaxConfigSize ) )
            hr = E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT;
        else
        {
            bytes.resize( static_cast<size_t>( size.QuadPart ) );
            DWORD got = 0;
            if ( !bytes.empty() && !ReadFile( file, &bytes[0], static_cast<DWORD>( bytes.size() ), &got, nullptr ) )
                hr = LastErrorHr();
            else if ( got != bytes.size() )
                hr = E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT;
        }

        CloseHandle( file );
        return hr;
    }

    HRESULT DefaultStartDir( std::wstring &dir )
    {
        std::wstring buf( 32768, L'\0' );
        DWORD n = GetModuleFileNameW( nullptr, &buf[0], static_cast<DWORD>( buf.size() ) );
        if ( !n ) return LastErrorHr();
        buf.resize( n );
        size_t slash = buf.find_last_of( L"\\/" );
        dir = slash == std::wstring::npos ? buf : buf.substr( 0, slash );
        return S_OK;
    }

    std::mutex g_lock;
    bool g_hasSource = false;
    bool g_inline = false;
    std::string g_source;
    bool g_cached = false;
    UINT32 g_titleId = 0;
    bool g_msaCached = false;
    std::string g_msaAppId;
}

namespace GameConfig
{
    /* Raw text of the first direct child of the root named wanted. */
    static HRESULT ReadElementText( const char *xml, size_t len, const wchar_t *wanted, std::wstring &text )
    {
        if ( !xml || !len || len > kMaxConfigSize ) return E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT;

        /* XmlLite reads from an IStream; give it a copy of the bytes. */
        ComPtr<IStream> stream;
        HRESULT hr = CreateStreamOnHGlobal( nullptr, TRUE, &stream );
        if ( FAILED( hr ) ) return hr;
        ULONG written = 0;
        hr = stream->Write( xml, static_cast<ULONG>( len ), &written );
        if ( FAILED( hr ) || written != len ) return FAILED( hr ) ? hr : E_FAIL;
        LARGE_INTEGER zero = {};
        hr = stream->Seek( zero, STREAM_SEEK_SET, nullptr );
        if ( FAILED( hr ) ) return hr;

        ComPtr<IXmlReader> reader;
        hr = CreateXmlReader( __uuidof( IXmlReader ), reinterpret_cast<void **>( &reader ), nullptr );
        if ( FAILED( hr ) ) return hr;
        hr = reader->SetProperty( XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit );
        if ( FAILED( hr ) ) return hr;
        hr = reader->SetInput( stream.p );
        if ( FAILED( hr ) ) return hr;

        UINT depth = 0;
        bool capturing = false, found = false;
        XmlNodeType type;
        size_t nodes = 0;

        while ( ( hr = reader->Read( &type ) ) == S_OK )
        {
            /* Every node consumes input; more nodes than bytes means the reader is not advancing
             * (Wine's XmlLite returns text nodes forever for input truncated inside an element). */
            if ( ++nodes > len ) return E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT;
            switch ( type )
            {
                case XmlNodeType_Element:
                {
                    const WCHAR *name = nullptr;
                    UINT nameLen = 0;
                    if ( FAILED( reader->GetLocalName( &name, &nameLen ) ) ) return E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT;
                    std::wstring element( name, nameLen );
                    depth++;

                    if ( depth == 1 && element != L"Game" ) return E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT;
                    if ( capturing ) return E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT; /* child element inside the wanted one */
                    if ( depth == 2 && !found && element == wanted ) capturing = true;

                    if ( reader->IsEmptyElement() )
                    {
                        if ( capturing ) { capturing = false; found = true; }
                        depth--;
                    }
                    break;
                }
                case XmlNodeType_EndElement:
                    if ( capturing && depth == 2 ) { capturing = false; found = true; }
                    depth--;
                    break;
                case XmlNodeType_Text:
                case XmlNodeType_CDATA:
                case XmlNodeType_Whitespace:
                {
                    if ( !capturing ) break;
                    const WCHAR *value = nullptr;
                    UINT valueLen = 0;
                    if ( FAILED( reader->GetValue( &value, &valueLen ) ) ) return E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT;
                    text.append( value, valueLen );
                    break;
                }
                default:
                    break;
            }
        }

        if ( FAILED( hr ) || !found ) return E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT;
        return S_OK;
    }

    HRESULT ParseTitleId( const char *xml, size_t len, UINT32 *titleId )
    {
        std::wstring text;
        HRESULT hr = ReadElementText( xml, len, L"TitleId", text );
        if ( FAILED( hr ) ) return hr;

        UINT32 value = 0;
        if ( !ParseHex( text, &value ) ) return E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT;
        *titleId = value;
        return S_OK;
    }

    HRESULT ParseMsaAppId( const char *xml, size_t len, std::string &appId )
    {
        std::wstring text;
        HRESULT hr = ReadElementText( xml, len, L"MSAAppId", text );
        if ( FAILED( hr ) ) return hr;

        size_t begin = text.find_first_not_of( L" \t\r\n" );
        if ( begin == std::wstring::npos ) return E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT;
        text = text.substr( begin, text.find_last_not_of( L" \t\r\n" ) + 1 - begin );

        int n = WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>( text.size() ),
                                     nullptr, 0, nullptr, nullptr );
        if ( n <= 0 ) return E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT;
        std::string value( n, '\0' );
        WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>( text.size() ),
                             &value[0], n, nullptr, nullptr );
        appId = value;
        return S_OK;
    }

    HRESULT FindConfigFile( const std::wstring &startDir, std::wstring &path )
    {
        std::wstring dir = startDir;
        for ( ;; )
        {
            while ( !dir.empty() && ( dir.back() == L'\\' || dir.back() == L'/' ) ) dir.pop_back();

            std::wstring candidate = dir + L"\\" + kConfigName;
            DWORD attrs = GetFileAttributesW( candidate.c_str() );
            if ( attrs != INVALID_FILE_ATTRIBUTES )
            {
                if ( !( attrs & FILE_ATTRIBUTE_DIRECTORY ) )
                {
                    path = candidate;
                    return S_OK;
                }
            }
            else
            {
                DWORD err = GetLastError();
                if ( err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND )
                    return LastErrorHr();
            }

            size_t slash = dir.find_last_of( L"\\/" );
            if ( slash == std::wstring::npos ) break;
            dir.resize( slash );
        }
        return E_GAME_MISSING_GAME_CONFIG;
    }

    void SetSource( const char *config, bool isInline )
    {
        std::lock_guard<std::mutex> lock( g_lock );
        g_hasSource = config != nullptr;
        g_inline = isInline;
        g_source = config ? config : "";
        g_cached = false;
        g_msaCached = false;
    }

    /* The current source's XML; path is the file path (empty for inline) for logging. Caller holds g_lock. */
    static HRESULT LoadConfig( std::string &xml, std::wstring &path )
    {
        HRESULT hr = S_OK;
        if ( g_hasSource && g_inline )
            xml = g_source;
        else
        {
            if ( g_hasSource )
            {
                int n = MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, g_source.c_str(), -1, nullptr, 0 );
                if ( n <= 1 )
                    hr = E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT;
                else
                {
                    path.resize( n );
                    MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, g_source.c_str(), -1, &path[0], n );
                    path.resize( n - 1 );
                }
            }
            else
            {
                std::wstring dir;
                hr = DefaultStartDir( dir );
                if ( SUCCEEDED( hr ) ) hr = FindConfigFile( dir, path );
            }
            if ( SUCCEEDED( hr ) ) hr = ReadFileBytes( path, xml );
        }
        return hr;
    }

    HRESULT GetTitleId( UINT32 *titleId )
    {
        std::lock_guard<std::mutex> lock( g_lock );
        if ( g_cached )
        {
            *titleId = g_titleId;
            return S_OK;
        }

        std::string xml;
        std::wstring path;
        HRESULT hr = LoadConfig( xml, path );

        UINT32 value = 0;
        if ( SUCCEEDED( hr ) ) hr = ParseTitleId( xml.data(), xml.size(), &value );
        if ( FAILED( hr ) )
        {
            ERR( "TitleId not available: %#lx (config path \"%ls\")\n", hr, path.c_str() );
            return hr;
        }

        TRACE( "TitleId %#x\n", value );
        g_cached = true;
        g_titleId = value;
        *titleId = value;
        return S_OK;
    }

    HRESULT GetMsaAppId( std::string &appId )
    {
        std::lock_guard<std::mutex> lock( g_lock );
        if ( g_msaCached )
        {
            appId = g_msaAppId;
            return S_OK;
        }

        std::string xml;
        std::wstring path;
        HRESULT hr = LoadConfig( xml, path );

        std::string value;
        if ( SUCCEEDED( hr ) ) hr = ParseMsaAppId( xml.data(), xml.size(), value );
        if ( FAILED( hr ) )
        {
            ERR( "MSAAppId not available: %#lx (config path \"%ls\")\n", hr, path.c_str() );
            return hr;
        }

        g_msaCached = true;
        g_msaAppId = value;
        appId = value;
        return S_OK;
    }
}
