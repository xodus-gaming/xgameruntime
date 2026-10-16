/*
 * Xbox Game runtime Library
 *  Static Library -> Implementations
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

#include "private.h"

// This is mainly to prevent code repetition
template <typename T>
T
QueryApiImpl(
    REFCLSID clsid
) {
    HRESULT hr;
    T result = nullptr;

    // The library would actually try to initialize xgameruntime if it's not initialized yet when calling QueryApiImpl.
    ensureLoggerInitialized();

    if ( GlobalState::initialized || SUCCEEDED( hr = XGameRuntimeInitialize() ) )
    {
        hr = GlobalState::QueryApiImpl( clsid, IID_PPV_ARGS( &result ) );
        if ( FAILED ( hr ) )
        {
            if ( hr == REGDB_E_CLASSNOTREG )
            {
                LPCSTR errorMessage = "XGameRuntime is missing required dependencies, run GamingRepair.exe to resolve.";
                ERR("%s\n", errorMessage);
                GlobalState::XErrorReport( hr, errorMessage );
            }
            throw Exception( hr );
        }
    } else
        throw Exception( hr );

    return result;
}

template <typename R, typename F>
R
CallApi(
    F&& function
) {
    try
    {
        return std::forward<F>(function)();
    }
    catch (const Exception& e)
    {
        if constexpr ( std::is_same_v<R, HRESULT> )
        {
            return e.status;
        } else
        {
            return R{};
        }
    }
}

#define IMPL_CALL(ret, name, args, call_args)                                            \
    ret __stdcall name args                                                              \
    {                                                                                    \
        return CallApi<ret>([&] {                                                        \
            return QueryApiImpl<CONSUMED_INTERFACE>(CONSUMED_CLSID)->name call_args;     \
        });                                                                              \
    }

// --- XThreadingImpl --- //
#define CONSUMED_CLSID CLSID_XThreadingImpl

// --- IXThreadingImpl --- //
#define CONSUMED_INTERFACE IXThreadingImpl*

IMPL_CALL( HRESULT, XAsyncGetStatus, (XAsyncBlock* asyncBlock, BOOLEAN wait), (asyncBlock, wait) )
IMPL_CALL( HRESULT, XAsyncGetResultSize, (XAsyncBlock *asyncBlock, SIZE_T *bufferSize), (asyncBlock, bufferSize) )
IMPL_CALL( void, XAsyncCancel, (XAsyncBlock* asyncBlock), (asyncBlock) )
IMPL_CALL( HRESULT, XAsyncRun, (XAsyncBlock *asyncBlock, XAsyncWork *work), (asyncBlock, work) )
IMPL_CALL( HRESULT, XAsyncBegin, (XAsyncBlock *asyncBlock, void *context, const void *identity, const char *identityName, XAsyncProvider *provider), (asyncBlock, context, identity, identityName, provider) )
IMPL_CALL( HRESULT, XAsyncSchedule, (XAsyncBlock* asyncBlock, UINT32 delayInMs), (asyncBlock, delayInMs) )
IMPL_CALL( void, XAsyncComplete, (XAsyncBlock* asyncBlock, HRESULT result, SIZE_T requiredBufferSize), (asyncBlock, result, requiredBufferSize) )
IMPL_CALL( HRESULT, XAsyncGetResult, (XAsyncBlock *asyncBlock, const void *identity, SIZE_T bufferSize, void *buffer, SIZE_T *bufferUsed), (asyncBlock, identity, bufferSize, buffer, bufferUsed) )
IMPL_CALL( HRESULT, XTaskQueueCreate, (XTaskQueueDispatchMode workDispatchMode, XTaskQueueDispatchMode completionDispatchMode, XTaskQueueHandle *queue), (workDispatchMode, completionDispatchMode, queue) )
IMPL_CALL( HRESULT, XTaskQueueCreateComposite, (XTaskQueuePortHandle workPort, XTaskQueuePortHandle completionPort, XTaskQueueHandle *queue), (workPort, completionPort, queue) )
IMPL_CALL( HRESULT, XTaskQueueGetPort, (XTaskQueueHandle queue, XTaskQueuePort port, XTaskQueuePortHandle *portHandle), (queue, port, portHandle) )
IMPL_CALL( HRESULT, XTaskQueueDuplicateHandle, (XTaskQueueHandle queueHandle, XTaskQueueHandle *duplicatedHandle), (queueHandle, duplicatedHandle) )
IMPL_CALL( BOOLEAN, XTaskQueueDispatch, (XTaskQueueHandle queue, XTaskQueuePort port, UINT32 timeoutInMs), (queue, port, timeoutInMs) )
IMPL_CALL( BOOLEAN, XTaskQueueIsEmpty, (XTaskQueueHandle queue, XTaskQueuePort port), (queue, port) )
IMPL_CALL( void, XTaskQueueCloseHandle, (XTaskQueueHandle queue), (queue) )
IMPL_CALL( HRESULT, XTaskQueueSubmitCallback, (XTaskQueueHandle queue, XTaskQueuePort port, void *callbackContext, XTaskQueueCallback *callback), (queue, port, callbackContext, callback) )
IMPL_CALL( HRESULT, XTaskQueueSubmitDelayedCallback, (XTaskQueueHandle queue, XTaskQueuePort port, UINT32 delayMs, void *callbackContext, XTaskQueueCallback *callback), (queue, port, delayMs, callbackContext, callback) )
IMPL_CALL( HRESULT, XTaskQueueRegisterWaiter, (XTaskQueueHandle queue, XTaskQueuePort port, HANDLE waitHandle, void *callbackContext, XTaskQueueCallback *callback, XTaskQueueRegistrationToken *token), (queue, port, waitHandle, callbackContext, callback, token) )
IMPL_CALL( void, XTaskQueueUnregisterWaiter, (XTaskQueueHandle queue, XTaskQueueRegistrationToken token), (queue, token) )
IMPL_CALL( HRESULT, XTaskQueueTerminate, (XTaskQueueHandle queue, BOOLEAN wait, void *callbackContext, XTaskQueueTerminatedCallback *callback), (queue, wait, callbackContext, callback) )
IMPL_CALL( HRESULT, XTaskQueueRegisterMonitor, (XTaskQueueHandle queue, void *callbackContext, XTaskQueueMonitorCallback *callback, XTaskQueueRegistrationToken *token), (queue, callbackContext, callback, token) )
IMPL_CALL( void, XTaskQueueUnregisterMonitor, (XTaskQueueHandle queue, XTaskQueueRegistrationToken token), (queue, token) )
IMPL_CALL( BOOLEAN, XTaskQueueGetCurrentProcessTaskQueue, (XTaskQueueHandle *queue), (queue) )
IMPL_CALL( void, XTaskQueueSetCurrentProcessTaskQueue, (XTaskQueueHandle queue), (queue) )
IMPL_CALL( HRESULT, XThreadSetTimeSensitive, (BOOLEAN isTimeSensitiveThread), (isTimeSensitiveThread) )
IMPL_CALL( void, XThreadAssertNotTimeSensitive, (), () )
IMPL_CALL( BOOLEAN, XThreadIsTimeSensitive, (), () )

#undef CONSUMED_INTERFACE

#undef CONSUMED_CLSID



// --- XAccessibilityImpl --- //
#define CONSUMED_CLSID CLSID_XAccessibilityImpl

// --- IXAccessibilityImpl --- //
#define CONSUMED_INTERFACE IXAccessibilityImpl*

IMPL_CALL( HRESULT, XClosedCaptionGetProperties, (XClosedCaptionProperties *props), (props) )
IMPL_CALL( HRESULT, XClosedCaptionSetEnabled, (BOOLEAN enabled), (enabled) )
IMPL_CALL( HRESULT, XHighContrastGetMode, (XHighContrastMode *mode), (mode) )
IMPL_CALL( HRESULT, XSpeechToTextSetPositionHint, (XSpeechToTextPositionHint position), (position) )
IMPL_CALL( HRESULT, XSpeechToTextSendString, (const char *speakerName, const char *content, XSpeechToTextType type), (speakerName, content, type) )
IMPL_CALL( HRESULT, XSpeechSynthesizerEnumerateInstalledVoices, (void *context, XSpeechSynthesizerInstalledVoicesCallback *callback), (context, callback) )
IMPL_CALL( HRESULT, XSpeechSynthesizerCreate, (XSpeechSynthesizerHandle *speechSynthesizer), (speechSynthesizer) )
IMPL_CALL( HRESULT, XSpeechSynthesizerCloseHandle, (XSpeechSynthesizerHandle speechSynthesizer), (speechSynthesizer) )
IMPL_CALL( HRESULT, XSpeechSynthesizerSetDefaultVoice, (XSpeechSynthesizerHandle speechSynthesizer), (speechSynthesizer) )
IMPL_CALL( HRESULT, XSpeechSynthesizerSetCustomVoice, (XSpeechSynthesizerHandle speechSynthesizer, const char *voiceId), (speechSynthesizer, voiceId) )
IMPL_CALL( HRESULT, XSpeechSynthesizerCreateStreamFromText, (XSpeechSynthesizerHandle speechSynthesizer, const char *text, XSpeechSynthesizerStreamHandle *speechSynthesisStream), (speechSynthesizer, text, speechSynthesisStream) )
IMPL_CALL( HRESULT, XSpeechSynthesizerCloseStreamHandle, (XSpeechSynthesizerStreamHandle speechSynthesisStream), (speechSynthesisStream) )
IMPL_CALL( HRESULT, XSpeechSynthesizerGetStreamDataSize, (XSpeechSynthesizerStreamHandle speechSynthesisStream, SIZE_T *bufferSize), (speechSynthesisStream, bufferSize) )
IMPL_CALL( HRESULT, XSpeechSynthesizerGetStreamData, (XSpeechSynthesizerStreamHandle speechSynthesisStream, SIZE_T bufferSize, void *buffer, SIZE_T *bufferUsed), (speechSynthesisStream, bufferSize, buffer, bufferUsed) )
IMPL_CALL( HRESULT, XSpeechToTextBeginHypothesisString, (const char *speakerName, const char *content, XSpeechToTextType type, UINT32 *hypothesisId), (speakerName, content, type, hypothesisId) )
IMPL_CALL( HRESULT, XSpeechToTextUpdateHypothesisString, (UINT32 hypothesisId, const char *content), (hypothesisId, content) )
IMPL_CALL( HRESULT, XSpeechToTextFinalizeHypothesisString, (UINT32 hypothesisId, const char *content), (hypothesisId, content) )
IMPL_CALL( HRESULT, XSpeechToTextCancelHypothesisString, (UINT32 hypothesisId), (hypothesisId) )
IMPL_CALL( HRESULT, XSpeechSynthesizerCreateStreamFromSsml, (XSpeechSynthesizerHandle speechSynthesizer, const char *ssml, XSpeechSynthesizerStreamHandle *speechSynthesisStream), (speechSynthesizer, ssml, speechSynthesisStream) )

#undef CONSUMED_INTERFACE

// --- IXAccessibilityImpl2 --- //
#define CONSUMED_INTERFACE IXAccessibilityImpl2*

#undef CONSUMED_INTERFACE

#undef CONSUMED_CLSID



// --- XAppCaptureMetadataImpl --- //
#define CONSUMED_CLSID CLSID_XAppCaptureMetadataImpl

// --- IXAppCaptureMetadataImpl --- //
#define CONSUMED_INTERFACE IXAppCaptureMetadataImpl*

IMPL_CALL( BOOLEAN, XAppBroadcastIsAppBroadcasting, (), () )
IMPL_CALL( HRESULT, XAppBroadcastShowUI, (XUserHandle requestingUser), (requestingUser) )
IMPL_CALL( HRESULT, XAppBroadcastGetStatus, (XUserHandle requestingUser, XAppBroadcastStatus *appBroadcastStatus), (requestingUser, appBroadcastStatus) )
IMPL_CALL( HRESULT, XAppBroadcastRegisterIsAppBroadcastingChanged, (XTaskQueueHandle queue, void *context, XAppBroadcastMonitorCallback *appBroadcastMonitorCallback, XTaskQueueRegistrationToken *token), (queue, context, appBroadcastMonitorCallback, token) )
IMPL_CALL( BOOLEAN, XAppBroadcastUnregisterIsAppBroadcastingChanged, (XTaskQueueRegistrationToken token, BOOLEAN wait), (token, wait) )
IMPL_CALL( HRESULT, XAppCaptureMetadataAddStringEvent, (const char *name, const char *value, XAppCaptureMetadataPriority priority), (name, value, priority) )
IMPL_CALL( HRESULT, XAppCaptureMetadataAddInt32Event, (const char *name, INT32 value, XAppCaptureMetadataPriority priority), (name, value, priority) )
IMPL_CALL( HRESULT, XAppCaptureMetadataAddDoubleEvent, (const char *name, double value, XAppCaptureMetadataPriority priority), (name, value, priority) )
IMPL_CALL( HRESULT, XAppCaptureMetadataStartStringState, (const char *name, const char *value, XAppCaptureMetadataPriority priority), (name, value, priority) )
IMPL_CALL( HRESULT, XAppCaptureMetadataStartInt32State, (const char *name, INT32 value, XAppCaptureMetadataPriority priority), (name, value, priority) )
IMPL_CALL( HRESULT, XAppCaptureMetadataStartDoubleState, (const char *name, double value, XAppCaptureMetadataPriority priority), (name, value, priority) )
IMPL_CALL( HRESULT, XAppCaptureMetadataStopState, (const char *name), (name) )
IMPL_CALL( HRESULT, XAppCaptureMetadataStopAllStates, (), () )
IMPL_CALL( HRESULT, XAppCaptureMetadataRemainingStorageBytesAvailable, (UINT64 *value), (value) )
IMPL_CALL( HRESULT, XAppCaptureRegisterMetadataPurged, (XTaskQueueHandle queue, void *context, XAppCaptureMetadataPurgedCallback *callback, XTaskQueueRegistrationToken *token), (queue, context, callback, token) )
IMPL_CALL( BOOLEAN, XAppCaptureUnRegisterMetadataPurged, (XTaskQueueRegistrationToken token, BOOLEAN wait), (token, wait) )

#undef CONSUMED_INTERFACE

// --- IXAppCaptureImpl --- //
#define CONSUMED_INTERFACE IXAppCaptureImpl*

IMPL_CALL( HRESULT, XAppCaptureTakeDiagnosticScreenshot, (BOOLEAN gamescreenOnly, XAppCaptureScreenshotFormatFlag captureFlags, const char *filenamePrefix, XAppCaptureDiagnosticScreenshotResult *result), (gamescreenOnly, captureFlags, filenamePrefix, result) )
IMPL_CALL( HRESULT, XAppCaptureRecordDiagnosticClip, (time_t startTime, UINT32 durationInMs, const char *filenamePrefix, XAppCaptureRecordClipResult *result), (startTime, durationInMs, filenamePrefix, result) )
IMPL_CALL( HRESULT, XAppCaptureTakeScreenshot, (XUserHandle requestingUser, XAppCaptureTakeScreenshotResult *result), (requestingUser, result) )
IMPL_CALL( HRESULT, XAppCaptureOpenScreenshotStream, (const char *localId, XAppCaptureScreenshotFormatFlag screenshotFormat, XAppCaptureScreenshotStreamHandle *handle, UINT64 *totalBytes), (localId, screenshotFormat, handle, totalBytes) )
IMPL_CALL( HRESULT, XAppCaptureReadScreenshotStream, (XAppCaptureScreenshotStreamHandle handle, UINT64 startPosition, UINT32 bytesToRead, UINT8 *buffer, UINT32 *bytesWritten), (handle, startPosition, bytesToRead, buffer, bytesWritten) )
IMPL_CALL( HRESULT, XAppCaptureCloseScreenshotStream, (XAppCaptureScreenshotStreamHandle handle), (handle) )
IMPL_CALL( HRESULT, XAppCaptureEnableRecord, (), () )
IMPL_CALL( HRESULT, XAppCaptureDisableRecord, (), () )

#undef CONSUMED_INTERFACE

// --- IXAppCaptureImpl2 --- //
#define CONSUMED_INTERFACE IXAppCaptureImpl2*

IMPL_CALL( HRESULT, XAppCaptureGetVideoCaptureSettings, (XAppCaptureVideoCaptureSettings *userCaptureSettings), (userCaptureSettings) )
IMPL_CALL( HRESULT, XAppCaptureRecordTimespan, (const SYSTEMTIME *startTimestamp, UINT64 durationInMilliseconds, XAppCaptureLocalResult *result), (startTimestamp, durationInMilliseconds, result) )
IMPL_CALL( HRESULT, XAppCaptureReadLocalStream, (XAppCaptureLocalStreamHandle handle, SIZE_T startPosition, UINT32 bytesToRead, UINT8 *buffer, UINT32 *bytesWritten), (handle, startPosition, bytesToRead, buffer, bytesWritten) )
IMPL_CALL( HRESULT, XAppCaptureCloseLocalStream, (XAppCaptureLocalStreamHandle handle), (handle) )

#undef CONSUMED_INTERFACE

// --- IXAppCaptureImpl3 --- //
#define CONSUMED_INTERFACE IXAppCaptureImpl3*

IMPL_CALL( HRESULT, XAppCaptureStartUserRecord, (XUserHandle requestingUser, UINT32 localIdBufferLength, char *localIdBuffer), (requestingUser, localIdBufferLength, localIdBuffer) )
IMPL_CALL( HRESULT, XAppCaptureStopUserRecord, (const char *localId, XAppCaptureUserRecordingResult *result), (localId, result) )

#undef CONSUMED_INTERFACE

// --- IXAppCaptureImpl4 --- //
#define CONSUMED_INTERFACE IXAppCaptureImpl4*

IMPL_CALL( HRESULT, XAppCaptureCancelUserRecord, (const char *localId), (localId) )

#undef CONSUMED_INTERFACE

#undef CONSUMED_CLSID



// --- XDisplayImpl --- //
#define CONSUMED_CLSID CLSID_XDisplayImpl

// --- IXDisplayImpl --- //
#define CONSUMED_INTERFACE IXDisplayImpl*

IMPL_CALL( XDisplayHdrModeResult, XDisplayTryEnableHdrMode, (XDisplayHdrModePreference displayModePreference, XDisplayHdrModeInfo *displayHdrModeInfo), (displayModePreference, displayHdrModeInfo) )

#undef CONSUMED_INTERFACE

#undef CONSUMED_CLSID



// --- XLauncherImpl --- //
#define CONSUMED_CLSID CLSID_XLauncherImpl

// --- IXLauncherImpl --- //
#define CONSUMED_INTERFACE IXLauncherImpl*

IMPL_CALL( HRESULT, XLaunchUri, (XUserHandle user, const char *uri), (user, uri) )
IMPL_CALL( HRESULT, XDisplayAcquireTimeoutDeferral, (XDisplayTimeoutDeferralHandle *handle), (handle) )
IMPL_CALL( void, XDisplayCloseTimeoutDeferralHandle, (XDisplayTimeoutDeferralHandle handle), (handle) )

#undef CONSUMED_INTERFACE

#undef CONSUMED_CLSID



// --- XErrorImpl --- //
#define CONSUMED_CLSID CLSID_XErrorImpl

// --- IXErrorImpl --- //
#define CONSUMED_INTERFACE IXErrorImpl*

IMPL_CALL( void, XErrorSetCallback, (XErrorCallback *callback, void *context), (callback, context) )
IMPL_CALL( void, XErrorSetOptions, (XErrorOptions optionsDebuggerPresent, XErrorOptions optionsDebuggerNotPresent), (optionsDebuggerPresent, optionsDebuggerNotPresent) )

#undef CONSUMED_INTERFACE

#undef CONSUMED_CLSID



// --- XGameImpl --- //
#define CONSUMED_CLSID CLSID_XGameImpl

// --- IXGameImpl --- //
#define CONSUMED_INTERFACE IXGameImpl*

IMPL_CALL( HRESULT, XGameGetXboxTitleId, (UINT32 *titleId), (titleId) )

#undef CONSUMED_INTERFACE

// --- IXGameImpl2 --- //
#define CONSUMED_INTERFACE IXGameImpl2*

IMPL_CALL( void, XLaunchNewGame, (const char *exePath, const char *args, XUserHandle defaultUser), (exePath, args, defaultUser) )

#undef CONSUMED_INTERFACE

// --- IXGameImpl3 --- //
#define CONSUMED_INTERFACE IXGameImpl3*

IMPL_CALL( HRESULT, XLaunchRestartOnCrash, (const char *args, UINT32 reserved), (args, reserved) )

#undef CONSUMED_INTERFACE

#undef CONSUMED_CLSID



// --- XGameActivationImpl --- //
#define CONSUMED_CLSID CLSID_XGameActivationImpl

// --- IXGameActivationImpl --- //
#define CONSUMED_INTERFACE IXGameActivationImpl*

IMPL_CALL( HRESULT, XGameActivationRegisterForEvent, (XTaskQueueHandle queue, void *context, XGameActivationCallback *callback, XTaskQueueRegistrationToken *token), (queue, context, callback, token) )
IMPL_CALL( BOOLEAN, XGameActivationUnregisterForEvent, (XTaskQueueRegistrationToken token, BOOLEAN wait), (token, wait) )
IMPL_CALL( HRESULT, XGameActivationAcceptPendingInvite, (const char *inviteUri), (inviteUri) )

#undef CONSUMED_INTERFACE

#undef CONSUMED_CLSID



// --- XGameEventImpl --- //
#define CONSUMED_CLSID CLSID_XGameEventImpl

// --- IXGameEventImpl --- //
#define CONSUMED_INTERFACE IXGameEventImpl*

IMPL_CALL( HRESULT, XGameEventWrite, (XUserHandle user, const char *serviceConfigId, const char *playSessionId, const char *eventName, const char *dimensionsJson, const char *measurementsJson), (user, serviceConfigId, playSessionId, eventName, dimensionsJson, measurementsJson) )

#undef CONSUMED_INTERFACE

#undef CONSUMED_CLSID



// --- XGameInviteImpl --- //
#define CONSUMED_CLSID CLSID_XGameInviteImpl

// --- IXGameInviteImpl --- //
#define CONSUMED_INTERFACE IXGameInviteImpl*

IMPL_CALL( HRESULT, XGameInviteRegisterForEvent, (XTaskQueueHandle queue, void *context, XGameInviteEventCallback *callback, XTaskQueueRegistrationToken *token), (queue, context, callback, token) )
IMPL_CALL( BOOLEAN, XGameInviteUnregisterForEvent, (XTaskQueueRegistrationToken token, BOOLEAN wait), (token, wait) )

#undef CONSUMED_INTERFACE

// --- IXGameInviteImpl2 --- //
#define CONSUMED_INTERFACE IXGameInviteImpl2*

IMPL_CALL( HRESULT, XGameInviteRegisterForPendingEvent, (XTaskQueueHandle queue, void *context, XGameInviteEventCallback *callback, XTaskQueueRegistrationToken *token), (queue, context, callback, token) )
IMPL_CALL( BOOLEAN, XGameInviteUnregisterForPendingEvent, (XTaskQueueRegistrationToken token, BOOLEAN wait), (token, wait) )
IMPL_CALL( HRESULT, XGameInviteAcceptPendingInvite, (const char *inviteUri), (inviteUri) )

#undef CONSUMED_INTERFACE

#undef CONSUMED_CLSID



// --- XGameProtocolImpl --- //
#define CONSUMED_CLSID CLSID_XGameProtocolImpl

// --- IXGameProtocolImpl --- //
#define CONSUMED_INTERFACE IXGameProtocolImpl*

IMPL_CALL( HRESULT, XGameProtocolRegisterForActivation, (XTaskQueueHandle queue, void *context, XGameProtocolActivationCallback *callback, XTaskQueueRegistrationToken *token), (queue, context, callback, token) )
IMPL_CALL( BOOLEAN, XGameProtocolUnregisterForActivation, (XTaskQueueRegistrationToken token, BOOLEAN wait), (token, wait) )

#undef CONSUMED_INTERFACE

#undef CONSUMED_CLSID



// --- XGameRuntimeFeatureImpl --- //
#define CONSUMED_CLSID CLSID_XGameRuntimeFeatureImpl

// --- IXGameRuntimeFeatureImpl --- //
#define CONSUMED_INTERFACE IXGameRuntimeFeatureImpl*

IMPL_CALL( BOOLEAN, XGameRuntimeIsFeatureAvailable, (XGameRuntimeFeature feature), (feature) )

#undef CONSUMED_INTERFACE

#undef CONSUMED_CLSID



// --- XGameSaveImpl --- //
#define CONSUMED_CLSID CLSID_XGameSaveImpl

// --- IXGameSaveImpl --- //
#define CONSUMED_INTERFACE IXGameSaveImpl*

IMPL_CALL( HRESULT, XGameSaveInitializeProvider, (XUserHandle requestingUser, const char *configurationId, BOOLEAN syncOnDemand, XGameSaveProviderHandle *provider), (requestingUser, configurationId, syncOnDemand, provider) )
IMPL_CALL( HRESULT, XGameSaveInitializeProviderAsync, (XUserHandle requestingUser, const char *configurationId, BOOLEAN syncOnDemand, XAsyncBlock *async), (requestingUser, configurationId, syncOnDemand, async) )
IMPL_CALL( HRESULT, XGameSaveInitializeProviderResult, (XAsyncBlock *async, XGameSaveProviderHandle *provider), (async, provider) )
IMPL_CALL( void, XGameSaveCloseProvider, (XGameSaveProviderHandle provider), (provider) )
IMPL_CALL( HRESULT, XGameSaveGetRemainingQuota, (XGameSaveProviderHandle provider, INT64 *remainingQuota), (provider, remainingQuota) )
IMPL_CALL( HRESULT, XGameSaveGetRemainingQuotaAsync, (XGameSaveProviderHandle provider, XAsyncBlock *async), (provider, async) )
IMPL_CALL( HRESULT, XGameSaveGetRemainingQuotaResult, (XAsyncBlock *async, INT64 *remainingQuota), (async, remainingQuota) )
IMPL_CALL( HRESULT, XGameSaveDeleteContainer, (XGameSaveProviderHandle provider, const char *containerName), (provider, containerName) )
IMPL_CALL( HRESULT, XGameSaveDeleteContainerAsync, (XGameSaveProviderHandle provider, const char *containerName, XAsyncBlock *async), (provider, containerName, async) )
IMPL_CALL( HRESULT, XGameSaveDeleteContainerResult, (XAsyncBlock *async), (async) )
IMPL_CALL( HRESULT, XGameSaveGetContainerInfo, (XGameSaveProviderHandle provider, const char *containerName, void *context, XGameSaveContainerInfoCallback *callback), (provider, containerName, context, callback) )
IMPL_CALL( HRESULT, XGameSaveEnumerateContainerInfo, (XGameSaveProviderHandle provider, void *context, XGameSaveContainerInfoCallback *callback), (provider, context, callback) )
IMPL_CALL( HRESULT, XGameSaveEnumerateContainerInfoByName, (XGameSaveProviderHandle provider, const char *containerNamePrefix, void *context, XGameSaveContainerInfoCallback *callback), (provider, containerNamePrefix, context, callback) )
IMPL_CALL( HRESULT, XGameSaveCreateContainer, (XGameSaveProviderHandle provider, const char *containerName, XGameSaveContainerHandle *containerContext), (provider, containerName, containerContext) )
IMPL_CALL( void, XGameSaveCloseContainer, (XGameSaveContainerHandle context), (context) )
IMPL_CALL( HRESULT, XGameSaveEnumerateBlobInfo, (XGameSaveContainerHandle container, void *context, XGameSaveBlobInfoCallback *callback), (container, context, callback) )
IMPL_CALL( HRESULT, XGameSaveEnumerateBlobInfoByName, (XGameSaveContainerHandle container, const char *blobNamePrefix, void *context, XGameSaveBlobInfoCallback *callback), (container, blobNamePrefix, context, callback) )
IMPL_CALL( HRESULT, XGameSaveReadBlobData, (XGameSaveContainerHandle container, const char **blobNames, UINT32 *countOfBlobs, SIZE_T blobsSize, XGameSaveBlob *blobData), (container, blobNames, countOfBlobs, blobsSize, blobData) )
IMPL_CALL( HRESULT, XGameSaveReadBlobDataAsync, (XGameSaveContainerHandle container, const char **blobNames, UINT32 countOfBlobs, XAsyncBlock *async), (container, blobNames, countOfBlobs, async) )
IMPL_CALL( HRESULT, XGameSaveReadBlobDataResult, (XAsyncBlock *async, SIZE_T blobsSize, XGameSaveBlob *blobData, UINT32 *countOfBlobs), (async, blobsSize, blobData, countOfBlobs) )
IMPL_CALL( HRESULT, XGameSaveCreateUpdate, (XGameSaveContainerHandle container, const char *containerDisplayName, XGameSaveUpdateHandle *updateContext), (container, containerDisplayName, updateContext) )
IMPL_CALL( void, XGameSaveCloseUpdate, (XGameSaveUpdateHandle context), (context) )
IMPL_CALL( HRESULT, XGameSaveSubmitBlobWrite, (XGameSaveUpdateHandle updateContext, const char *blobName, UINT8 *data, SIZE_T byteCount), (updateContext, blobName, data, byteCount) )
IMPL_CALL( HRESULT, XGameSaveSubmitBlobDelete, (XGameSaveUpdateHandle updateContext, const char *blobName), (updateContext, blobName) )
IMPL_CALL( HRESULT, XGameSaveSubmitUpdate, (XGameSaveUpdateHandle updateContext), (updateContext) )
IMPL_CALL( HRESULT, XGameSaveSubmitUpdateAsync, (XGameSaveUpdateHandle updateContext, XAsyncBlock *async), (updateContext, async) )
IMPL_CALL( HRESULT, XGameSaveSubmitUpdateResult, (XAsyncBlock *async), (async) )

#undef CONSUMED_INTERFACE

// --- IXGameSaveImpl2 --- //
#define CONSUMED_INTERFACE IXGameSaveImpl2*

IMPL_CALL( HRESULT, XGameSaveFilesGetFolderWithUiAsync, (XUserHandle requestingUser, const char *configurationId, XAsyncBlock *async), (requestingUser, configurationId, async) )
IMPL_CALL( HRESULT, XGameSaveFilesGetFolderWithUiResult, (XAsyncBlock *async, SIZE_T folderSize, char *folderResult), (async, folderSize, folderResult) )
IMPL_CALL( HRESULT, XGameSaveFilesGetRemainingQuota, (XUserHandle userContext, const char *configurationId, INT64 *remainingQuota), (userContext, configurationId, remainingQuota) )

#undef CONSUMED_INTERFACE

// --- IXGameSaveImpl3 --- //
#define CONSUMED_INTERFACE IXGameSaveImpl3*

#undef CONSUMED_INTERFACE

#undef CONSUMED_CLSID



// --- XGameStreamingImpl --- //
#define CONSUMED_CLSID CLSID_XGameStreamingImpl

// --- IXGameStreamingImpl --- //
#define CONSUMED_INTERFACE IXGameStreamingImpl*

IMPL_CALL( HRESULT, XGameStreamingInitialize, (), () )
IMPL_CALL( void, XGameStreamingUninitialize, (), () )
IMPL_CALL( BOOLEAN, XGameStreamingIsStreaming, (), () )
IMPL_CALL( HRESULT, XGameStreamingRegisterClientPropertiesChanged, (XGameStreamingClientId client, XTaskQueueHandle queue, void *context, XGameStreamingClientPropertiesChangedCallback *callback, XTaskQueueRegistrationToken *token), (client, queue, context, callback, token) )
IMPL_CALL( BOOLEAN, XGameStreamingUnregisterClientPropertiesChanged, (XGameStreamingClientId client, XTaskQueueRegistrationToken token, BOOLEAN wait), (client, token, wait) )
IMPL_CALL( HRESULT, XGameStreamingGetStreamPhysicalDimensions, (XGameStreamingClientId client, UINT32 *horizontalMm, UINT32 *verticalMm), (client, horizontalMm, verticalMm) )
IMPL_CALL( UINT32, XGameStreamingGetClientCount, (), () )
IMPL_CALL( HRESULT, XGameStreamingGetClients, (UINT32 clientCount, XGameStreamingClientId clients[], UINT32 *clientsUsed), (clientCount, clients, clientsUsed) )
IMPL_CALL( XGameStreamingConnectionState, XGameStreamingGetConnectionState, (XGameStreamingClientId client), (client) )
IMPL_CALL( HRESULT, XGameStreamingRegisterConnectionStateChanged, (XTaskQueueHandle queue, void *context, XGameStreamingConnectionStateChangedCallback *callback, XTaskQueueRegistrationToken *token), (queue, context, callback, token) )
IMPL_CALL( BOOLEAN, XGameStreamingUnregisterConnectionStateChanged, (XTaskQueueRegistrationToken token, BOOLEAN wait), (token, wait) )
IMPL_CALL( HRESULT, XGameStreamingGetStreamAddedLatency, (XGameStreamingClientId client, UINT32 *averageInputLatencyUs, UINT32 *averageOutputLatencyUs, UINT32 *standardDeviationUs), (client, averageInputLatencyUs, averageOutputLatencyUs, standardDeviationUs) )
IMPL_CALL( SIZE_T, XGameStreamingGetServerLocationNameSize, (), () )
IMPL_CALL( HRESULT, XGameStreamingGetServerLocationName, (SIZE_T serverLocationNameSize, char *serverLocationName), (serverLocationNameSize, serverLocationName) )
IMPL_CALL( void, XGameStreamingHideTouchControls, (), () )
IMPL_CALL( void, XGameStreamingShowTouchControlLayout, (const char *layout), (layout) )
IMPL_CALL( void, XGameStreamingHideTouchControlsOnClient, (XGameStreamingClientId client), (client) )
IMPL_CALL( void, XGameStreamingShowTouchControlLayoutOnClient, (XGameStreamingClientId client, const char *layout), (client, layout) )
IMPL_CALL( HRESULT, XGameStreamingIsTouchInputEnabled, (XGameStreamingClientId client, BOOLEAN *touchInputEnabled), (client, touchInputEnabled) )
IMPL_CALL( HRESULT, XGameStreamingGetLastFrameDisplayed, (XGameStreamingClientId client, D3D12XBOX_FRAME_PIPELINE_TOKEN *framePipelineToken), (client, framePipelineToken) )
IMPL_CALL( HRESULT, XGameStreamingGetAssociatedFrame, (IGameInputReading *gamepadReading, D3D12XBOX_FRAME_PIPELINE_TOKEN *framePipelineToken), (gamepadReading, framePipelineToken) )
IMPL_CALL( HRESULT, XGameStreamingGetGamepadPhysicality, (IGameInputReading *gamepadReading, XGameStreamingGamepadPhysicality *gamepadPhysicality), (gamepadReading, gamepadPhysicality) )
IMPL_CALL( HRESULT, XGameStreamingUpdateTouchControlsState, (SIZE_T operationCount, const XGameStreamingTouchControlsStateOperation operations[]), (operationCount, operations) )
IMPL_CALL( HRESULT, XGameStreamingUpdateTouchControlsStateOnClient, (XGameStreamingClientId client, SIZE_T operationCount, const XGameStreamingTouchControlsStateOperation operations[]), (client, operationCount, operations) )
IMPL_CALL( HRESULT, XGameStreamingShowTouchControlsWithStateUpdate, (const char *layout, SIZE_T operationCount, const XGameStreamingTouchControlsStateOperation operations[]), (layout, operationCount, operations) )
IMPL_CALL( HRESULT, XGameStreamingShowTouchControlsWithStateUpdateOnClient, (XGameStreamingClientId client, const char *layout, SIZE_T operationCount, const XGameStreamingTouchControlsStateOperation operations[]), (client, layout, operationCount, operations) )
IMPL_CALL( SIZE_T, XGameStreamingGetTouchBundleVersionNameSize, (XGameStreamingClientId client), (client) )
IMPL_CALL( HRESULT, XGameStreamingGetTouchBundleVersion, (XGameStreamingClientId client, XVersion *version, SIZE_T versionNameSize, char *versionName), (client, version, versionNameSize, versionName) )
IMPL_CALL( HRESULT, XGameStreamingGetClientIPAddress, (XGameStreamingClientId client, SIZE_T ipAddressSize, char *ipAddress), (client, ipAddressSize, ipAddress) )

#undef CONSUMED_INTERFACE

// --- IXGameStreamingImpl2 --- //
#define CONSUMED_INTERFACE IXGameStreamingImpl2*

IMPL_CALL( HRESULT, XGameStreamingGetSessionId, (XGameStreamingClientId client, SIZE_T sessionIdSize, char *sessionId, SIZE_T *sessionIdUsed), (client, sessionIdSize, sessionId, sessionIdUsed) )

#undef CONSUMED_INTERFACE

// --- IXGameStreamingImpl3 --- //
#define CONSUMED_INTERFACE IXGameStreamingImpl3*

IMPL_CALL( HRESULT, XGameStreamingGetDisplayDetails, (XGameStreamingClientId client, UINT32 maxSupportedPixels, float widestSupportedAspectRatio, float tallestSupportedAspectRatio, XGameStreamingDisplayDetails *displayDetails), (client, maxSupportedPixels, widestSupportedAspectRatio, tallestSupportedAspectRatio, displayDetails) )
IMPL_CALL( HRESULT, XGameStreamingSetResolution, (UINT32 width, UINT32 height), (width, height) )

#undef CONSUMED_INTERFACE

#undef CONSUMED_CLSID



// --- XGameUiImpl --- //
#define CONSUMED_CLSID CLSID_XGameUiImpl

// --- IXGameUiImpl --- //
#define CONSUMED_INTERFACE IXGameUiImpl*

IMPL_CALL( HRESULT, XGameUiShowMessageDialogAsync, (XAsyncBlock *async, const char *titleText, const char *contentText, const char *firstButtonText, const char *secondButtonText, const char *thirdButtonText, XGameUiMessageDialogButton defaultButton, XGameUiMessageDialogButton cancelButton), (async, titleText, contentText, firstButtonText, secondButtonText, thirdButtonText, defaultButton, cancelButton) )
IMPL_CALL( HRESULT, XGameUiShowMessageDialogResult, (XAsyncBlock *async, XGameUiMessageDialogButton *resultButton), (async, resultButton) )
IMPL_CALL( HRESULT, XGameUiShowSendGameInviteAsync, (XAsyncBlock *async, XUserHandle requestingUser, const char *sessionConfigurationId, const char *sessionTemplateName, const char *sessionId, const char *invitationText, const char *customActivationContext), (async, requestingUser, sessionConfigurationId, sessionTemplateName, sessionId, invitationText, customActivationContext) )
IMPL_CALL( HRESULT, XGameUiShowSendGameInviteResult, (XAsyncBlock *async), (async) )
IMPL_CALL( HRESULT, XGameUiShowPlayerProfileCardAsync, (XAsyncBlock *async, XUserHandle requestingUser, UINT64 targetPlayer), (async, requestingUser, targetPlayer) )
IMPL_CALL( HRESULT, XGameUiShowPlayerProfileCardResult, (XAsyncBlock *async), (async) )
IMPL_CALL( HRESULT, XGameUiShowAchievementsAsync, (XAsyncBlock *async, XUserHandle requestingUser, UINT32 titleId), (async, requestingUser, titleId) )
IMPL_CALL( HRESULT, XGameUiShowAchievementsResult, (XAsyncBlock *async), (async) )
IMPL_CALL( HRESULT, XGameUiShowPlayerPickerAsync, (XAsyncBlock *async, XUserHandle requestingUser, const char *promptText, UINT32 selectFromPlayersCount, const UINT64 selectFromPlayers[], UINT32 preSelectedPlayersCount, UINT64 preSelectedPlayers[], UINT32 minSelectionCount, UINT32 maxSelectionCount), (async, requestingUser, promptText, selectFromPlayersCount, selectFromPlayers, preSelectedPlayersCount, preSelectedPlayers, minSelectionCount, maxSelectionCount) )
IMPL_CALL( HRESULT, XGameUiShowPlayerPickerResultCount, (XAsyncBlock *async, UINT32 *resultPlayersCount), (async, resultPlayersCount) )
IMPL_CALL( HRESULT, XGameUiShowPlayerPickerResult, (XAsyncBlock *async, UINT32 resultPlayersCount, UINT64 resultPlayers[], UINT32 *resultPlayersUsed), (async, resultPlayersCount, resultPlayers, resultPlayersUsed) )
IMPL_CALL( HRESULT, XGameUiShowErrorDialogAsync, (XAsyncBlock *async, HRESULT errorCode, const char *context), (async, errorCode, context) )
IMPL_CALL( HRESULT, XGameUiShowErrorDialogResult, (XAsyncBlock *async), (async) )
IMPL_CALL( HRESULT, XGameUiSetNotificationPositionHint, (XGameUiNotificationPositionHint position), (position) )
IMPL_CALL( HRESULT, XGameUiShowTextEntryAsync, (XAsyncBlock *async, const char *titleText, const char *descriptionText, const char *defaultText, XGameUiTextEntryInputScope inputScope, UINT32 maxTextLength), (async, titleText, descriptionText, defaultText, inputScope, maxTextLength) )
IMPL_CALL( HRESULT, XGameUiShowTextEntryResultSize, (XAsyncBlock *async, UINT32 *resultTextBufferSize), (async, resultTextBufferSize) )
IMPL_CALL( HRESULT, XGameUiShowTextEntryResult, (XAsyncBlock *async, UINT32 resultTextBufferSize, char *resultTextBuffer, UINT32 *resultTextBufferUsed), (async, resultTextBufferSize, resultTextBuffer, resultTextBufferUsed) )
IMPL_CALL( HRESULT, XGameUiShowWebAuthenticationAsync, (XAsyncBlock *async, XUserHandle requestingUser, const char *requestUri, const char *completionUri), (async, requestingUser, requestUri, completionUri) )
IMPL_CALL( HRESULT, XGameUiShowWebAuthenticationResultSize, (XAsyncBlock *async, SIZE_T *bufferSize), (async, bufferSize) )
IMPL_CALL( HRESULT, XGameUiShowWebAuthenticationResult, (XAsyncBlock *async, SIZE_T bufferSize, void *buffer, XGameUiWebAuthenticationResultData **ptrToBuffer, SIZE_T *bufferUsed), (async, bufferSize, buffer, ptrToBuffer, bufferUsed) )
IMPL_CALL( HRESULT, XGameUiShowWebAuthenticationWithOptionsAsync, (XAsyncBlock *async, XUserHandle requestingUser, const char *requestUri, const char *completionUri, XGameUiWebAuthenticationOptions options), (async, requestingUser, requestUri, completionUri, options) )

#undef CONSUMED_INTERFACE

// --- IXGameUiImpl2 --- //
#define CONSUMED_INTERFACE IXGameUiImpl2*

IMPL_CALL( HRESULT, XGameUiShowMultiplayerActivityGameInviteAsync, (XAsyncBlock *async, XUserHandle requestingUser), (async, requestingUser) )
IMPL_CALL( HRESULT, XGameUiShowMultiplayerActivityGameInviteResult, (XAsyncBlock *async), (async) )
IMPL_CALL( HRESULT, XGameUiTextEntryOpen, (const XGameUiTextEntryOptions *options, UINT32 maxLength, const char *initialText, UINT32 initialCursorIndex, XGameUiTextEntryHandle *handle), (options, maxLength, initialText, initialCursorIndex, handle) )
IMPL_CALL( HRESULT, XGameUiTextEntryClose, (XGameUiTextEntryHandle handle), (handle) )
IMPL_CALL( HRESULT, XGameUiTextEntryGetState, (XGameUiTextEntryHandle handle, XGameUiTextEntryChangeTypeFlags *changeType, UINT32 *cursorIndex, UINT32 *imeClauseStartIndex, UINT32 *imeClauseEndIndex, UINT32 bufferSize, char *buffer), (handle, changeType, cursorIndex, imeClauseStartIndex, imeClauseEndIndex, bufferSize, buffer) )
IMPL_CALL( HRESULT, XGameUiTextEntryGetExtents, (XGameUiTextEntryHandle handle, XGameUiTextEntryExtents *extents), (handle, extents) )
IMPL_CALL( HRESULT, XGameUiTextEntryUpdatePositionHint, (XGameUiTextEntryHandle handle, XGameUiTextEntryPositionHint positionHint), (handle, positionHint) )
IMPL_CALL( HRESULT, XGameUiTextEntryUpdateVisibility, (XGameUiTextEntryHandle handle, XGameUiTextEntryVisibilityFlags visibilityFlags), (handle, visibilityFlags) )

#undef CONSUMED_INTERFACE

// --- IXGameUiImpl3 --- //
#define CONSUMED_INTERFACE IXGameUiImpl3*

IMPL_CALL( HRESULT, XGameUiShowStateShareAsync, (XAsyncBlock *async, XUserHandle requestingUser, const char *linkToken), (async, requestingUser, linkToken) )
IMPL_CALL( HRESULT, XGameUiShowStateShareResult, (XAsyncBlock *async), (async) )

#undef CONSUMED_INTERFACE

// --- IXGameUiImpl4 --- //
#define CONSUMED_INTERFACE IXGameUiImpl4*

IMPL_CALL( HRESULT, XGameUiSetUiCallbacks, (const XGameUiUiCallbacks *callbacks, BOOLEAN useSystemUiIfAvailable), (callbacks, useSystemUiIfAvailable) )
IMPL_CALL( HRESULT, XGameUiSetMessageDialogUiResponse, (XGameUiCallbackHandle callbackHandle, XGameUiMessageDialogButton response), (callbackHandle, response) )
IMPL_CALL( HRESULT, XGameUiSetPlayerPickerUiResponse, (XGameUiCallbackHandle callbackHandle, UINT32 playerCount, const UINT64 players[]), (callbackHandle, playerCount, players) )
IMPL_CALL( HRESULT, XGameUiSetTextEntryUiResponse, (XGameUiCallbackHandle callbackHandle, const char *response), (callbackHandle, response) )
IMPL_CALL( HRESULT, XGameUiSetPlayerProfileCardUiResponse, (XGameUiCallbackHandle callbackHandle), (callbackHandle) )
IMPL_CALL( HRESULT, XGameUiSetSendGameInviteUiResponse, (XGameUiCallbackHandle callbackHandle), (callbackHandle) )
IMPL_CALL( HRESULT, XGameUiSetAchievementsUiResponse, (XGameUiCallbackHandle callbackHandle), (callbackHandle) )
IMPL_CALL( HRESULT, XGameUiSetMultiplayerActivityGameInviteUiResponse, (XGameUiCallbackHandle callbackHandle), (callbackHandle) )
IMPL_CALL( HRESULT, XGameUiSetErrorDialogUiResponse, (XGameUiCallbackHandle callbackHandle), (callbackHandle) )

#undef CONSUMED_INTERFACE

#undef CONSUMED_CLSID



// --- XNetworkingImpl --- //
#define CONSUMED_CLSID CLSID_XNetworkingImpl

// --- IXNetworkingImpl --- //
#define CONSUMED_INTERFACE IXNetworkingImpl*

IMPL_CALL( HRESULT, XNetworkingQueryPreferredLocalUdpMultiplayerPort, (UINT16 *preferredLocalUdpMultiplayerPort), (preferredLocalUdpMultiplayerPort) )
IMPL_CALL( HRESULT, XNetworkingQueryPreferredLocalUdpMultiplayerPortAsync, (XAsyncBlock *asyncBlock), (asyncBlock) )
IMPL_CALL( HRESULT, XNetworkingQueryPreferredLocalUdpMultiplayerPortAsyncResult, (XAsyncBlock *asyncBlock, UINT16 *preferredLocalUdpMultiplayerPort), (asyncBlock, preferredLocalUdpMultiplayerPort) )
IMPL_CALL( HRESULT, XNetworkingRegisterPreferredLocalUdpMultiplayerPortChanged, (XTaskQueueHandle queue, void *context, XNetworkingPreferredLocalUdpMultiplayerPortChangedCallback *callback, XTaskQueueRegistrationToken *token), (queue, context, callback, token) )
IMPL_CALL( BOOLEAN, XNetworkingUnregisterPreferredLocalUdpMultiplayerPortChanged, (XTaskQueueRegistrationToken token, BOOLEAN wait), (token, wait) )
IMPL_CALL( HRESULT, XNetworkingQuerySecurityInformationForUrlAsync, (const char *url, XAsyncBlock *asyncBlock), (url, asyncBlock) )
IMPL_CALL( HRESULT, XNetworkingQuerySecurityInformationForUrlAsyncResultSize, (XAsyncBlock *asyncBlock, SIZE_T *securityInformationBufferByteCount), (asyncBlock, securityInformationBufferByteCount) )
IMPL_CALL( HRESULT, XNetworkingQuerySecurityInformationForUrlAsyncResult, (XAsyncBlock *asyncBlock, SIZE_T securityInformationBufferByteCount, SIZE_T *securityInformationBufferByteCountUsed, UINT8 *securityInformationBuffer, XNetworkingSecurityInformation **securityInformation), (asyncBlock, securityInformationBufferByteCount, securityInformationBufferByteCountUsed, securityInformationBuffer, securityInformation) )
IMPL_CALL( HRESULT, XNetworkingQuerySecurityInformationForUrlUtf16Async, (const WCHAR *url, XAsyncBlock *asyncBlock), (url, asyncBlock) )
IMPL_CALL( HRESULT, XNetworkingQuerySecurityInformationForUrlUtf16AsyncResultSize, (XAsyncBlock *asyncBlock, SIZE_T *securityInformationBufferByteCount), (asyncBlock, securityInformationBufferByteCount) )
IMPL_CALL( HRESULT, XNetworkingQuerySecurityInformationForUrlUtf16AsyncResult, (XAsyncBlock *asyncBlock, SIZE_T securityInformationBufferByteCount, SIZE_T *securityInformationBufferByteCountUsed, UINT8 *securityInformationBuffer, XNetworkingSecurityInformation **securityInformation), (asyncBlock, securityInformationBufferByteCount, securityInformationBufferByteCountUsed, securityInformationBuffer, securityInformation) )
IMPL_CALL( HRESULT, XNetworkingVerifyServerCertificate, (void *requestHandle, const XNetworkingSecurityInformation *securityInformation), (requestHandle, securityInformation) )
IMPL_CALL( HRESULT, XNetworkingGetConnectivityHint, (XNetworkingConnectivityHint *connectivityHint), (connectivityHint) )
IMPL_CALL( HRESULT, XNetworkingRegisterConnectivityHintChanged, (XTaskQueueHandle queue, void *context, XNetworkingConnectivityHintChangedCallback *callback, XTaskQueueRegistrationToken *token), (queue, context, callback, token) )
IMPL_CALL( BOOLEAN, XNetworkingUnregisterConnectivityHintChanged, (XTaskQueueRegistrationToken token, BOOLEAN wait), (token, wait) )

#undef CONSUMED_INTERFACE

// --- IXNetworkingImpl2 --- //
#define CONSUMED_INTERFACE IXNetworkingImpl2*

IMPL_CALL( HRESULT, XNetworkingQueryConfigurationSetting, (XNetworkingConfigurationSetting configurationSetting, UINT64 *value), (configurationSetting, value) )
IMPL_CALL( HRESULT, XNetworkingSetConfigurationSetting, (XNetworkingConfigurationSetting configurationParameter, UINT64 value), (configurationParameter, value) )
IMPL_CALL( HRESULT, XNetworkingQueryStatistics, (XNetworkingStatisticsType statisticsType, XNetworkingStatisticsBuffer *statisticsBuffer), (statisticsType, statisticsBuffer) )

#undef CONSUMED_INTERFACE

#undef CONSUMED_CLSID



// --- XPackageImpl --- //
#define CONSUMED_CLSID CLSID_XPackageImpl

// --- IXPackageImpl --- //
#define CONSUMED_INTERFACE IXPackageImpl*

IMPL_CALL( HRESULT, XPackageGetCurrentProcessPackageIdentifier, (SIZE_T bufferSize, char *buffer), (bufferSize, buffer) )
IMPL_CALL( BOOLEAN, XPackageIsPackagedProcess, (), () )
IMPL_CALL( HRESULT, XPackageCreateInstallationMonitor, (const char *packageIdentifier, UINT32 selectorCount, XPackageChunkSelector selectors[], UINT32 minimumUpdateIntervalMs, XTaskQueueHandle queue, XPackageInstallationMonitorHandle *installationMonitor), (packageIdentifier, selectorCount, selectors, minimumUpdateIntervalMs, queue, installationMonitor) )
IMPL_CALL( void, XPackageCloseInstallationMonitorHandle, (XPackageInstallationMonitorHandle installationMonitor), (installationMonitor) )
IMPL_CALL( void, XPackageGetInstallationProgress, (XPackageInstallationMonitorHandle installationMonitor, XPackageInstallationProgress *progress), (installationMonitor, progress) )
IMPL_CALL( BOOLEAN, XPackageUpdateInstallationMonitor, (XPackageInstallationMonitorHandle installationMonitor), (installationMonitor) )
IMPL_CALL( HRESULT, XPackageRegisterInstallationProgressChanged, (XPackageInstallationMonitorHandle installationMonitor, void *context, XPackageInstallationProgressCallback *callback, XTaskQueueRegistrationToken *token), (installationMonitor, context, callback, token) )
IMPL_CALL( BOOLEAN, XPackageUnregisterInstallationProgressChanged, (XPackageInstallationMonitorHandle installationMonitor, XTaskQueueRegistrationToken token, BOOLEAN wait), (installationMonitor, token, wait) )
IMPL_CALL( HRESULT, XPackageGetUserLocale, (SIZE_T localeSize, char *locale), (localeSize, locale) )
IMPL_CALL( HRESULT, XPackageFindChunkAvailability, (const char *packageIdentifier, UINT32 selectorCount, XPackageChunkSelector selectors[], XPackageChunkAvailability *availability), (packageIdentifier, selectorCount, selectors, availability) )
IMPL_CALL( HRESULT, XPackageEnumerateChunkAvailability, (const char *packageIdentifier, XPackageChunkSelectorType type, void *context, XPackageChunkAvailabilityCallback *callback), (packageIdentifier, type, context, callback) )
IMPL_CALL( HRESULT, XPackageChangeChunkInstallOrder, (const char *packageIdentifier, UINT32 selectorCount, XPackageChunkSelector selectors[]), (packageIdentifier, selectorCount, selectors) )
IMPL_CALL( HRESULT, XPackageInstallChunks, (const char *packageIdentifier, UINT32 selectorCount, XPackageChunkSelector *selectors, UINT32 minimumUpdateIntervalMs, BOOLEAN suppressUserConfirmation, XTaskQueueHandle queue, XPackageInstallationMonitorHandle *installationMonitor), (packageIdentifier, selectorCount, selectors, minimumUpdateIntervalMs, suppressUserConfirmation, queue, installationMonitor) )
IMPL_CALL( HRESULT, XPackageInstallChunksAsync, (const char *packageIdentifier, UINT32 selectorCount, XPackageChunkSelector selectors[], UINT32 minimumUpdateIntervalMs, BOOLEAN suppressUserConfirmation, XAsyncBlock *asyncBlock), (packageIdentifier, selectorCount, selectors, minimumUpdateIntervalMs, suppressUserConfirmation, asyncBlock) )
IMPL_CALL( HRESULT, XPackageInstallChunksResult, (XAsyncBlock *asyncBlock, XPackageInstallationMonitorHandle *installationMonitor), (asyncBlock, installationMonitor) )
IMPL_CALL( HRESULT, XPackageEstimateDownloadSize, (const char *packageIdentifier, UINT32 selectorCount, XPackageChunkSelector selectors[], UINT64 *downloadSize, BOOLEAN *shouldPresentUserConfirmation), (packageIdentifier, selectorCount, selectors, downloadSize, shouldPresentUserConfirmation) )
IMPL_CALL( HRESULT, XPackageUninstallChunks, (const char *packageIdentifier, UINT32 selectorCount, XPackageChunkSelector selectors[]), (packageIdentifier, selectorCount, selectors) )
IMPL_CALL( BOOLEAN, XPackageUnregisterPackageInstalled, (XTaskQueueRegistrationToken token, BOOLEAN wait), (token, wait) )
IMPL_CALL( HRESULT, XPackageGetMountPathSize, (XPackageMountHandle mount, SIZE_T *pathSize), (mount, pathSize) )
IMPL_CALL( HRESULT, XPackageGetMountPath, (XPackageMountHandle mount, SIZE_T pathSize, char *path), (mount, pathSize, path) )
IMPL_CALL( void, XPackageCloseMountHandle, (XPackageMountHandle mount), (mount) )
IMPL_CALL( HRESULT, XPackageEnumeratePackages, (XPackageKind kind, XPackageEnumerationScope scope, void *context, XPackageEnumerationCallback *callback), (kind, scope, context, callback) )
IMPL_CALL( HRESULT, XPackageRegisterPackageInstalled, (XTaskQueueHandle queue, void *context, XPackageInstalledCallback *callback, XTaskQueueRegistrationToken *token), (queue, context, callback, token) )
IMPL_CALL( HRESULT, XPackageGetWriteStats, (XPackageWriteStats *writeStats), (writeStats) )
IMPL_CALL( HRESULT, XPackageUninstallUWPInstance, (const char *packageName), (packageName) )
IMPL_CALL( HRESULT, XPackageEnumerateFeatures, (const char *packageIdentifier, void *context, XPackageFeatureEnumerationCallback *callback), (packageIdentifier, context, callback) )
IMPL_CALL( BOOLEAN, XPackageUninstallPackage, (const char *packageIdentifier), (packageIdentifier) )

#undef CONSUMED_INTERFACE

// --- IXPackageImpl2 --- //
#define CONSUMED_INTERFACE IXPackageImpl2*

IMPL_CALL( HRESULT, XPackageMountWithUiAsync, (const char *packageIdentifier, XAsyncBlock *async), (packageIdentifier, async) )
IMPL_CALL( HRESULT, XPackageMountWithUiResult, (XAsyncBlock *async, XPackageMountHandle *mount), (async, mount) )

#undef CONSUMED_INTERFACE

// --- IXPackageImpl3 --- //
#define CONSUMED_INTERFACE IXPackageImpl3*

#undef CONSUMED_INTERFACE

// --- IXPackageImpl4 --- //
#define CONSUMED_INTERFACE IXPackageImpl4*

IMPL_CALL( HRESULT, XPackageGetKind, (const char *packageIdentifier, XPackageKind *kind), (packageIdentifier, kind) )

#undef CONSUMED_INTERFACE

#undef CONSUMED_CLSID



// --- XPersistentLocalStorageImpl --- //
#define CONSUMED_CLSID CLSID_XPersistentLocalStorageImpl

// --- IXPersistentLocalStorageImpl --- //
#define CONSUMED_INTERFACE IXPersistentLocalStorageImpl*

IMPL_CALL( HRESULT, XPersistentLocalStorageGetPathSize, (SIZE_T *pathSize), (pathSize) )
IMPL_CALL( HRESULT, XPersistentLocalStorageGetPath, (SIZE_T pathSize, char *path, SIZE_T *pathUsed), (pathSize, path, pathUsed) )
IMPL_CALL( HRESULT, XPersistentLocalStorageGetSpaceInfo, (XPersistentLocalStorageSpaceInfo *info), (info) )
IMPL_CALL( HRESULT, XPersistentLocalStoragePromptUserForSpaceAsync, (UINT64 requestedBytes, XAsyncBlock *asyncBlock), (requestedBytes, asyncBlock) )
IMPL_CALL( HRESULT, XPersistentLocalStoragePromptUserForSpaceResult, (XAsyncBlock *asyncBlock), (asyncBlock) )

#undef CONSUMED_INTERFACE

// --- IXPersistentLocalStorageImpl2 --- //
#define CONSUMED_INTERFACE IXPersistentLocalStorageImpl2*

#undef CONSUMED_INTERFACE

// --- IXPersistentLocalStorageImpl3 --- //
#define CONSUMED_INTERFACE IXPersistentLocalStorageImpl3*

IMPL_CALL( HRESULT, XPersistentLocalStorageMountForPackage, (const char *packageIdentifier, XPackageMountHandle *mountHandle), (packageIdentifier, mountHandle) )

#undef CONSUMED_INTERFACE

#undef CONSUMED_CLSID



// --- XStoreImpl --- //
#define CONSUMED_CLSID CLSID_XStoreImpl

// --- IXStoreImpl --- //
#define CONSUMED_INTERFACE IXStoreImpl*

IMPL_CALL( HRESULT, XStoreCreateContext, (const XUserHandle user, XStoreContextHandle *storeContextHandle), (user, storeContextHandle) )
IMPL_CALL( void, XStoreCloseContextHandle, (XStoreContextHandle storeContextHandle), (storeContextHandle) )
IMPL_CALL( HRESULT, XStoreQueryAssociatedProductsAsync, (const XStoreContextHandle storeContextHandle, XStoreProductKind productKinds, UINT32 maxItemsToRetrievePerPage, XAsyncBlock *async), (storeContextHandle, productKinds, maxItemsToRetrievePerPage, async) )
IMPL_CALL( HRESULT, XStoreQueryAssociatedProductsResult, (XAsyncBlock *async, XStoreProductQueryHandle *productQueryHandle), (async, productQueryHandle) )
IMPL_CALL( HRESULT, XStoreQueryProductsAsync, (const XStoreContextHandle storeContextHandle, XStoreProductKind productKinds, const char **storeIds, SIZE_T storeIdsCount, const char **actionFilters, SIZE_T actionFiltersCount, XAsyncBlock *async), (storeContextHandle, productKinds, storeIds, storeIdsCount, actionFilters, actionFiltersCount, async) )
IMPL_CALL( HRESULT, XStoreQueryProductsResult, (XAsyncBlock *async, XStoreProductQueryHandle *productQueryHandle), (async, productQueryHandle) )
IMPL_CALL( HRESULT, XStoreQueryEntitledProductsAsync, (const XStoreContextHandle storeContextHandle, XStoreProductKind productKinds, UINT32 maxItemsToRetrievePerPage, XAsyncBlock *async), (storeContextHandle, productKinds, maxItemsToRetrievePerPage, async) )
IMPL_CALL( HRESULT, XStoreQueryEntitledProductsResult, (XAsyncBlock *async, XStoreProductQueryHandle *productQueryHandle), (async, productQueryHandle) )
IMPL_CALL( HRESULT, XStoreQueryProductForCurrentGameAsync, (const XStoreContextHandle storeContextHandle, XAsyncBlock *async), (storeContextHandle, async) )
IMPL_CALL( HRESULT, XStoreQueryProductForCurrentGameResult, (XAsyncBlock *async, XStoreProductQueryHandle *productQueryHandle), (async, productQueryHandle) )
IMPL_CALL( HRESULT, XStoreQueryProductForPackageAsync, (const XStoreContextHandle storeContextHandle, XStoreProductKind productKinds, const char *packageIdentifier, XAsyncBlock *async), (storeContextHandle, productKinds, packageIdentifier, async) )
IMPL_CALL( HRESULT, XStoreQueryProductForPackageResult, (XAsyncBlock *async, XStoreProductQueryHandle *productQueryHandle), (async, productQueryHandle) )
IMPL_CALL( HRESULT, XStoreEnumerateProductsQuery, (const XStoreProductQueryHandle productQueryHandle, void *context, XStoreProductQueryCallback *callback), (productQueryHandle, context, callback) )
IMPL_CALL( BOOLEAN, XStoreProductsQueryHasMorePages, (const XStoreProductQueryHandle productQueryHandle), (productQueryHandle) )
IMPL_CALL( HRESULT, XStoreProductsQueryNextPageAsync, (const XStoreProductQueryHandle productQueryHandle, XAsyncBlock *async), (productQueryHandle, async) )
IMPL_CALL( HRESULT, XStoreProductsQueryNextPageResult, (XAsyncBlock *async, XStoreProductQueryHandle *productQueryHandle), (async, productQueryHandle) )
IMPL_CALL( void, XStoreCloseProductsQueryHandle, (XStoreProductQueryHandle productQueryHandle), (productQueryHandle) )
IMPL_CALL( HRESULT, XStoreAcquireLicenseForPackageAsync, (const XStoreProductQueryHandle productQueryHandle, const char *packageIdentifier, XAsyncBlock *async), (productQueryHandle, packageIdentifier, async) )
IMPL_CALL( HRESULT, XStoreAcquireLicenseForPackageResult, (XAsyncBlock *async, XStoreLicenseHandle *storeLicenseHandle), (async, storeLicenseHandle) )
IMPL_CALL( BOOLEAN, XStoreIsLicenseValid, (const XStoreLicenseHandle storeLicenseHandle), (storeLicenseHandle) )
IMPL_CALL( void, XStoreCloseLicenseHandle, (XStoreLicenseHandle storeLicenseHandle), (storeLicenseHandle) )
IMPL_CALL( HRESULT, XStoreCanAcquireLicenseForStoreIdAsync, (const XStoreContextHandle storeContextHandle, const char *storeProductId, XAsyncBlock *async), (storeContextHandle, storeProductId, async) )
IMPL_CALL( HRESULT, XStoreCanAcquireLicenseForStoreIdResult, (XAsyncBlock *async, XStoreCanAcquireLicenseResult *storeCanAcquireLicense), (async, storeCanAcquireLicense) )
IMPL_CALL( HRESULT, XStoreCanAcquireLicenseForPackageAsync, (const XStoreContextHandle storeContextHandle, const char *packageIdentifier, XAsyncBlock *async), (storeContextHandle, packageIdentifier, async) )
IMPL_CALL( HRESULT, XStoreCanAcquireLicenseForPackageResult, (XAsyncBlock *async, XStoreCanAcquireLicenseResult *storeCanAcquireLicense), (async, storeCanAcquireLicense) )
IMPL_CALL( HRESULT, XStoreQueryGameLicenseAsync, (const XStoreContextHandle storeContextHandle, XAsyncBlock *async), (storeContextHandle, async) )
IMPL_CALL( HRESULT, XStoreQueryGameLicenseResult, (XAsyncBlock *async, XStoreGameLicense *license), (async, license) )
IMPL_CALL( HRESULT, XStoreQueryAddOnLicensesAsync, (const XStoreContextHandle storeContextHandle, XAsyncBlock *async), (storeContextHandle, async) )
IMPL_CALL( HRESULT, XStoreQueryAddOnLicensesResultCount, (XAsyncBlock *async, UINT32 *count), (async, count) )
IMPL_CALL( HRESULT, XStoreQueryAddOnLicensesResult, (XAsyncBlock *async, UINT32 count, XStoreAddonLicense addOnLicenses[]), (async, count, addOnLicenses) )
IMPL_CALL( HRESULT, XStoreQueryConsumableBalanceRemainingAsync, (const XStoreContextHandle storeContextHandle, const char *storeProductId, XAsyncBlock *async), (storeContextHandle, storeProductId, async) )
IMPL_CALL( HRESULT, XStoreQueryConsumableBalanceRemainingResult, (XAsyncBlock *async, XStoreConsumableResult *consumableResult), (async, consumableResult) )
IMPL_CALL( HRESULT, XStoreReportConsumableFulfillmentAsync, (const XStoreContextHandle storeContextHandle, const char *storeProductId, UINT32 quantity, GUID trackingId, XAsyncBlock *async), (storeContextHandle, storeProductId, quantity, trackingId, async) )
IMPL_CALL( HRESULT, XStoreReportConsumableFulfillmentResult, (XAsyncBlock *async, XStoreConsumableResult *consumableResult), (async, consumableResult) )
IMPL_CALL( HRESULT, XStoreGetUserCollectionsIdAsync, (const XStoreContextHandle storeContextHandle, const char *serviceTicket, const char *publisherUserId, XAsyncBlock *async), (storeContextHandle, serviceTicket, publisherUserId, async) )
IMPL_CALL( HRESULT, XStoreGetUserCollectionsIdResultSize, (XAsyncBlock *async, SIZE_T *size), (async, size) )
IMPL_CALL( HRESULT, XStoreGetUserCollectionsIdResult, (XAsyncBlock *async, SIZE_T size, char *result), (async, size, result) )
IMPL_CALL( HRESULT, XStoreGetUserPurchaseIdAsync, (const XStoreContextHandle storeContextHandle, const char *serviceTicket, const char *publisherUserId, XAsyncBlock *async), (storeContextHandle, serviceTicket, publisherUserId, async) )
IMPL_CALL( HRESULT, XStoreGetUserPurchaseIdResultSize, (XAsyncBlock *async, SIZE_T *size), (async, size) )
IMPL_CALL( HRESULT, XStoreGetUserPurchaseIdResult, (XAsyncBlock *async, SIZE_T size, char *result), (async, size, result) )
IMPL_CALL( HRESULT, XStoreQueryLicenseTokenAsync, (const XStoreContextHandle storeContextHandle, const char **productIds, SIZE_T productIdsCount, const char *customDeveloperString, XAsyncBlock *async), (storeContextHandle, productIds, productIdsCount, customDeveloperString, async) )
IMPL_CALL( HRESULT, XStoreQueryLicenseTokenResultSize, (XAsyncBlock *async, SIZE_T *size), (async, size) )
IMPL_CALL( HRESULT, XStoreQueryLicenseTokenResult, (XAsyncBlock *async, SIZE_T size, char *result), (async, size, result) )
IMPL_CALL( HRESULT, XStoreShowPurchaseUIAsync, (const XStoreContextHandle storeContextHandle, const char *storeId, const char *name, const char *extendedJsonData, XAsyncBlock *async), (storeContextHandle, storeId, name, extendedJsonData, async) )
IMPL_CALL( HRESULT, XStoreShowPurchaseUIResult, (XAsyncBlock *async), (async) )
IMPL_CALL( HRESULT, XStoreShowRateAndReviewUIAsync, (const XStoreContextHandle storeContextHandle, XAsyncBlock *async), (storeContextHandle, async) )
IMPL_CALL( HRESULT, XStoreShowRateAndReviewUIResult, (XAsyncBlock *async, XStoreRateAndReviewResult *result), (async, result) )
IMPL_CALL( HRESULT, XStoreShowRedeemTokenUIAsync, (const XStoreContextHandle storeContextHandle, const char *token, const char **allowedStoreIds, SIZE_T allowedStoreIdsCount, BOOLEAN disallowCsvRedemption, XAsyncBlock *async), (storeContextHandle, token, allowedStoreIds, allowedStoreIdsCount, disallowCsvRedemption, async) )
IMPL_CALL( HRESULT, XStoreShowRedeemTokenUIResult, (XAsyncBlock *async), (async) )
IMPL_CALL( HRESULT, XStoreQueryGameAndDlcPackageUpdatesAsync, (const XStoreContextHandle storeContextHandle, XAsyncBlock *async), (storeContextHandle, async) )
IMPL_CALL( HRESULT, XStoreQueryGameAndDlcPackageUpdatesResultCount, (XAsyncBlock *async, UINT32 *count), (async, count) )
IMPL_CALL( HRESULT, XStoreQueryGameAndDlcPackageUpdatesResult, (XAsyncBlock *async, UINT32 count, XStorePackageUpdate packageUpdates[]), (async, count, packageUpdates) )
IMPL_CALL( HRESULT, XStoreDownloadPackageUpdatesAsync, (const XStoreContextHandle storeContextHandle, const char **packageIdentifiers, SIZE_T packageIdentifiersCount, XAsyncBlock *async), (storeContextHandle, packageIdentifiers, packageIdentifiersCount, async) )
IMPL_CALL( HRESULT, XStoreDownloadPackageUpdatesResult, (XAsyncBlock *async), (async) )
IMPL_CALL( HRESULT, XStoreDownloadAndInstallPackageUpdatesAsync, (const XStoreContextHandle storeContextHandle, const char **packageIdentifiers, SIZE_T packageIdentifiersCount, XAsyncBlock *async), (storeContextHandle, packageIdentifiers, packageIdentifiersCount, async) )
IMPL_CALL( HRESULT, XStoreDownloadAndInstallPackageUpdatesResult, (XAsyncBlock *async), (async) )
IMPL_CALL( HRESULT, XStoreDownloadAndInstallPackagesAsync, (const XStoreContextHandle storeContextHandle, const char **storeIds, SIZE_T storeIdsCount, XAsyncBlock *async), (storeContextHandle, storeIds, storeIdsCount, async) )
IMPL_CALL( HRESULT, XStoreDownloadAndInstallPackagesResultCount, (XAsyncBlock *async, UINT32 *count), (async, count) )
IMPL_CALL( HRESULT, XStoreDownloadAndInstallPackagesResult, (XAsyncBlock *async, UINT32 count, char **packageIdentifiers), (async, count, packageIdentifiers) )
IMPL_CALL( HRESULT, XStoreQueryPackageIdentifier, (const char *storeId, SIZE_T size, char *packageIdentifier), (storeId, size, packageIdentifier) )
IMPL_CALL( HRESULT, XStoreRegisterGameLicenseChanged, (XStoreContextHandle storeContextHandle, XTaskQueueHandle queue, void *context, XStoreGameLicenseChangedCallback *callback, XTaskQueueRegistrationToken *token), (storeContextHandle, queue, context, callback, token) )
IMPL_CALL( BOOLEAN, XStoreUnregisterGameLicenseChanged, (XStoreContextHandle storeContextHandle, XTaskQueueRegistrationToken token, BOOLEAN wait), (storeContextHandle, token, wait) )
IMPL_CALL( HRESULT, XStoreRegisterPackageLicenseLost, (XStoreLicenseHandle licenseHandle, XTaskQueueHandle queue, void *context, XStorePackageLicenseLostCallback *callback, XTaskQueueRegistrationToken *token), (licenseHandle, queue, context, callback, token) )
IMPL_CALL( BOOLEAN, XStoreUnregisterPackageLicenseLost, (XStoreLicenseHandle licenseHandle, XTaskQueueRegistrationToken token, BOOLEAN wait), (licenseHandle, token, wait) )

#undef CONSUMED_INTERFACE

// --- IXStoreImpl2 --- //
#define CONSUMED_INTERFACE IXStoreImpl2*

IMPL_CALL( BOOLEAN, XStoreIsAvailabilityPurchasable, (const XStoreAvailability availability), (availability) )

#undef CONSUMED_INTERFACE

// --- IXStoreImpl3 --- //
#define CONSUMED_INTERFACE IXStoreImpl3*

IMPL_CALL( HRESULT, XStoreAcquireLicenseForDurablesAsync, (const XStoreContextHandle storeContextHandle, const char *storeId, XAsyncBlock *async), (storeContextHandle, storeId, async) )
IMPL_CALL( HRESULT, XStoreAcquireLicenseForDurablesResult, (XAsyncBlock *async, XStoreLicenseHandle *storeLicenseHandle), (async, storeLicenseHandle) )

#undef CONSUMED_INTERFACE

// --- IXStoreImpl4 --- //
#define CONSUMED_INTERFACE IXStoreImpl4*

IMPL_CALL( HRESULT, XStoreShowAssociatedProductsUIAsync, (const XStoreContextHandle storeContextHandle, const char *storeId, XStoreProductKind productKinds, XAsyncBlock *async), (storeContextHandle, storeId, productKinds, async) )
IMPL_CALL( HRESULT, XStoreShowAssociatedProductsUIResult, (XAsyncBlock *async), (async) )
IMPL_CALL( HRESULT, XStoreShowProductPageUIAsync, (const XStoreContextHandle storeContextHandle, const char *storeId, XAsyncBlock *async), (storeContextHandle, storeId, async) )
IMPL_CALL( HRESULT, XStoreShowProductPageUIResult, (XAsyncBlock *async), (async) )

#undef CONSUMED_INTERFACE

// --- IXStoreImpl5 --- //
#define CONSUMED_INTERFACE IXStoreImpl5*

IMPL_CALL( HRESULT, XStoreQueryAssociatedProductsForStoreIdAsync, (const XStoreContextHandle storeContextHandle, const char *storeId, XStoreProductKind productKinds, UINT32 maxItemsToRetrievePerPage, XAsyncBlock *async), (storeContextHandle, storeId, productKinds, maxItemsToRetrievePerPage, async) )
IMPL_CALL( HRESULT, XStoreQueryAssociatedProductsForStoreIdResult, (XAsyncBlock *async, XStoreProductQueryHandle *productQueryHandle), (async, productQueryHandle) )
IMPL_CALL( HRESULT, XStoreQueryPackageUpdatesAsync, (XStoreContextHandle storeContextHandle, const char **packageIdentifiers, SIZE_T packageIdentifiersCount, XAsyncBlock *async), (storeContextHandle, packageIdentifiers, packageIdentifiersCount, async) )
IMPL_CALL( HRESULT, XStoreQueryPackageUpdatesResultCount, (XAsyncBlock *async, UINT32 *count), (async, count) )
IMPL_CALL( HRESULT, XStoreQueryPackageUpdatesResult, (XAsyncBlock *async, UINT32 count, XStorePackageUpdate packageUpdates[]), (async, count, packageUpdates) )

#undef CONSUMED_INTERFACE

// --- IXStoreImpl6 --- //
#define CONSUMED_INTERFACE IXStoreImpl6*

IMPL_CALL( HRESULT, XStoreShowGiftingUIAsync, (XStoreContextHandle storeContextHandle, const char *storeId, const char *name, const char *extendedJsonData, XAsyncBlock *async), (storeContextHandle, storeId, name, extendedJsonData, async) )
IMPL_CALL( HRESULT, XStoreShowGiftingUIResult, (XAsyncBlock *async), (async) )

#undef CONSUMED_INTERFACE

#undef CONSUMED_CLSID



// --- XSystemImpl --- //
#define CONSUMED_CLSID CLSID_XSystemImpl

// --- IXSystemImpl --- //
#define CONSUMED_INTERFACE IXSystemImpl*

IMPL_CALL( HRESULT, XSystemGetConsoleId, (INT32 consoleIdSize, char *consoleId, SIZE_T *consoleIdUsed), (consoleIdSize, consoleId, consoleIdUsed) )
IMPL_CALL( HRESULT, XSystemGetXboxLiveSandboxId, (INT32 sandboxIdSize, char *sandboxId, SIZE_T *sandboxIdUsed), (sandboxIdSize, sandboxId, sandboxIdUsed) )
IMPL_CALL( HRESULT, XSystemGetAppSpecificDeviceId, (INT32 appSpecificDeviceIdSize, char *appSpecificDeviceId, SIZE_T *appSpecificDeviceIdUsed), (appSpecificDeviceIdSize, appSpecificDeviceId, appSpecificDeviceIdUsed) )

#undef CONSUMED_INTERFACE

// --- IXSystemImpl2 --- //
#define CONSUMED_INTERFACE IXSystemImpl2*

#undef CONSUMED_INTERFACE

// --- IXSystemImpl3 --- //
#define CONSUMED_INTERFACE IXSystemImpl3*

IMPL_CALL( HRESULT, XSystemHandleTrack, (XSystemHandleCallback callback, void *context), (callback, context) )
IMPL_CALL( BOOLEAN, XSystemIsHandleValid, (XSystemHandle handle), (handle) )

#undef CONSUMED_INTERFACE

// --- IXSystemImpl4 --- //
#define CONSUMED_INTERFACE IXSystemImpl4*

IMPL_CALL( void, XSystemAllowFullDownloadBandwidth, (BOOLEAN enable), (enable) )

#undef CONSUMED_INTERFACE

// --- IXSystemImpl5 --- //
#define CONSUMED_INTERFACE IXSystemImpl5*

#undef CONSUMED_INTERFACE

#undef CONSUMED_CLSID

// --- XSystemAnalyticsImpl --- //
#define CONSUMED_CLSID CLSID_XSystemAnalyticsImpl

// --- IXSystemAnalyticsImpl --- //
#define CONSUMED_INTERFACE IXSystemAnalyticsImpl*

IMPL_CALL( XSystemAnalyticsInfo, XSystemGetAnalyticsInfo, (), () )

#undef CONSUMED_INTERFACE

#undef CONSUMED_CLSID



// --- XUserImpl --- //
#define CONSUMED_CLSID CLSID_XUserImpl

// --- IXUserImpl --- //
#define CONSUMED_INTERFACE IXUserImpl*

IMPL_CALL( HRESULT, XUserDuplicateHandle, (XUserHandle handle, XUserHandle *duplicatedHandle), (handle, duplicatedHandle) )
IMPL_CALL( void, XUserCloseHandle, (XUserHandle user), (user) )
IMPL_CALL( INT32, XUserCompare, (XUserHandle user1, XUserHandle user2), (user1, user2) )
IMPL_CALL( HRESULT, XUserGetMaxUsers, (UINT32 *maxUsers), (maxUsers) )
IMPL_CALL( HRESULT, XUserAddAsync, (XUserAddOptions options, XAsyncBlock *async), (options, async) )
IMPL_CALL( HRESULT, XUserAddResult, (XAsyncBlock *async, XUserHandle *newUser), (async, newUser) )
IMPL_CALL( HRESULT, XUserGetLocalId, (XUserHandle user, XUserLocalId *userLocalId), (user, userLocalId) )
IMPL_CALL( HRESULT, XUserFindUserByLocalId, (XUserLocalId userLocalId, XUserHandle *handle), (userLocalId, handle) )
IMPL_CALL( HRESULT, XUserGetId, (XUserHandle user, UINT64 *userId), (user, userId) )
IMPL_CALL( HRESULT, XUserFindUserById, (UINT64 userId, XUserHandle *handle), (userId, handle) )
IMPL_CALL( HRESULT, XUserGetIsGuest, (XUserHandle user, BOOLEAN *isGuest), (user, isGuest) )
IMPL_CALL( HRESULT, XUserGetState, (XUserHandle user, XUserState *state), (user, state) )
IMPL_CALL( HRESULT, XUserGetGamerPictureAsync, (XUserHandle user, XUserGamerPictureSize pictureSize, XAsyncBlock *async), (user, pictureSize, async) )
IMPL_CALL( HRESULT, XUserGetGamerPictureResultSize, (XAsyncBlock *async, SIZE_T *bufferSize), (async, bufferSize) )
IMPL_CALL( HRESULT, XUserGetGamerPictureResult, (XAsyncBlock *async, SIZE_T bufferSize, void *buffer, SIZE_T *bufferUsed), (async, bufferSize, buffer, bufferUsed) )
IMPL_CALL( HRESULT, XUserGetAgeGroup, (XUserHandle user, XUserAgeGroup *ageGroup), (user, ageGroup) )
IMPL_CALL( HRESULT, XUserCheckPrivilege, (XUserHandle user, XUserPrivilegeOptions options, XUserPrivilege privilege, BOOLEAN *hasPrivilege, XUserPrivilegeDenyReason *reason), (user, options, privilege, hasPrivilege, reason) )
IMPL_CALL( HRESULT, XUserResolvePrivilegeWithUiAsync, (XUserHandle user, XUserPrivilegeOptions options, XUserPrivilege privilege, XAsyncBlock *async), (user, options, privilege, async) )
IMPL_CALL( HRESULT, XUserResolvePrivilegeWithUiResult, (XAsyncBlock *async), (async) )
IMPL_CALL( HRESULT, XUserGetTokenAndSignatureAsync, (XUserHandle user, XUserGetTokenAndSignatureOptions options, const char *method, const char *url, SIZE_T headerCount, const XUserGetTokenAndSignatureHttpHeader headers[], SIZE_T bodySize, const void *bodyBuffer, XAsyncBlock *async), (user, options, method, url, headerCount, headers, bodySize, bodyBuffer, async) )
IMPL_CALL( HRESULT, XUserGetTokenAndSignatureResultSize, (XAsyncBlock *async, SIZE_T *bufferSize), (async, bufferSize) )
IMPL_CALL( HRESULT, XUserGetTokenAndSignatureResult, (XAsyncBlock *async, SIZE_T bufferSize, void *buffer, XUserGetTokenAndSignatureData **ptrToBuffer, SIZE_T *bufferUsed), (async, bufferSize, buffer, ptrToBuffer, bufferUsed) )
IMPL_CALL( HRESULT, XUserGetTokenAndSignatureUtf16Async, (XUserHandle user, XUserGetTokenAndSignatureOptions options, const WCHAR *method, const WCHAR *url, SIZE_T headerCount, const XUserGetTokenAndSignatureUtf16HttpHeader headers[], SIZE_T bodySize, const void *bodyBuffer, XAsyncBlock *async), (user, options, method, url, headerCount, headers, bodySize, bodyBuffer, async) )
IMPL_CALL( HRESULT, XUserGetTokenAndSignatureUtf16ResultSize, (XAsyncBlock *async, SIZE_T *bufferSize), (async, bufferSize) )
IMPL_CALL( HRESULT, XUserGetTokenAndSignatureUtf16Result, (XAsyncBlock *async, SIZE_T bufferSize, void *buffer, XUserGetTokenAndSignatureUtf16Data **ptrToBuffer, SIZE_T *bufferUsed), (async, bufferSize, buffer, ptrToBuffer, bufferUsed) )
IMPL_CALL( HRESULT, XUserResolveIssueWithUiAsync, (XUserHandle user, const char *url, XAsyncBlock *async), (user, url, async) )
IMPL_CALL( HRESULT, XUserResolveIssueWithUiResult, (XAsyncBlock *async), (async) )
IMPL_CALL( HRESULT, XUserResolveIssueWithUiUtf16Async, (XUserHandle user, const WCHAR *url, XAsyncBlock *async), (user, url, async) )
IMPL_CALL( HRESULT, XUserResolveIssueWithUiUtf16Result, (XAsyncBlock *async), (async) )
IMPL_CALL( HRESULT, XUserRegisterForChangeEvent, (XTaskQueueHandle queue, void *context, XUserChangeEventCallback *callback, XTaskQueueRegistrationToken *token), (queue, context, callback, token) )
IMPL_CALL( BOOLEAN, XUserUnregisterForChangeEvent, (XTaskQueueRegistrationToken token, BOOLEAN wait), (token, wait) )
IMPL_CALL( HRESULT, XUserGetSignOutDeferral, (XUserSignOutDeferralHandle *deferral), (deferral) )
IMPL_CALL( void, XUserCloseSignOutDeferralHandle, (XUserSignOutDeferralHandle deferral), (deferral) )

#undef CONSUMED_INTERFACE

// --- IXUserImpl2 --- //
#define CONSUMED_INTERFACE IXUserImpl2*

IMPL_CALL( HRESULT, XUserAddByIdWithUiAsync, (UINT64 userId, XAsyncBlock *async), (userId, async) )
IMPL_CALL( HRESULT, XUserAddByIdWithUiResult, (XAsyncBlock *async, XUserHandle *newUser), (async, newUser) )

#undef CONSUMED_INTERFACE

// --- IXUserImpl3 --- //
#define CONSUMED_INTERFACE IXUserImpl3*

IMPL_CALL( HRESULT, XUserGetMsaTokenSilentlyAsync, (XUserHandle user, XUserGetMsaTokenSilentlyOptions options, const char *scope, XAsyncBlock *async), (user, options, scope, async) )
IMPL_CALL( HRESULT, XUserGetMsaTokenSilentlyResult, (XAsyncBlock *async, SIZE_T resultTokenSize, char *resultToken, SIZE_T *resultTokenUsed), (async, resultTokenSize, resultToken, resultTokenUsed) )
IMPL_CALL( HRESULT, XUserGetMsaTokenSilentlyResultSize, (XAsyncBlock *async, SIZE_T *tokenSize), (async, tokenSize) )

#undef CONSUMED_INTERFACE

// --- IXUserImpl4 --- //
#define CONSUMED_INTERFACE IXUserImpl4*

IMPL_CALL( BOOLEAN, XUserIsStoreUser, (XUserHandle user), (user) )

#undef CONSUMED_INTERFACE

// --- IXUserImpl5 --- //
#define CONSUMED_INTERFACE IXUserImpl5*

IMPL_CALL( HRESULT, XUserPlatformRemoteConnectSetEventHandlers, (XTaskQueueHandle queue, XUserPlatformRemoteConnectEventHandlers *handlers), (queue, handlers) )
IMPL_CALL( HRESULT, XUserPlatformRemoteConnectCancelPrompt, (XUserPlatformOperation operation), (operation) )
IMPL_CALL( HRESULT, XUserPlatformSpopPromptSetEventHandlers, (XTaskQueueHandle queue, XUserPlatformSpopPromptEventHandler *handler, void *context), (queue, handler, context) )
IMPL_CALL( HRESULT, XUserPlatformSpopPromptComplete, (XUserPlatformOperation operation, XUserPlatformOperationResult result), (operation, result) )

#undef CONSUMED_INTERFACE

// --- IXUserImpl6 --- //
#define CONSUMED_INTERFACE IXUserImpl6*

IMPL_CALL( BOOLEAN, XUserIsSignOutPresent, (), () )
IMPL_CALL( HRESULT, XUserSignOutAsync, (XUserHandle user, XAsyncBlock *async), (user, async) )
IMPL_CALL( HRESULT, XUserSignOutResult, (XAsyncBlock *async), (async) )

#undef CONSUMED_INTERFACE

// --- IXUserGamertagImpl --- //
#define CONSUMED_INTERFACE IXUserGamertagImpl*

IMPL_CALL( HRESULT, XUserGetGamertag, (XUserHandle user, XUserGamertagComponent gamertagComponent, SIZE_T gamertagSize, char *gamertag, SIZE_T *gamertagUsed), (user, gamertagComponent, gamertagSize, gamertag, gamertagUsed) )

#undef CONSUMED_INTERFACE

#undef CONSUMED_CLSID

// --- XUserDeviceImpl --- //
#define CONSUMED_CLSID CLSID_XUserDeviceImpl

// --- IXUserDeviceImpl --- //
#define CONSUMED_INTERFACE IXUserDeviceImpl*

IMPL_CALL( HRESULT, XUserFindForDevice, (const APP_LOCAL_DEVICE_ID *deviceId, XUserHandle *handle), (deviceId, handle) )
IMPL_CALL( HRESULT, XUserRegisterForDeviceAssociationChanged, (XTaskQueueHandle queue, void *context, XUserDeviceAssociationChangedCallback *callback, XTaskQueueRegistrationToken *token), (queue, context, callback, token) )
IMPL_CALL( BOOLEAN, XUserUnregisterForDeviceAssociationChanged, (XTaskQueueRegistrationToken token, BOOLEAN wait), (token, wait) )
IMPL_CALL( HRESULT, XUserGetDefaultAudioEndpointUtf16, (XUserLocalId user, XUserDefaultAudioEndpointKind defaultAudioEndpointKind, SIZE_T endpointIdUtf16Count, WCHAR endpointIdUtf16[], SIZE_T *endpointIdUtf16Used), (user, defaultAudioEndpointKind, endpointIdUtf16Count, endpointIdUtf16, endpointIdUtf16Used) )
IMPL_CALL( HRESULT, XUserRegisterForDefaultAudioEndpointUtf16Changed, (XTaskQueueHandle queue, void *context, XUserDefaultAudioEndpointUtf16ChangedCallback *callback, XTaskQueueRegistrationToken *token), (queue, context, callback, token) )
IMPL_CALL( BOOLEAN, XUserUnregisterForDefaultAudioEndpointUtf16Changed, (XTaskQueueRegistrationToken token, BOOLEAN wait), (token, wait) )
IMPL_CALL( HRESULT, XUserFindControllerForUserWithUiAsync, (XUserHandle user, XAsyncBlock *async), (user, async) )
IMPL_CALL( HRESULT, XUserFindControllerForUserWithUiResult, (XAsyncBlock *async, APP_LOCAL_DEVICE_ID *deviceId), (async, deviceId) )

#undef CONSUMED_INTERFACE

// --- IXUserDeviceImpl2 --- //
#define CONSUMED_INTERFACE IXUserDeviceImpl2*

#undef CONSUMED_INTERFACE

#undef CONSUMED_CLSID