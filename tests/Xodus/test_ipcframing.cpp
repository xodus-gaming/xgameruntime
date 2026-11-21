/*
 * Xodus IpcFraming Tests
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

#include "Xodus/IpcFraming.h"

using namespace Xodus;

namespace
{
    const HRESULT INVALID_DATA = HRESULT_FROM_WIN32( ERROR_INVALID_DATA );

    std::vector<uint8_t> Bytes( std::initializer_list<uint8_t> l ) { return std::vector<uint8_t>( l ); }
}

TEST( XodusIpcFraming, EncodeHeaderBytesForEachType )
{
    const uint8_t body[] = { 0xAA, 0xBB, 0xCC };
    for ( uint16_t type : { 1, 2, 3, 4, 5, 6, 7, 8 } )
    {
        auto out = Encode( type, body, sizeof( body ) );
        EXPECT_EQ( out, Bytes( { 0x58, 0x53, 0x44, 0x58, (uint8_t)type, 0x00, 0x03, 0x00, 0xAA, 0xBB, 0xCC } ) );
    }
    EXPECT_EQ( Encode( FRAME_PING, nullptr, 0 ), Bytes( { 0x58, 0x53, 0x44, 0x58, 0x01, 0x00, 0x00, 0x00 } ) );
}

TEST( XodusIpcFraming, MaxPayloadRoundTrips )
{
    std::vector<uint8_t> payload( 0xFFFF, 0x5A );
    auto out = Encode( FRAME_MSA_TOKEN_REQUEST, payload.data(), payload.size() );
    ASSERT_EQ( out.size(), FRAME_HEADER_SIZE + 0xFFFF );
    EXPECT_EQ( out[6], 0xFF );
    EXPECT_EQ( out[7], 0xFF );

    FrameDecoder d;
    d.Feed( out.data(), out.size() );
    Frame f;
    ASSERT_EQ( d.Next( f ), S_OK );
    EXPECT_EQ( f.type, FRAME_MSA_TOKEN_REQUEST );
    EXPECT_EQ( f.payload, payload );

    payload.push_back( 0 );                 /* one byte over the limit */
    EXPECT_TRUE( Encode( FRAME_MSA_TOKEN_REQUEST, payload.data(), payload.size() ).empty() );
}

TEST( XodusIpcFraming, FrameSplitAtEveryOffset )
{
    const uint8_t body[] = { 1, 2, 3, 4, 5 };
    auto wire = Encode( FRAME_PONG, body, sizeof( body ) );

    for ( size_t split = 0; split <= wire.size(); split++ )
    {
        FrameDecoder d;
        Frame f;
        d.Feed( wire.data(), split );
        if ( split < wire.size() )
        {
            EXPECT_EQ( d.Next( f ), S_FALSE ) << "split " << split;
            EXPECT_EQ( d.Pending(), split ) << "partial data must be kept";
        }
        d.Feed( wire.data() + split, wire.size() - split );
        ASSERT_EQ( d.Next( f ), S_OK ) << "split " << split;
        EXPECT_EQ( f.type, FRAME_PONG );
        EXPECT_EQ( f.payload, std::vector<uint8_t>( body, body + sizeof( body ) ) );
        EXPECT_EQ( d.Next( f ), S_FALSE );
        EXPECT_EQ( d.Pending(), 0u );
    }
}

TEST( XodusIpcFraming, TwoFramesInOneChunkReturnedOnceEach )
{
    const uint8_t a[] = { 9 }, b[] = { 8, 7 };
    auto wire = Encode( FRAME_PING, a, 1 );
    auto second = Encode( FRAME_PONG, b, 2 );
    wire.insert( wire.end(), second.begin(), second.end() );

    FrameDecoder d;
    d.Feed( wire.data(), wire.size() );
    Frame f;
    ASSERT_EQ( d.Next( f ), S_OK );
    EXPECT_EQ( f.type, FRAME_PING );
    EXPECT_EQ( f.payload, Bytes( { 9 } ) );
    ASSERT_EQ( d.Next( f ), S_OK );
    EXPECT_EQ( f.type, FRAME_PONG );
    EXPECT_EQ( f.payload, Bytes( { 8, 7 } ) );
    EXPECT_EQ( d.Next( f ), S_FALSE );
}

TEST( XodusIpcFraming, BadMagicErrorsWithoutConsuming )
{
    auto wire = Encode( FRAME_PING, nullptr, 0 );
    wire[0] ^= 0xFF;

    FrameDecoder d;
    d.Feed( wire.data(), wire.size() );
    Frame f;
    EXPECT_EQ( d.Next( f ), INVALID_DATA );
    EXPECT_EQ( d.Pending(), wire.size() );
    EXPECT_EQ( d.Next( f ), INVALID_DATA ) << "error is sticky, never skips ahead";
}

TEST( XodusIpcFraming, BadMagicDetectedBeforeFullHeader )
{
    const uint8_t junk[] = { 0x00, 0x11, 0x22, 0x33 };
    FrameDecoder d;
    d.Feed( junk, sizeof( junk ) );
    Frame f;
    EXPECT_EQ( d.Next( f ), INVALID_DATA );
}
