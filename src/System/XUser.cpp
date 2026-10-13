/*
 * Xbox Game runtime Library
 *  GDK Component: System API -> XThread, XAsync and XTask
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

#include "../private.h"

#include <atomic>
#include <vector>
#include <coxasync.hpp>

class XUserImpl6 : 
    public IXUserImpl6
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
             iid == __uuidof( IXUserImpl6 ) )
        {
            AddRef();
            *out = static_cast<IXUserImpl6 *>(this);
            return S_OK;
        }

        FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( &iid ) );
        *out = nullptr;
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

        // Polymorphic classes should not be deleted.
        /*
        if ( !curr )
            delete this;
        */

        return curr;
    }

    HRESULT STDMETHODCALLTYPE XUserAddAsync(
        XUserAddOptions options,
        XAsyncBlock *async) override {
        TRACE( "XUserAddAsync called with options %d, async %p.\n", options, async );
        return coxasync::XAsync<XUserHandle>::begin( async, [](auto ctx) -> coxasync::coroutine<XUserHandle> {

            co_return XUserHandle{};
        } );
    }

    HRESULT STDMETHODCALLTYPE XUserAddResult(
        XAsyncBlock *async,
        XUserHandle *newUser) override {
        TRACE( "XUserAddResult called with async %p, newUser %p.\n", async, newUser );
        return coxasync::XAsync<XUserHandle>::getResult( async, sizeof(XUserHandle), newUser, nullptr );
    }

    HRESULT STDMETHODCALLTYPE XUserGetTokenAndSignatureAsync(
        XUserHandle user,
        XUserGetTokenAndSignatureOptions options,
        const char *method,
        const char *url,
        SIZE_T headerCount,
        const XUserGetTokenAndSignatureHttpHeader headers[],
        SIZE_T bodySize,
        const void *bodyBuffer,
        XAsyncBlock *async) override {
        TRACE( "XUserGetTokenAndSignatureAsync called with user %p, options %d, method %s, url %s, headerCount %zu, bodySize %zu, async %p.\n",
            user, options, method, url, headerCount, bodySize, async );
        std::string methodStr( method );
        std::string urlStr( url );
        std::string body( static_cast<const char *>(bodyBuffer), bodySize );
        std::vector<std::pair<std::string, std::string>> headerVec;
        for (SIZE_T i = 0; i < headerCount; ++i) {
            headerVec.emplace_back( headers[i].name, headers[i].value );
        }
        return coxasync::XAsync<coxasync::dynamic_result>::begin( async, [=](auto ctx) -> coxasync::coroutine<coxasync::dynamic_result> {
            std::string token = "dummy_token";
            std::string signature = "dummy_signature";
            co_await ctx.storeResult(sizeof(XUserGetTokenAndSignatureData) + token.size() + signature.size() + 2, [&](void* buffer, size_t size) {
                XUserGetTokenAndSignatureData* data = static_cast<XUserGetTokenAndSignatureData*>(buffer);
                data->token = reinterpret_cast<char*>(data + 1);
                data->signature = data->token + token.size() + 1;
                memcpy((void*)data->token, token.c_str(), token.size() + 1);
                memcpy((void*)data->signature, signature.c_str(), signature.size() + 1);
                return S_OK;
            });
            co_return S_OK;
        } );

    }

    HRESULT STDMETHODCALLTYPE XUserGetTokenAndSignatureResultSize(
        XAsyncBlock *async,
        SIZE_T *bufferSize) override {
        TRACE( "XUserGetTokenAndSignatureResultSize called with async %p, bufferSize %p.\n", async, bufferSize );
        return XAsyncGetResultSize( async, bufferSize );
    }

    HRESULT STDMETHODCALLTYPE XUserGetTokenAndSignatureResult(
        XAsyncBlock *async,
        SIZE_T bufferSize,
        void *buffer,
        XUserGetTokenAndSignatureData **ptrToBuffer,
        SIZE_T *bufferUsed) override {
        TRACE( "XUserGetTokenAndSignatureResult called with async %p, bufferSize %zu, buffer %p, ptrToBuffer %p, bufferUsed %p.\n",
            async, bufferSize, buffer, ptrToBuffer, bufferUsed );
        if (ptrToBuffer) {
            *ptrToBuffer = static_cast<XUserGetTokenAndSignatureData *>(buffer);
        }
        return coxasync::XAsync<coxasync::dynamic_result>::getResult( async, bufferSize, buffer, bufferUsed );
    }

        virtual HRESULT STDMETHODCALLTYPE XUserDuplicateHandle(
        XUserHandle handle,
        XUserHandle *duplicatedHandle) override {
        TRACE("unimplemented");
        abort();
    }

    virtual void STDMETHODCALLTYPE XUserCloseHandle(
        XUserHandle user) override {
        TRACE("unimplemented");
        abort();
    }

    virtual INT32 STDMETHODCALLTYPE XUserCompare(
        XUserHandle user1,
        XUserHandle user2) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserGetMaxUsers(
        UINT32 *maxUsers) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserGetLocalId(
        XUserHandle user,
        XUserLocalId *userLocalId) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserFindUserByLocalId(
        XUserLocalId userLocalId,
        XUserHandle *handle) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserGetId(
        XUserHandle user,
        UINT64 *userId) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserFindUserById(
        UINT64 userId,
        XUserHandle *handle) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserGetIsGuest(
        XUserHandle user,
        BOOLEAN *isGuest) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserGetState(
        XUserHandle user,
        XUserState *state) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE __PADDING__(
        ) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserGetGamerPictureAsync(
        XUserHandle user,
        XUserGamerPictureSize pictureSize,
        XAsyncBlock *async) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserGetGamerPictureResultSize(
        XAsyncBlock *async,
        SIZE_T *bufferSize) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserGetGamerPictureResult(
        XAsyncBlock *async,
        SIZE_T bufferSize,
        void *buffer,
        SIZE_T *bufferUsed) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserGetAgeGroup(
        XUserHandle user,
        XUserAgeGroup *ageGroup) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserCheckPrivilege(
        XUserHandle user,
        XUserPrivilegeOptions options,
        XUserPrivilege privilege,
        BOOLEAN *hasPrivilege,
        XUserPrivilegeDenyReason *reason) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserResolvePrivilegeWithUiAsync(
        XUserHandle user,
        XUserPrivilegeOptions options,
        XUserPrivilege privilege,
        XAsyncBlock *async) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserResolvePrivilegeWithUiResult(
        XAsyncBlock *async) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserGetTokenAndSignatureUtf16Async(
        XUserHandle user,
        XUserGetTokenAndSignatureOptions options,
        const WCHAR *method,
        const WCHAR *url,
        SIZE_T headerCount,
        const XUserGetTokenAndSignatureUtf16HttpHeader headers[],
        SIZE_T bodySize,
        const void *bodyBuffer,
        XAsyncBlock *async) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserGetTokenAndSignatureUtf16ResultSize(
        XAsyncBlock *async,
        SIZE_T *bufferSize) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserGetTokenAndSignatureUtf16Result(
        XAsyncBlock *async,
        SIZE_T bufferSize,
        void *buffer,
        XUserGetTokenAndSignatureUtf16Data **ptrToBuffer,
        SIZE_T *bufferUsed) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserResolveIssueWithUiAsync(
        XUserHandle user,
        const char *url,
        XAsyncBlock *async) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserResolveIssueWithUiResult(
        XAsyncBlock *async) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserResolveIssueWithUiUtf16Async(
        XUserHandle user,
        const WCHAR *url,
        XAsyncBlock *async) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserResolveIssueWithUiUtf16Result(
        XAsyncBlock *async) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserRegisterForChangeEvent(
        XTaskQueueHandle queue,
        void *context,
        XUserChangeEventCallback *callback,
        XTaskQueueRegistrationToken *token) override {
        TRACE("unimplemented");
        abort();
    }

    virtual BOOLEAN STDMETHODCALLTYPE XUserUnregisterForChangeEvent(
        XTaskQueueRegistrationToken token,
        BOOLEAN wait) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserGetSignOutDeferral(
        XUserSignOutDeferralHandle *deferral) override {
        TRACE("unimplemented");
        abort();
    }

    virtual void STDMETHODCALLTYPE XUserCloseSignOutDeferralHandle(
        XUserSignOutDeferralHandle deferral) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserAddByIdWithUiAsync(
        UINT64 userId,
        XAsyncBlock *async) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserAddByIdWithUiResult(
        XAsyncBlock *async,
        XUserHandle *newUser) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserGetMsaTokenSilentlyAsync(
        XUserHandle user,
        XUserGetMsaTokenSilentlyOptions options,
        const char *scope,
        XAsyncBlock *async) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserGetMsaTokenSilentlyResult(
        XAsyncBlock *async,
        SIZE_T resultTokenSize,
        char *resultToken,
        SIZE_T *resultTokenUsed) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserGetMsaTokenSilentlyResultSize(
        XAsyncBlock *async,
        SIZE_T *tokenSize) override {
        TRACE("unimplemented");
        abort();
    }

    virtual BOOLEAN STDMETHODCALLTYPE XUserIsStoreUser(
        XUserHandle user) override {
        TRACE("unimplemented");
        abort();
    }


    virtual HRESULT STDMETHODCALLTYPE XUserPlatformRemoteConnectSetEventHandlers(
        XTaskQueueHandle queue,
        XUserPlatformRemoteConnectEventHandlers *handlers) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserPlatformRemoteConnectCancelPrompt(
        XUserPlatformOperation operation) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserPlatformSpopPromptSetEventHandlers(
        XTaskQueueHandle queue,
        XUserPlatformSpopPromptEventHandler *handler,
        void *context) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserPlatformSpopPromptComplete(
        XUserPlatformOperation operation,
        XUserPlatformOperationResult result) override {
        TRACE("unimplemented");
        abort();
    }


    virtual BOOLEAN STDMETHODCALLTYPE XUserIsSignOutPresent(
        ) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserSignOutAsync(
        XUserHandle user,
        XAsyncBlock *async) override {
        TRACE("unimplemented");
        abort();
    }

    virtual HRESULT STDMETHODCALLTYPE XUserSignOutResult(
        XAsyncBlock *async) override {
        TRACE("unimplemented");
        abort();
    }


private:
    std::atomic_long ref{ 1 };
};

static XUserImpl6 g_x_user_impl;

IXUserImpl *x_user_impl = static_cast<IXUserImpl*>(&g_x_user_impl);