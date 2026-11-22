/*
 * Xbox Game runtime Library
 *  Tests
 *
 * Written by Weather
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

#include "test_fixtures.h"

#include <windows.h>
#include <xgameruntimeinit.h>
#include <private/logging.h>

namespace
{
    /* Code under test that links in directly (not through the DLL) logs through
     * the production logger, which needs initializing once in this process. */
    class LoggingEnvironment : public ::testing::Environment
    {
        void SetUp() override { InitializeLogging(); }
    };
    ::testing::Environment *const g_logging = ::testing::AddGlobalTestEnvironment( new LoggingEnvironment );
}

void XGameRuntimeTests::SetUpTestSuite()
{
}

void XGameRuntimeTests::TearDownTestSuite()
{
}