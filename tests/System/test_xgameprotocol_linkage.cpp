/*
 * Xbox Game runtime Library
 *  Tests: XGameProtocol linkage
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
 * This file declares the two XGameProtocol functions itself, in extern "C", and
 * includes none of the library headers. A C caller such as a game sees them the
 * same way, so the references below link only if the library exports the plain C
 * names. The functions are not called: the signatures here are only as exact as a
 * reference needs, and the other tests check what the functions do.
 */
extern "C"
{
    void __stdcall XGameProtocolRegisterForActivation( void );
    void __stdcall XGameProtocolUnregisterForActivation( void );
}

TEST( XGameProtocolLinkageTests, ACCallerFindsTheXGameProtocolFunctions )
{
    void *volatile functions[] = {
        reinterpret_cast<void *>( &XGameProtocolRegisterForActivation ),
        reinterpret_cast<void *>( &XGameProtocolUnregisterForActivation ),
    };

    EXPECT_NE( functions[0], functions[1] );
}
