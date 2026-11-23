/*
 * Xbox Game runtime Library
 *  GDK Component: System API -> XAccessibility
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

#include "../private.h"
#include "XAccessibility.h"

#include <atomic>
#include <mutex>
#include <set>

/* Logging helper: %s of a null pointer is undefined. */
static inline const char *NullSafe( const char *str ) { return str ? str : "(null)"; }

class XAccessibilityImpl :
    public IXAccessibilityImpl2
{
public:
    HRESULT WINAPI QueryInterface( REFIID iid, void **out ) noexcept override
    {
        TRACE( "iface %p, iid %s, out %p.\n", this, debugstr_guid( &iid ), out );

        if (!out) return E_POINTER;
        *out = nullptr;

        if ( iid == __uuidof( IUnknown ) ||
             iid == __uuidof( IInspectable ) ||
             iid == __uuidof( IAgileObject ) ||
             iid == __uuidof( IXAccessibilityImpl ) ||
             iid == __uuidof( IXAccessibilityImpl2 ) )
        {
            AddRef();
            *out = static_cast<IXAccessibilityImpl2 *>(this);
            return S_OK;
        }

        FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( &iid ) );
        return E_NOINTERFACE;
    }

    ULONG WINAPI
    AddRef() noexcept override
    {
        ULONG curr = static_cast<ULONG>(++ref);
        TRACE( "iface %p increasing refcount to %lu.\n", this, curr );
        return curr;
    }

    ULONG WINAPI
    Release() noexcept override
    {
        ULONG curr = static_cast<ULONG>(--ref);
        TRACE( "iface %p decreasing refcount to %lu.\n", this, curr );

        // Static object, never deleted.
        return curr;
    }

    HRESULT WINAPI XClosedCaptionGetProperties( XClosedCaptionProperties *props ) override
    {
        TRACE( "props %p.\n", props );
        if ( !props ) return E_INVALIDARG;

        props->BackgroundColor.Value = 0xFF000000;
        props->WindowColor.Value = 0xFF000000;
        props->FontColor.Value = 0xFFFFFFFF;
        props->FontEdgeAttribute = XClosedCaptionFontEdgeAttribute::Default;
        props->FontStyle = XClosedCaptionFontStyle::Default;
        props->FontScale = 1.0f;
        props->Enabled = captionsEnabled;
        return S_OK;
    }

    HRESULT WINAPI XClosedCaptionSetEnabled( BOOLEAN enabled ) override
    {
        TRACE( "enabled %d.\n", enabled );
        captionsEnabled = enabled;
        return S_OK;
    }

    HRESULT WINAPI XHighContrastGetMode( XHighContrastMode *mode ) override
    {
        TRACE( "mode %p.\n", mode );
        if ( !mode ) return E_INVALIDARG;

        HIGHCONTRASTW hc = {};
        hc.cbSize = sizeof( hc );
        hc.lpszDefaultScheme = nullptr;
        bool ok = SystemParametersInfoW( SPI_GETHIGHCONTRAST, sizeof( hc ), &hc, 0 );

        *mode = ClassifyHighContrast( ok, hc.dwFlags, GetSysColor( COLOR_WINDOW ), GetSysColor( COLOR_WINDOWTEXT ) );
        return S_OK;
    }

    HRESULT WINAPI XSpeechToTextSetPositionHint( XSpeechToTextPositionHint position ) override
    {
        TRACE( "position %u.\n", static_cast<UINT32>( position ) );
        if ( static_cast<UINT32>( position ) > static_cast<UINT32>( XSpeechToTextPositionHint::TopRight ) )
            return E_INVALIDARG;
        return S_OK;
    }

    HRESULT WINAPI XSpeechToTextSendString( const char *speakerName, const char *content, XSpeechToTextType type ) override
    {
        TRACE( "speakerName %s, content %s, type %u.\n", NullSafe( speakerName ), NullSafe( content ), static_cast<UINT32>( type ) );
        if ( !speakerName || !content || !ValidType( type ) ) return E_INVALIDARG;
        return S_OK;
    }

    HRESULT WINAPI XSpeechSynthesizerEnumerateInstalledVoices( void *context, XSpeechSynthesizerInstalledVoicesCallback *callback ) override
    {
        TRACE( "context %p, callback %p.\n", context, callback );
        if ( !callback ) return E_INVALIDARG;
        return S_OK; // No installed voices.
    }

    HRESULT WINAPI XSpeechSynthesizerCreate( XSpeechSynthesizerHandle *speechSynthesizer ) override
    {
        TRACE( "speechSynthesizer %p.\n", speechSynthesizer );
        if ( !speechSynthesizer ) return E_INVALIDARG;
        *speechSynthesizer = nullptr;
        FIXME( "no speech engine, returning E_NOTIMPL.\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XSpeechSynthesizerCloseHandle( XSpeechSynthesizerHandle speechSynthesizer ) override
    {
        TRACE( "speechSynthesizer %p.\n", speechSynthesizer );
        return speechSynthesizer ? E_INVALIDARG : S_OK;
    }

    // No synthesizer handle can exist, so every call that takes one fails.
    HRESULT WINAPI XSpeechSynthesizerSetDefaultVoice( XSpeechSynthesizerHandle speechSynthesizer ) override
    {
        TRACE( "speechSynthesizer %p.\n", speechSynthesizer );
        return E_INVALIDARG;
    }

    HRESULT WINAPI XSpeechSynthesizerSetCustomVoice( XSpeechSynthesizerHandle speechSynthesizer, const char *voiceId ) override
    {
        TRACE( "speechSynthesizer %p, voiceId %s.\n", speechSynthesizer, NullSafe( voiceId ) );
        return E_INVALIDARG;
    }

    HRESULT WINAPI XSpeechSynthesizerCreateStreamFromText( XSpeechSynthesizerHandle speechSynthesizer, const char *text, XSpeechSynthesizerStreamHandle *speechSynthesisStream ) override
    {
        TRACE( "speechSynthesizer %p, text %s, speechSynthesisStream %p.\n", speechSynthesizer, NullSafe( text ), speechSynthesisStream );
        if ( speechSynthesisStream ) *speechSynthesisStream = nullptr;
        return E_INVALIDARG;
    }

    HRESULT WINAPI XSpeechSynthesizerCloseStreamHandle( XSpeechSynthesizerStreamHandle speechSynthesisStream ) override
    {
        TRACE( "speechSynthesisStream %p.\n", speechSynthesisStream );
        return speechSynthesisStream ? E_INVALIDARG : S_OK;
    }

    HRESULT WINAPI XSpeechSynthesizerGetStreamDataSize( XSpeechSynthesizerStreamHandle speechSynthesisStream, SIZE_T *bufferSize ) override
    {
        TRACE( "speechSynthesisStream %p, bufferSize %p.\n", speechSynthesisStream, bufferSize );
        return E_INVALIDARG;
    }

    HRESULT WINAPI XSpeechSynthesizerGetStreamData( XSpeechSynthesizerStreamHandle speechSynthesisStream, SIZE_T bufferSize, void *buffer, SIZE_T *bufferUsed ) override
    {
        TRACE( "speechSynthesisStream %p, bufferSize %Iu, buffer %p, bufferUsed %p.\n", speechSynthesisStream, bufferSize, buffer, bufferUsed );
        return E_INVALIDARG;
    }

    HRESULT WINAPI XSpeechToTextBeginHypothesisString( const char *speakerName, const char *content, XSpeechToTextType type, UINT32 *hypothesisId ) override
    {
        TRACE( "speakerName %s, content %s, type %u, hypothesisId %p.\n", NullSafe( speakerName ), NullSafe( content ), static_cast<UINT32>( type ), hypothesisId );
        if ( !hypothesisId ) return E_INVALIDARG;
        *hypothesisId = 0;
        if ( !speakerName || !content || !ValidType( type ) ) return E_INVALIDARG;

        std::lock_guard<std::mutex> lock( hypothesesLock );
        while ( nextHypothesisId == 0 || openHypotheses.count( nextHypothesisId ) )
            ++nextHypothesisId;
        *hypothesisId = nextHypothesisId++;
        openHypotheses.insert( *hypothesisId );
        return S_OK;
    }

    HRESULT WINAPI XSpeechToTextUpdateHypothesisString( UINT32 hypothesisId, const char *content ) override
    {
        TRACE( "hypothesisId %u, content %s.\n", hypothesisId, NullSafe( content ) );
        if ( !content ) return E_INVALIDARG;

        std::lock_guard<std::mutex> lock( hypothesesLock );
        return openHypotheses.count( hypothesisId ) ? S_OK : E_INVALIDARG;
    }

    HRESULT WINAPI XSpeechToTextFinalizeHypothesisString( UINT32 hypothesisId, const char *content ) override
    {
        TRACE( "hypothesisId %u, content %s.\n", hypothesisId, NullSafe( content ) );
        if ( !content ) return E_INVALIDARG;

        std::lock_guard<std::mutex> lock( hypothesesLock );
        return openHypotheses.erase( hypothesisId ) ? S_OK : E_INVALIDARG;
    }

    HRESULT WINAPI XSpeechToTextCancelHypothesisString( UINT32 hypothesisId ) override
    {
        TRACE( "hypothesisId %u.\n", hypothesisId );

        std::lock_guard<std::mutex> lock( hypothesesLock );
        return openHypotheses.erase( hypothesisId ) ? S_OK : E_INVALIDARG;
    }

    HRESULT WINAPI XSpeechSynthesizerCreateStreamFromSsml( XSpeechSynthesizerHandle speechSynthesizer, const char *ssml, XSpeechSynthesizerStreamHandle *speechSynthesisStream ) override
    {
        TRACE( "speechSynthesizer %p, ssml %s, speechSynthesisStream %p.\n", speechSynthesizer, NullSafe( ssml ), speechSynthesisStream );
        if ( speechSynthesisStream ) *speechSynthesisStream = nullptr;
        return E_INVALIDARG;
    }

private:
    static bool ValidType( XSpeechToTextType type )
    {
        return type == XSpeechToTextType::Voice || type == XSpeechToTextType::Text;
    }

    std::atomic_long ref{ 1 };
    std::atomic<BOOLEAN> captionsEnabled{ FALSE };
    std::mutex hypothesesLock;
    std::set<UINT32> openHypotheses;
    UINT32 nextHypothesisId = 1;
};

static XAccessibilityImpl g_x_accessibility;

IXAccessibilityImpl2 *x_accessibility_impl = static_cast<IXAccessibilityImpl2 *>(&g_x_accessibility);
