/*
 * XSystem C linkage Tests
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

#include <windows.h>
#include <gtest/gtest.h>

/*
 * This file declares the seven XSystem functions itself, in extern "C", and
 * includes none of the library headers. A C caller such as a game sees them the
 * same way, so the references below link only if the library exports the plain C
 * names. The functions are not called: the signatures here are only as exact as a
 * reference needs, and the other tests check what the functions do.
 */
extern "C"
{
    void __stdcall XSystemGetConsoleId( void );
    void __stdcall XSystemGetXboxLiveSandboxId( void );
    void __stdcall XSystemGetAppSpecificDeviceId( void );
    void __stdcall XSystemHandleTrack( void );
    void __stdcall XSystemIsHandleValid( void );
    void __stdcall XSystemAllowFullDownloadBandwidth( void );
    void __stdcall XSystemGetAnalyticsInfo( void );
}

TEST( XSystemLinkageTests, ACCallerFindsTheXSystemFunctions )
{
    void *volatile functions[] = {
        reinterpret_cast<void *>( &XSystemGetConsoleId ),
        reinterpret_cast<void *>( &XSystemGetXboxLiveSandboxId ),
        reinterpret_cast<void *>( &XSystemGetAppSpecificDeviceId ),
        reinterpret_cast<void *>( &XSystemHandleTrack ),
        reinterpret_cast<void *>( &XSystemIsHandleValid ),
        reinterpret_cast<void *>( &XSystemAllowFullDownloadBandwidth ),
        reinterpret_cast<void *>( &XSystemGetAnalyticsInfo ),
    };

    for ( void *function : functions )
    {
        EXPECT_NE( nullptr, function );
    }
}
