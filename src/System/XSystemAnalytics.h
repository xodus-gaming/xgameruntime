/*
 * Xbox Game runtime Library
 *  GDK Component: System API -> XSystemAnalytics
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

#ifndef __XGAMERUNTIME_XSYSTEMANALYTICS_H__
#define __XGAMERUNTIME_XSYSTEMANALYTICS_H__

#include <xsystem.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

/*
 * The logic that turns the two strings Windows reports for
 * Windows.System.Profile.AnalyticsInfo (device family "Windows.Desktop" and
 * a decimal version number) into an XSystemAnalyticsInfo. It is header only
 * and free of any Windows runtime calls so it can be tested with plain
 * strings.
 */

// Produces the device family and the device family version strings.
using AnalyticsSource = std::function<HRESULT( std::string &family, std::string &version )>;

// Copies at most 63 bytes plus the NUL, never ending in the middle of a UTF-8 sequence.
inline void CopyAnalyticsString( const char *src, size_t len, char ( &dst )[64] )
{
    if ( len > sizeof( dst ) - 1 )
    {
        len = sizeof( dst ) - 1;
        // src[len] is the first byte left out: if it continues a character, drop that whole character.
        while ( len > 0 && ( static_cast<unsigned char>( src[len] ) & 0xC0 ) == 0x80 )
            --len;
    }

    std::memcpy( dst, src, len );
    dst[len] = '\0';
}

/*
 * family is "<family>.<form>" split at the first dot (no dot: the whole
 * string is the family and the form is empty). version is a decimal uint64
 * made of four 16 bit fields, major in the high bits. On failure out is all zero.
 */
inline HRESULT ParseAnalyticsInfo( const std::string &family, const std::string &version, XSystemAnalyticsInfo &out )
{
    std::memset( &out, 0, sizeof( out ) );

    if ( family.empty() || family[0] == '.' || version.empty() )
        return E_INVALIDARG;

    uint64_t value = 0;
    for ( char c : version )
    {
        if ( c < '0' || c > '9' )
            return E_INVALIDARG;

        unsigned digit = static_cast<unsigned>( c - '0' );
        if ( value > ( UINT64_MAX - digit ) / 10 )
            return E_INVALIDARG;

        value = value * 10 + digit;
    }

    size_t dot = family.find( '.' );
    if ( dot == std::string::npos )
    {
        CopyAnalyticsString( family.data(), family.size(), out.family );
    }
    else
    {
        CopyAnalyticsString( family.data(), dot, out.family );
        CopyAnalyticsString( family.data() + dot + 1, family.size() - dot - 1, out.form );
    }

    out.osVersion.major = static_cast<UINT16>( value >> 48 );
    out.osVersion.minor = static_cast<UINT16>( ( value >> 32 ) & 0xFFFF );
    out.osVersion.build = static_cast<UINT16>( ( value >> 16 ) & 0xFFFF );
    out.osVersion.revision = static_cast<UINT16>( value & 0xFFFF );

    // The OS hosting the game is the OS itself on a Windows PC.
    out.hostingOsVersion = out.osVersion;

    return S_OK;
}

/*
 * Fills out from source. out is written in full on every call: all zero
 * first, so a failing source or an unparsable answer can never leave stale
 * or uninitialized bytes behind.
 */
inline HRESULT FillAnalyticsInfo( const AnalyticsSource &source, XSystemAnalyticsInfo &out )
{
    std::memset( &out, 0, sizeof( out ) );

    std::string family, version;
    HRESULT hr = source( family, version );
    if ( FAILED( hr ) )
        return hr;

    return ParseAnalyticsInfo( family, version, out );
}

#endif
