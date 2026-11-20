/*
 * Xbox Game runtime Library
 *  Xodus Interopability Layer -> IpcFraming
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

#include "IpcFraming.h"

namespace Xodus
{
    static void PutLe( std::vector<uint8_t> &v, uint32_t val, int bytes )
    {
        for ( int i = 0; i < bytes; i++ )
            v.push_back( static_cast<uint8_t>( val >> ( 8 * i ) ) );
    }

    static uint32_t GetLe( const uint8_t *p, int bytes )
    {
        uint32_t val = 0;
        for ( int i = 0; i < bytes; i++ )
            val |= static_cast<uint32_t>( p[i] ) << ( 8 * i );
        return val;
    }

    std::vector<uint8_t> Encode( uint16_t type, const uint8_t *payload, size_t len )
    {
        std::vector<uint8_t> out;
        if ( len > FRAME_MAX_PAYLOAD || ( len && !payload ) ) return out;

        out.reserve( FRAME_HEADER_SIZE + len );
        PutLe( out, FRAME_MAGIC, 4 );
        PutLe( out, type, 2 );
        PutLe( out, static_cast<uint32_t>( len ), 2 );
        if ( len ) out.insert( out.end(), payload, payload + len );
        return out;
    }

    void FrameDecoder::Feed( const uint8_t *p, size_t n )
    {
        if ( p && n ) buf.insert( buf.end(), p, p + n );
    }

    HRESULT FrameDecoder::Next( Frame &f )
    {
        /* Check magic as soon as its 4 bytes are in, not only once a full header is. */
        if ( buf.size() >= 4 && GetLe( buf.data(), 4 ) != FRAME_MAGIC )
            return HRESULT_FROM_WIN32( ERROR_INVALID_DATA );
        if ( buf.size() < FRAME_HEADER_SIZE ) return S_FALSE;

        size_t len = GetLe( buf.data() + 6, 2 );
        if ( buf.size() < FRAME_HEADER_SIZE + len ) return S_FALSE;

        f.type = static_cast<uint16_t>( GetLe( buf.data() + 4, 2 ) );
        f.payload.assign( buf.begin() + FRAME_HEADER_SIZE, buf.begin() + FRAME_HEADER_SIZE + len );
        buf.erase( buf.begin(), buf.begin() + FRAME_HEADER_SIZE + len );
        return S_OK;
    }
}
