/*
 * XThreading Tests
 *  From https://github.com/microsoft/libHttpClient
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

#include "../test_fixtures.h"

#include <thread>

#include <windows.h>
#include <xasync.h>
#include <xasyncprovider.h>
#include <xgameruntimeinit.h>
#include <private/XAsync.hpp>

class XAsyncTests : public XGameRuntimeTests {};

TEST_F(XAsyncTests, WaitForResolve)
{
    std::shared_ptr<XGameRuntime::XAsync<int>> result;
    int value;
    ABI::XGameRuntime::XAsync<int>::Create( nullptr, []( int* result ) -> HRESULT { *result = 42; return S_OK; }, &result );
    result->GetResults( TRUE, &value );
    ASSERT_EQ( value, 42 );

    // How to use ABI::XGameRuntime::XAsync<int>?
}