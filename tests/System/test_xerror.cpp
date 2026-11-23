/*
 * XError Tests
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

#include <cstdio>
#include <string>
#include <vector>

#include <windows.h>
#include <xerror.h>
#include <xgameruntimeinit.h>

#ifndef DBG_PRINTEXCEPTION_C
#define DBG_PRINTEXCEPTION_C 0x40010006
#endif

struct CallbackRecord
{
    int calls = 0;
    HRESULT hr = 0;
    const char *msg = nullptr;
    void *ctx = nullptr;
    BOOLEAN result = FALSE;
};

static BOOLEAN WINAPI RecordingCallback( HRESULT hr, const char *msg, void *context )
{
    CallbackRecord *rec = static_cast<CallbackRecord *>( context );
    rec->calls++;
    rec->hr = hr;
    rec->msg = msg;
    rec->ctx = context;
    return rec->result;
}

static std::vector<std::string> *g_captured;

static LONG CALLBACK CaptureHandler( EXCEPTION_POINTERS *info )
{
    EXCEPTION_RECORD *rec = info->ExceptionRecord;
    if ( rec->ExceptionCode != DBG_PRINTEXCEPTION_C || !g_captured ) return EXCEPTION_CONTINUE_SEARCH;
    g_captured->push_back( reinterpret_cast<const char *>( rec->ExceptionInformation[1] ) );
    return EXCEPTION_CONTINUE_EXECUTION;
}

class XErrorTests : public XGameRuntimeTests
{
protected:
    using QueryFn = HRESULT (WINAPI *)( REFCLSID, REFIID, void ** );
    using ReportFn = HRESULT (WINAPI *)( HRESULT, LPCSTR );

    void SetUp() override
    {
        ASSERT_EQ( XGameRuntimeInitialize(), S_OK );
        HMODULE dll = GetModuleHandleA( "xgameruntime.dll" );
        ASSERT_NE( dll, nullptr );
        query = reinterpret_cast<QueryFn>( GetProcAddress( dll, "QueryApiImpl" ) );
        report = reinterpret_cast<ReportFn>( GetProcAddress( dll, "XErrorReport" ) );
        ASSERT_NE( query, nullptr );
        ASSERT_NE( report, nullptr );
        ASSERT_EQ( query( CLSID_XErrorImpl, IID_IXErrorImpl, reinterpret_cast<void **>( &error ) ), S_OK );

        // Every test starts from a known state.
        XErrorSetCallback( nullptr, nullptr );
        XErrorSetOptions( XErrorOptions::None, XErrorOptions::None );

        captured.clear();
        g_captured = &captured;
        handler = AddVectoredExceptionHandler( 1, CaptureHandler );
    }

    void TearDown() override
    {
        if ( handler ) RemoveVectoredExceptionHandler( handler );
        g_captured = nullptr;
        XErrorSetCallback( nullptr, nullptr );
        XErrorSetOptions( XErrorOptions::None, XErrorOptions::None );
        if ( error ) error->Release();
    }

    // The debugger is never attached in the test runner, so the not-present options decide.
    // The present options are set to None so that selecting them is detected.
    void SetOutputOptions( XErrorOptions notPresent )
    {
        XErrorSetOptions( XErrorOptions::None, notPresent );
    }

    static std::string Header( HRESULT hr )
    {
        char buf[64];
        snprintf( buf, sizeof( buf ), "XERROR: HR:%08X threadID:%d", static_cast<unsigned int>( hr ),
                  static_cast<int>( GetCurrentThreadId() ) );
        return buf;
    }

    QueryFn query = nullptr;
    ReportFn report = nullptr;
    IXErrorImpl *error = nullptr;
    PVOID handler = nullptr;
    std::vector<std::string> captured;
};

TEST_F( XErrorTests, QueryInterface )
{
    auto refs = [this] { ULONG n = error->AddRef(); error->Release(); return n - 1; };
    const ULONG base = refs();

    IUnknown *unk = nullptr;
    EXPECT_EQ( query( CLSID_XErrorImpl, IID_IUnknown, reinterpret_cast<void **>( &unk ) ), S_OK );
    ASSERT_NE( unk, nullptr );
    EXPECT_EQ( refs(), base + 1 );
    unk->Release();
    EXPECT_EQ( refs(), base );

    IXErrorImpl *impl = nullptr;
    EXPECT_EQ( query( CLSID_XErrorImpl, IID_IXErrorImpl, reinterpret_cast<void **>( &impl ) ), S_OK );
    EXPECT_EQ( impl, error );
    EXPECT_EQ( refs(), base + 1 );
    impl->Release();

    void *out = reinterpret_cast<void *>( 1 );
    const GUID other = { 0x12345678, 0x1234, 0x1234, { 1, 2, 3, 4, 5, 6, 7, 8 } };
    EXPECT_EQ( query( CLSID_XErrorImpl, other, &out ), E_NOINTERFACE );
    EXPECT_EQ( out, nullptr );

    EXPECT_EQ( error->QueryInterface( IID_IXErrorImpl, nullptr ), E_POINTER );
    EXPECT_EQ( refs(), base );
}

TEST_F( XErrorTests, ReportCallsCallbackThroughExport )
{
    CallbackRecord rec;
    XErrorSetCallback( RecordingCallback, &rec );

    EXPECT_EQ( report( E_FAIL, "boom" ), S_OK );
    EXPECT_EQ( rec.calls, 1 );
    EXPECT_EQ( rec.hr, E_FAIL );
    ASSERT_NE( rec.msg, nullptr );
    EXPECT_STREQ( rec.msg, "boom" );
    EXPECT_EQ( rec.ctx, &rec );
}

TEST_F( XErrorTests, NullMessageReachesCallback )
{
    CallbackRecord rec;
    XErrorSetCallback( RecordingCallback, &rec );

    EXPECT_EQ( report( E_FAIL, nullptr ), S_OK );
    EXPECT_EQ( rec.calls, 1 );
    EXPECT_EQ( rec.msg, nullptr );
}

TEST_F( XErrorTests, NullCallbackClearsCallback )
{
    CallbackRecord rec;
    XErrorSetCallback( RecordingCallback, &rec );
    XErrorSetCallback( nullptr, nullptr );

    report( E_FAIL, "x" );
    EXPECT_EQ( rec.calls, 0 );
}

TEST_F( XErrorTests, SecondCallbackReplacesFirst )
{
    CallbackRecord first, second;
    XErrorSetCallback( RecordingCallback, &first );
    XErrorSetCallback( RecordingCallback, &second );

    report( E_FAIL, "x" );
    EXPECT_EQ( first.calls, 0 );
    EXPECT_EQ( second.calls, 1 );
}

TEST_F( XErrorTests, DebugOutputFormat )
{
    SetOutputOptions( XErrorOptions::OutputDebugStringOnError );

    report( 0x80004005, "hello" );
    ASSERT_EQ( captured.size(), 1u );
    EXPECT_EQ( captured[0], Header( 0x80004005 ) + " hello\n" );
}

TEST_F( XErrorTests, NullMessageDebugOutput )
{
    SetOutputOptions( XErrorOptions::OutputDebugStringOnError );

    report( E_FAIL, nullptr );
    ASSERT_EQ( captured.size(), 1u );
    EXPECT_EQ( captured[0], Header( E_FAIL ) + " \n" );
}

TEST_F( XErrorTests, MessageOf191BytesIsOneCall )
{
    SetOutputOptions( XErrorOptions::OutputDebugStringOnError );

    std::string msg( 191, 'a' );
    report( E_FAIL, msg.c_str() );
    ASSERT_EQ( captured.size(), 1u );
    EXPECT_EQ( captured[0], Header( E_FAIL ) + " " + msg + "\n" );
}

TEST_F( XErrorTests, MessageOf192BytesIsThreeCalls )
{
    SetOutputOptions( XErrorOptions::OutputDebugStringOnError );

    std::string msg( 192, 'b' );
    report( E_FAIL, msg.c_str() );
    ASSERT_EQ( captured.size(), 3u );
    EXPECT_EQ( captured[0], Header( E_FAIL ) );
    EXPECT_EQ( captured[1], msg );
    EXPECT_EQ( captured[2], "\n" );
}

TEST_F( XErrorTests, OptionsDecideOutput )
{
    SetOutputOptions( XErrorOptions::None );
    report( E_FAIL, "x" );
    EXPECT_TRUE( captured.empty() );

    SetOutputOptions( XErrorOptions::FailFastOnError );
    report( E_FAIL, "x" );
    EXPECT_TRUE( captured.empty() );

    SetOutputOptions( XErrorOptions::DebugBreakOnError );
    report( E_FAIL, "x" );
    EXPECT_EQ( captured.size(), 1u );

    captured.clear();
    SetOutputOptions( XErrorOptions::OutputDebugStringOnError );
    report( E_FAIL, "x" );
    EXPECT_EQ( captured.size(), 1u );
}

TEST_F( XErrorTests, DebuggerPresentOptionsAreNotUsedWithoutDebugger )
{
    ASSERT_FALSE( IsDebuggerPresent() );

    XErrorSetOptions( XErrorOptions::OutputDebugStringOnError, XErrorOptions::None );
    report( E_FAIL, "x" );
    EXPECT_TRUE( captured.empty() );
}

TEST_F( XErrorTests, HandledErrorSkipsDebugOutput )
{
    SetOutputOptions( XErrorOptions::OutputDebugStringOnError );

    CallbackRecord rec;
    rec.result = TRUE;
    XErrorSetCallback( RecordingCallback, &rec );
    report( E_FAIL, "x" );
    EXPECT_EQ( rec.calls, 1 );
    EXPECT_TRUE( captured.empty() );

    // Any non-zero return means handled, not only 1.
    rec.result = 2;
    report( E_FAIL, "x" );
    EXPECT_EQ( rec.calls, 2 );
    EXPECT_TRUE( captured.empty() );

    rec.result = FALSE;
    report( E_FAIL, "x" );
    EXPECT_EQ( rec.calls, 3 );
    EXPECT_EQ( captured.size(), 1u );
}

TEST_F( XErrorTests, InterfaceAndExportShareState )
{
    SetOutputOptions( XErrorOptions::OutputDebugStringOnError );

    error->XErrorSetOptions( XErrorOptions::None, XErrorOptions::None );
    report( E_FAIL, "x" );
    EXPECT_TRUE( captured.empty() );

    CallbackRecord rec;
    error->XErrorSetCallback( RecordingCallback, &rec );
    report( E_FAIL, "x" );
    EXPECT_EQ( rec.calls, 1 );
}

// The vtable slots are the ABI of the genuine DLL: 3 Report, 4 SetCallback, 5 SetOptions.
// Call them by slot so that a reordered interface declaration is detected.
TEST_F( XErrorTests, VtableSlotsMatchGenuineLayout )
{
    void **vtbl = *reinterpret_cast<void ***>( error );
    auto slotReport = reinterpret_cast<void (WINAPI *)( IXErrorImpl *, HRESULT, LPCSTR )>( vtbl[3] );
    auto slotSetCallback = reinterpret_cast<void (WINAPI *)( IXErrorImpl *, XErrorCallback *, void * )>( vtbl[4] );
    auto slotSetOptions = reinterpret_cast<void (WINAPI *)( IXErrorImpl *, UINT32, UINT32 )>( vtbl[5] );

    CallbackRecord rec;
    slotSetCallback( error, RecordingCallback, &rec );
    slotSetOptions( error, 0, 1 );
    slotReport( error, E_FAIL, "slot" );

    EXPECT_EQ( rec.calls, 1 );
    EXPECT_EQ( rec.hr, E_FAIL );
    EXPECT_STREQ( rec.msg, "slot" );
    EXPECT_EQ( captured.size(), 1u );
}

// The default of the debugger-not-present options can only be seen in a process that never
// called XErrorSetOptions: this probe is run by DefaultNotPresentOptionsAreNone in a child process.
TEST( XErrorDefaultsProbe, DISABLED_Probe )
{
    ASSERT_EQ( XGameRuntimeInitialize(), S_OK );
    auto report = reinterpret_cast<HRESULT (WINAPI *)( HRESULT, LPCSTR )>(
        GetProcAddress( GetModuleHandleA( "xgameruntime.dll" ), "XErrorReport" ) );
    ASSERT_NE( report, nullptr );

    std::vector<std::string> captured;
    g_captured = &captured;
    PVOID handler = AddVectoredExceptionHandler( 1, CaptureHandler );
    report( E_FAIL, "x" );
    RemoveVectoredExceptionHandler( handler );
    g_captured = nullptr;
    EXPECT_TRUE( captured.empty() );
}

TEST( XErrorDefaults, DefaultNotPresentOptionsAreNone )
{
    char path[MAX_PATH];
    ASSERT_GT( GetModuleFileNameA( nullptr, path, MAX_PATH ), 0u );

    std::string cmd = std::string( "\"" ) + path +
        "\" --gtest_also_run_disabled_tests --gtest_filter=XErrorDefaultsProbe.DISABLED_Probe";
    STARTUPINFOA si = { sizeof( si ) };
    PROCESS_INFORMATION pi = {};
    ASSERT_TRUE( CreateProcessA( nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi ) );
    EXPECT_EQ( WaitForSingleObject( pi.hProcess, 60000 ), WAIT_OBJECT_0 );
    DWORD code = 1;
    GetExitCodeProcess( pi.hProcess, &code );
    EXPECT_EQ( code, 0u );
    CloseHandle( pi.hProcess );
    CloseHandle( pi.hThread );
}
