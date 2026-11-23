/*
 * XAccessibility Tests
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

#include <set>

#include <windows.h>
#include <xgameruntimeinit.h>
#include <xaccessibility.h>

#include "../../src/System/XAccessibility.h"

// CLSID / IIDs from include/xaccessibility.idl.
static const GUID CLSID_Accessibility = { 0x3e241977, 0x6237, 0x41e9, { 0x85, 0x59, 0x10, 0x2c, 0x2d, 0x99, 0x83, 0xf1 } };
static const GUID IID_Accessibility = CLSID_Accessibility;
static const GUID IID_Accessibility2 = { 0xd722b373, 0x8c4d, 0x4692, { 0x9e, 0x51, 0xd6, 0xad, 0x9b, 0x37, 0xaa, 0x7d } };
static const GUID IID_Unknown = { 0x00000000, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
static const GUID IID_Inspectable = { 0xaf86e2e0, 0xb12d, 0x4c6a, { 0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90 } };
static const GUID IID_Agile = { 0x94ea2b94, 0xe9cc, 0x49e0, { 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90 } };

static XSpeechSynthesizerHandle const BogusSynth = reinterpret_cast<XSpeechSynthesizerHandle>( 0x1234 );
static XSpeechSynthesizerStreamHandle const BogusStream = reinterpret_cast<XSpeechSynthesizerStreamHandle>( 0x5678 );

class XAccessibilityTests : public XGameRuntimeTests
{
protected:
    static void SetUpTestSuite()
    {
        XGameRuntimeTests::SetUpTestSuite();
        ASSERT_TRUE( SUCCEEDED( XGameRuntimeInitialize() ) );
    }

    static void TearDownTestSuite()
    {
        XGameRuntimeUninitialize();
        XGameRuntimeTests::TearDownTestSuite();
    }

    static HRESULT Query( REFIID iid, void **out )
    {
        using QueryFn = HRESULT ( WINAPI * )( REFCLSID, REFIID, void ** );
        HMODULE module = GetModuleHandleW( L"xgameruntime.dll" );
        EXPECT_NE( module, nullptr );
        auto query = reinterpret_cast<QueryFn>( GetProcAddress( module, "QueryApiImpl" ) );
        EXPECT_NE( query, nullptr );
        return query( CLSID_Accessibility, iid, out );
    }
};

// ---- REQ-001 ----

TEST_F( XAccessibilityTests, QueryAcceptedInterfaces )
{
    const GUID *iids[] = { &IID_Unknown, &IID_Inspectable, &IID_Agile, &IID_Accessibility, &IID_Accessibility2 };
    for ( const GUID *iid : iids )
    {
        IUnknown *obj = nullptr;
        HRESULT hr = Query( *iid, reinterpret_cast<void **>( &obj ) );
        EXPECT_EQ( hr, S_OK );
        ASSERT_NE( obj, nullptr );
        obj->Release();
    }
}

TEST_F( XAccessibilityTests, QueryUnknownInterfaceClearsOut )
{
    static const GUID bogus = { 0x12345678, 0x1234, 0x1234, { 1, 2, 3, 4, 5, 6, 7, 8 } };
    void *out = reinterpret_cast<void *>( 0xDEADBEEF );
    EXPECT_EQ( Query( bogus, &out ), E_NOINTERFACE );
    EXPECT_EQ( out, nullptr );
}

TEST_F( XAccessibilityTests, QueryAddsOneReference )
{
    IUnknown *first = nullptr;
    ASSERT_EQ( Query( IID_Unknown, reinterpret_cast<void **>( &first ) ), S_OK );
    ULONG before = first->AddRef();
    first->Release();

    IUnknown *second = nullptr;
    ASSERT_EQ( Query( IID_Accessibility2, reinterpret_cast<void **>( &second ) ), S_OK );
    ULONG after = second->AddRef();
    second->Release();
    EXPECT_EQ( after, before + 1 );

    // A failed query adds no reference.
    static const GUID bogus = { 0x12345678, 0x1234, 0x1234, { 1, 2, 3, 4, 5, 6, 7, 8 } };
    void *out = nullptr;
    EXPECT_EQ( Query( bogus, &out ), E_NOINTERFACE );
    ULONG unchanged = second->AddRef();
    second->Release();
    EXPECT_EQ( unchanged, after );

    second->Release();
    first->Release();
}

TEST_F( XAccessibilityTests, QueryNullOut )
{
    EXPECT_EQ( Query( IID_Unknown, nullptr ), E_POINTER );
}

// ---- REQ-003 ----

TEST_F( XAccessibilityTests, ClosedCaptionDefaults )
{
    XClosedCaptionProperties props;
    memset( &props, 0xCD, sizeof( props ) );
    ASSERT_EQ( XClosedCaptionGetProperties( &props ), S_OK );
    EXPECT_EQ( props.BackgroundColor.Value, 0xFF000000u );
    EXPECT_EQ( props.WindowColor.Value, 0xFF000000u );
    EXPECT_EQ( props.FontColor.Value, 0xFFFFFFFFu );
    EXPECT_EQ( props.FontEdgeAttribute, XClosedCaptionFontEdgeAttribute::Default );
    EXPECT_EQ( props.FontStyle, XClosedCaptionFontStyle::Default );
    EXPECT_EQ( props.FontScale, 1.0f );
    EXPECT_EQ( props.Enabled, FALSE );
}

TEST_F( XAccessibilityTests, ClosedCaptionEnabledRoundTrip )
{
    XClosedCaptionProperties props = {};
    EXPECT_EQ( XClosedCaptionSetEnabled( TRUE ), S_OK );
    ASSERT_EQ( XClosedCaptionGetProperties( &props ), S_OK );
    EXPECT_EQ( props.Enabled, TRUE );

    EXPECT_EQ( XClosedCaptionSetEnabled( FALSE ), S_OK );
    ASSERT_EQ( XClosedCaptionGetProperties( &props ), S_OK );
    EXPECT_EQ( props.Enabled, FALSE );
}

TEST_F( XAccessibilityTests, ClosedCaptionNullProps )
{
    EXPECT_EQ( XClosedCaptionGetProperties( nullptr ), E_INVALIDARG );
}

// ---- REQ-004 ----

static constexpr DWORD ON = HCF_HIGHCONTRASTON;
static constexpr COLORREF BLACK = RGB( 0, 0, 0 ), WHITE = RGB( 255, 255, 255 );

TEST( ClassifyHighContrast, FlagClearIsOff )
{
    EXPECT_EQ( ClassifyHighContrast( true, 0, BLACK, WHITE ), XHighContrastMode::Off );
    EXPECT_EQ( ClassifyHighContrast( true, 0, WHITE, BLACK ), XHighContrastMode::Off );
}

TEST( ClassifyHighContrast, DarkAndLight )
{
    EXPECT_EQ( ClassifyHighContrast( true, ON, BLACK, WHITE ), XHighContrastMode::Dark );
    EXPECT_EQ( ClassifyHighContrast( true, ON, WHITE, BLACK ), XHighContrastMode::Light );
}

TEST( ClassifyHighContrast, ThresholdBoundary )
{
    // Gray c has Y = c, so 127 is Dark and 128 is Light.
    EXPECT_EQ( ClassifyHighContrast( true, ON, RGB( 127, 127, 127 ), WHITE ), XHighContrastMode::Dark );
    EXPECT_EQ( ClassifyHighContrast( true, ON, RGB( 128, 128, 128 ), BLACK ), XHighContrastMode::Light );
}

TEST( ClassifyHighContrast, ExactWeightsAndComponentOrder )
{
    // COLORREF values (0x00BBGGRR) whose Y is 127 or 128 with the exact weights 299/587/114 and divisor 1000,
    // chosen so that any altered weight, divisor or component extraction lands on the other side of 128.
    const COLORREF dark[] = { 0x8b7199, 0x4d49ff, 0xab797d, 0xe89f1b, 0x4367c3, 0xa19607, 0x2367c6, 0x0000ff, 0xff0000 }; // Y = 127, 127, 127, 127, 126, 108, 123, 76 (pure red), 29 (pure blue)
    const COLORREF light[] = { 0x44cc02, 0xf53bdb, 0xb1708d, 0x78955a, 0x3d79c0, 0x6cfd73, 0x77eb6c, 0x00ff00 }; // Y = 128, 128, 128, 128, 135, 195, 183, 149 (pure green)
    for ( COLORREF color : dark )
        EXPECT_EQ( ClassifyHighContrast( true, ON, color, WHITE ), XHighContrastMode::Dark ) << std::hex << color;
    for ( COLORREF color : light )
        EXPECT_EQ( ClassifyHighContrast( true, ON, color, BLACK ), XHighContrastMode::Light ) << std::hex << color;
}

TEST( ClassifyHighContrast, EqualLuminanceIsOther )
{
    EXPECT_EQ( ClassifyHighContrast( true, ON, WHITE, WHITE ), XHighContrastMode::Other );
    EXPECT_EQ( ClassifyHighContrast( true, ON, BLACK, BLACK ), XHighContrastMode::Other );
}

TEST( ClassifyHighContrast, CallFailedIsOther )
{
    EXPECT_EQ( ClassifyHighContrast( false, 0, BLACK, WHITE ), XHighContrastMode::Other );
    EXPECT_EQ( ClassifyHighContrast( false, ON, BLACK, WHITE ), XHighContrastMode::Other );
}

TEST_F( XAccessibilityTests, HighContrastGetModeOffUnderWine )
{
    // Preset a value that is not a valid mode, so a missing write is caught.
    XHighContrastMode mode = static_cast<XHighContrastMode>( 0xCDCDCDCD );
    EXPECT_EQ( XHighContrastGetMode( &mode ), S_OK );
    EXPECT_EQ( mode, XHighContrastMode::Off );
}

TEST_F( XAccessibilityTests, HighContrastNullMode )
{
    EXPECT_EQ( XHighContrastGetMode( nullptr ), E_INVALIDARG );
}

// ---- REQ-005 ----

TEST_F( XAccessibilityTests, SpeechToTextPositionHint )
{
    for ( UINT32 i = 0; i <= 7; ++i )
        EXPECT_EQ( XSpeechToTextSetPositionHint( static_cast<XSpeechToTextPositionHint>( i ) ), S_OK ) << i;
    EXPECT_EQ( XSpeechToTextSetPositionHint( static_cast<XSpeechToTextPositionHint>( 8 ) ), E_INVALIDARG );
    EXPECT_EQ( XSpeechToTextSetPositionHint( static_cast<XSpeechToTextPositionHint>( 0xFFFFFFFF ) ), E_INVALIDARG );
}

TEST_F( XAccessibilityTests, SpeechToTextSendString )
{
    EXPECT_EQ( XSpeechToTextSendString( "who", "hello", XSpeechToTextType::Voice ), S_OK );
    EXPECT_EQ( XSpeechToTextSendString( "who", "hello", XSpeechToTextType::Text ), S_OK );
    EXPECT_EQ( XSpeechToTextSendString( nullptr, "hello", XSpeechToTextType::Voice ), E_INVALIDARG );
    EXPECT_EQ( XSpeechToTextSendString( "who", nullptr, XSpeechToTextType::Voice ), E_INVALIDARG );
    EXPECT_EQ( XSpeechToTextSendString( "who", "hello", static_cast<XSpeechToTextType>( 2 ) ), E_INVALIDARG );
}

TEST_F( XAccessibilityTests, HypothesisBeginArguments )
{
    UINT32 id = 0xABCD;
    EXPECT_EQ( XSpeechToTextBeginHypothesisString( nullptr, "c", XSpeechToTextType::Voice, &id ), E_INVALIDARG );
    EXPECT_EQ( id, 0u );

    id = 0xABCD;
    EXPECT_EQ( XSpeechToTextBeginHypothesisString( "s", nullptr, XSpeechToTextType::Voice, &id ), E_INVALIDARG );
    EXPECT_EQ( id, 0u );

    id = 0xABCD;
    EXPECT_EQ( XSpeechToTextBeginHypothesisString( "s", "c", static_cast<XSpeechToTextType>( 2 ), &id ), E_INVALIDARG );
    EXPECT_EQ( id, 0u );

    EXPECT_EQ( XSpeechToTextBeginHypothesisString( "s", "c", XSpeechToTextType::Voice, nullptr ), E_INVALIDARG );
}

TEST_F( XAccessibilityTests, HypothesisIdsAreUniqueAndNonZero )
{
    std::set<UINT32> ids;
    for ( int i = 0; i < 8; ++i )
    {
        UINT32 id = 0;
        ASSERT_EQ( XSpeechToTextBeginHypothesisString( "s", "c", XSpeechToTextType::Text, &id ), S_OK );
        EXPECT_NE( id, 0u );
        EXPECT_TRUE( ids.insert( id ).second ) << "duplicate id " << id;
    }
    for ( UINT32 id : ids )
        EXPECT_EQ( XSpeechToTextCancelHypothesisString( id ), S_OK );
}

TEST_F( XAccessibilityTests, HypothesisUpdateAndFinalize )
{
    UINT32 id = 0;
    ASSERT_EQ( XSpeechToTextBeginHypothesisString( "s", "c", XSpeechToTextType::Voice, &id ), S_OK );

    EXPECT_EQ( XSpeechToTextUpdateHypothesisString( id, "more" ), S_OK );
    EXPECT_EQ( XSpeechToTextUpdateHypothesisString( id, "even more" ), S_OK );
    EXPECT_EQ( XSpeechToTextUpdateHypothesisString( id, nullptr ), E_INVALIDARG );
    EXPECT_EQ( XSpeechToTextFinalizeHypothesisString( id, nullptr ), E_INVALIDARG );

    // A failed Finalize does not close the id.
    EXPECT_EQ( XSpeechToTextFinalizeHypothesisString( id, "done" ), S_OK );

    EXPECT_EQ( XSpeechToTextUpdateHypothesisString( id, "late" ), E_INVALIDARG );
    EXPECT_EQ( XSpeechToTextFinalizeHypothesisString( id, "again" ), E_INVALIDARG );
    EXPECT_EQ( XSpeechToTextCancelHypothesisString( id ), E_INVALIDARG );
}

TEST_F( XAccessibilityTests, HypothesisCancel )
{
    UINT32 id = 0;
    ASSERT_EQ( XSpeechToTextBeginHypothesisString( "s", "c", XSpeechToTextType::Voice, &id ), S_OK );
    EXPECT_EQ( XSpeechToTextCancelHypothesisString( id ), S_OK );
    EXPECT_EQ( XSpeechToTextCancelHypothesisString( id ), E_INVALIDARG );
    EXPECT_EQ( XSpeechToTextUpdateHypothesisString( id, "x" ), E_INVALIDARG );
    EXPECT_EQ( XSpeechToTextFinalizeHypothesisString( id, "x" ), E_INVALIDARG );
}

TEST_F( XAccessibilityTests, HypothesisNeverOpenedId )
{
    EXPECT_EQ( XSpeechToTextUpdateHypothesisString( 0, "x" ), E_INVALIDARG );
    EXPECT_EQ( XSpeechToTextFinalizeHypothesisString( 0, "x" ), E_INVALIDARG );
    EXPECT_EQ( XSpeechToTextCancelHypothesisString( 0 ), E_INVALIDARG );
}

// ---- REQ-006 ----

static bool s_voiceCallbackCalled;

static BOOLEAN __stdcall VoiceCallback( const XSpeechSynthesizerVoiceInformation *, void * )
{
    s_voiceCallbackCalled = true;
    return TRUE;
}

TEST_F( XAccessibilityTests, SynthesizerEnumerateVoices )
{
    s_voiceCallbackCalled = false;
    EXPECT_EQ( XSpeechSynthesizerEnumerateInstalledVoices( nullptr, VoiceCallback ), S_OK );
    EXPECT_FALSE( s_voiceCallbackCalled );
    EXPECT_EQ( XSpeechSynthesizerEnumerateInstalledVoices( nullptr, nullptr ), E_INVALIDARG );
}

TEST_F( XAccessibilityTests, SynthesizerCreate )
{
    XSpeechSynthesizerHandle synth = BogusSynth;
    EXPECT_EQ( XSpeechSynthesizerCreate( &synth ), E_NOTIMPL );
    EXPECT_EQ( synth, nullptr );
    EXPECT_EQ( XSpeechSynthesizerCreate( nullptr ), E_INVALIDARG );
}

TEST_F( XAccessibilityTests, SynthesizerCloseHandles )
{
    EXPECT_EQ( XSpeechSynthesizerCloseHandle( nullptr ), S_OK );
    EXPECT_EQ( XSpeechSynthesizerCloseHandle( BogusSynth ), E_INVALIDARG );
    EXPECT_EQ( XSpeechSynthesizerCloseStreamHandle( nullptr ), S_OK );
    EXPECT_EQ( XSpeechSynthesizerCloseStreamHandle( BogusStream ), E_INVALIDARG );
}

TEST_F( XAccessibilityTests, SynthesizerVoiceSelection )
{
    EXPECT_EQ( XSpeechSynthesizerSetDefaultVoice( nullptr ), E_INVALIDARG );
    EXPECT_EQ( XSpeechSynthesizerSetDefaultVoice( BogusSynth ), E_INVALIDARG );
    EXPECT_EQ( XSpeechSynthesizerSetCustomVoice( nullptr, "voice" ), E_INVALIDARG );
    EXPECT_EQ( XSpeechSynthesizerSetCustomVoice( BogusSynth, "voice" ), E_INVALIDARG );
    EXPECT_EQ( XSpeechSynthesizerSetCustomVoice( BogusSynth, nullptr ), E_INVALIDARG );
}

TEST_F( XAccessibilityTests, SynthesizerCreateStreams )
{
    XSpeechSynthesizerStreamHandle stream = BogusStream;
    EXPECT_EQ( XSpeechSynthesizerCreateStreamFromText( BogusSynth, "text", &stream ), E_INVALIDARG );
    EXPECT_EQ( stream, nullptr );
    stream = BogusStream;
    EXPECT_EQ( XSpeechSynthesizerCreateStreamFromText( nullptr, "text", &stream ), E_INVALIDARG );
    EXPECT_EQ( stream, nullptr );
    stream = BogusStream;
    EXPECT_EQ( XSpeechSynthesizerCreateStreamFromText( BogusSynth, nullptr, &stream ), E_INVALIDARG );
    EXPECT_EQ( stream, nullptr );
    EXPECT_EQ( XSpeechSynthesizerCreateStreamFromText( BogusSynth, "text", nullptr ), E_INVALIDARG );

    stream = BogusStream;
    EXPECT_EQ( XSpeechSynthesizerCreateStreamFromSsml( BogusSynth, "<speak/>", &stream ), E_INVALIDARG );
    EXPECT_EQ( stream, nullptr );
    stream = BogusStream;
    EXPECT_EQ( XSpeechSynthesizerCreateStreamFromSsml( nullptr, "<speak/>", &stream ), E_INVALIDARG );
    EXPECT_EQ( stream, nullptr );
    stream = BogusStream;
    EXPECT_EQ( XSpeechSynthesizerCreateStreamFromSsml( BogusSynth, nullptr, &stream ), E_INVALIDARG );
    EXPECT_EQ( stream, nullptr );
    EXPECT_EQ( XSpeechSynthesizerCreateStreamFromSsml( BogusSynth, "<speak/>", nullptr ), E_INVALIDARG );
}

TEST_F( XAccessibilityTests, SynthesizerStreamData )
{
    SIZE_T size = 0, used = 0;
    char buffer[16];
    EXPECT_EQ( XSpeechSynthesizerGetStreamDataSize( BogusStream, &size ), E_INVALIDARG );
    EXPECT_EQ( XSpeechSynthesizerGetStreamDataSize( nullptr, &size ), E_INVALIDARG );
    EXPECT_EQ( XSpeechSynthesizerGetStreamDataSize( BogusStream, nullptr ), E_INVALIDARG );
    EXPECT_EQ( XSpeechSynthesizerGetStreamData( BogusStream, sizeof( buffer ), buffer, &used ), E_INVALIDARG );
    EXPECT_EQ( XSpeechSynthesizerGetStreamData( nullptr, sizeof( buffer ), buffer, &used ), E_INVALIDARG );
    EXPECT_EQ( XSpeechSynthesizerGetStreamData( BogusStream, sizeof( buffer ), nullptr, &used ), E_INVALIDARG );
    EXPECT_EQ( XSpeechSynthesizerGetStreamData( BogusStream, sizeof( buffer ), buffer, nullptr ), E_INVALIDARG );
}
