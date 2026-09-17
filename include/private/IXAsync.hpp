/*
 * XAsync C++ Wrapper
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

// TODO: Create XAsyncAction for result-less operations.

#ifndef XGAMERUNTIME_ASYNCWRAPPER_H
#define XGAMERUNTIME_ASYNCWRAPPER_H

#include <async.h>

#include <functional>
#include <mutex>
#include <atomic>

#define HANDLER_NOT_SET ((void *)~(ULONG_PTR)0)
#define E_ILLEGAL_DELEGATE_ASSIGNMENT                      _HRESULT_TYPEDEF_(0x80000018)

using namespace ABI;
using namespace ABI::XGameRuntime;

template<typename T>
using async_operation_callback = HRESULT (WINAPI *)( IUnknown *invoker, PVOID param, T *result );

// Avoid passing raw interfaces to XAsync, as T needs to be trivially copyable.
// Use interface pointers instead such as XAsync<IUnknown *>.
template<typename T>
class XAsync
    : public IXAsync<T>
{
public:
    XAsync() = default;
    virtual ~XAsync() = default;

    /* IUnknown Methods */
    HRESULT WINAPI
    QueryInterface( REFIID iid, void** out ) noexcept override
    {
        TRACE( "iface %p, iid %s, out %p.\n", this, debugstr_guid( &iid ), out );

        if (!out) return E_POINTER;
        *out = nullptr;

        if ( iid == __uuidof( IUnknown ) ||
             iid == __uuidof( IInspectable ) ||
             iid == __uuidof( IXAsync<T> ) )
        {
            AddRef();
            *out = static_cast<IXAsync<T> *>(this);
            return S_OK;
        }

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

        if ( !curr )
        {
            if ( completed )
            {
                completed->Release();
            }
            delete this;
        }

        return curr;
    }

    /* IInspectable Methods */
    HRESULT WINAPI
    GetIids( ULONG *iid_count, IID **iids ) noexcept override
    {
        TRACE( "iface %p, iid_count %p, iids %p\n", this, iid_count, iids );

        if ( !iid_count || !iids )
            return E_POINTER;

        *iid_count = 1;
        IID* allocated = static_cast<IID*>( CoTaskMemAlloc( sizeof(IID) * (*iid_count) ) );

        if ( !allocated )
            return E_OUTOFMEMORY;

        allocated[0] = __uuidof( IXAsync<T> );

        *iids = allocated;
        return S_OK;
    }

    HRESULT WINAPI
    GetRuntimeClassName( HSTRING *class_name ) noexcept override
    {
        TRACE( "iface %p, class_name %p\n", this, class_name );
        return WindowsCreateString( (LPCWSTR)L"Windows.Foundation.IAsyncAction`1<IInspectable>", 48, class_name );
    }

    HRESULT WINAPI
    GetTrustLevel( TrustLevel *trust_level ) noexcept override
    {
        FIXME( "iface %p, trust_level %p stub!\n", this, trust_level );
        return E_NOTIMPL;
    }

    /* IAsyncOperation<TResult> methods */
    HRESULT WINAPI
    put_Completed( IXAsyncOperationCompletedHandlerImpl *routine ) noexcept override
    {
        HRESULT hr;

        TRACE( "iface %p, routine %p\n", this, routine );

        {
            std::unique_lock<std::mutex> lock_guard( lock );

            if ( completed != HANDLER_NOT_SET )
                return E_ILLEGAL_DELEGATE_ASSIGNMENT;

            // Keep the routine alive until invokation.
            routine = completed;
            routine->AddRef();

            if ( status > Started )
            {
                completed = nullptr; /* Prevent concurrent invoke. */
                lock_guard.unlock();

                routine->Invoke( &block );
                routine->Release();

                return S_OK;
            }
        }

        return S_OK;
    }

    HRESULT WINAPI
    get_Completed( IXAsyncOperationCompletedHandlerImpl **routine ) noexcept override
    {
        TRACE( "iface %p, routine %p\n", this, routine );

        {
            const std::lock_guard<std::mutex> lock_guard( lock );

            if ( completed == nullptr || completed == HANDLER_NOT_SET )
                *routine = nullptr;
            else
            {
                completed->AddRef();
                *routine = completed;
            }
        }

        return S_OK;
    }

    XAsyncBlock* WINAPI
    get_Block() noexcept override
    {
        return &block;
    }

    HRESULT WINAPI
    GetResults( BOOLEAN wait, T *results ) override
    {
        HRESULT hr;
        T result;

        TRACE( "iface %p, results %p\n", this, results );

        if ( !results )
            return E_POINTER;

        hr = XAsyncGetStatus( &block, wait );
        if ( FAILED( hr ) ) return hr;

        hr = XAsyncGetResult( &block, (PVOID)Create, sizeof(T), &result, nullptr );
        if ( FAILED( hr ) ) return hr;

        *results = result;

        return S_OK;
    }

    /* Internal methods */
    static HRESULT WINAPI
    Create( IUnknown *invoker, PVOID context, XTaskQueueHandle queue, async_operation_callback<T> work,
                IXAsync<T> **out )
    {
        HRESULT hr = S_OK;;
        XAsync<T> *impl = new XAsync<T>();
        AsyncContext *asyncctx = new AsyncContext();

        // Capturing by reference here may result in a dangling pointer
        CallbackThunk *completioncb = new CallbackThunk( [=]( XAsyncBlock* async )
        {
            async_completion_callback( impl );
            impl->Release();
            if ( !out )
                impl->Release(); //self destructing operation.
            impl->status = AsyncStatus::Completed;
        } );

        TRACE( "invoker %p, context %p, queue %p, work %p, out %p\n", invoker, context, queue, work, out );

        static_assert( std::is_trivially_copyable_v<T> );

        impl->block.queue = queue;
        impl->block.context = static_cast<PVOID>( completioncb );
        impl->block.callback = CallbackThunk::Callback;
        impl->completed = static_cast<IXAsyncOperationCompletedHandlerImpl*>( HANDLER_NOT_SET );
        impl->status = AsyncStatus::Started;

        // Keep XAsync alive until completioncb is invoked.
        impl->AddRef();

        invoker->AddRef();
        asyncctx->invoker = invoker;
        asyncctx->context = context;
        asyncctx->work = work;

        hr = XAsyncBegin( &impl->block, asyncctx, (PVOID)Create, __FUNCTION__, async_worker_callback );
        if ( FAILED( hr ) )
        {
            delete asyncctx;
            impl->Release();

            // Also release the reference held by completioncb, as it's never going to be invoked.
            impl->Release();
            delete completioncb;
            return hr;
        }

        if ( out )
            *out = impl;

        return hr;
    }

private:
    struct AsyncContext
    {
        HRESULT hr;
        T result;
        async_operation_callback<T> work;
        IUnknown *invoker;
        PVOID context;
    };

    class CallbackThunk
    {
    public:
        CallbackThunk() = default;

        explicit CallbackThunk( std::function<void(XAsyncBlock*)> func )
            : _func(func)
        {
        }

        static void CALLBACK Callback( XAsyncBlock* async )
        {
            const CallbackThunk* pthis = static_cast<CallbackThunk*>(async->context);
            pthis->_func( async );
            delete pthis; // Thunks are to be deleted after used since completion callbacks are only invoked once.
        }

    private:
        std::function<void(XAsyncBlock*)> _func;
    };

    static void CALLBACK
    async_completion_callback( IUnknown* iface )
    {
        XAsync *impl = static_cast<XAsync *>( iface );
        IXAsyncOperationCompletedHandlerImpl* handler;

        {
            const std::lock_guard<std::mutex> lock( impl->lock );
            handler = impl->completed;
            impl->completed = nullptr;
        }
        if ( handler )
        {
            handler->Invoke( &impl->block );
            handler->Release();
        }
    }

    static HRESULT CALLBACK
    async_worker_callback( XAsyncOp opCode, const XAsyncProviderData* data )
    {
        AsyncContext* ctx = static_cast<AsyncContext*>(data->context);
        HRESULT hr = S_OK;

        switch (opCode)
        {
        case XAsyncOp::Begin:
            hr = XAsyncSchedule( data->async, 0 );
            break;

        case XAsyncOp::Cancel:
            // Cancellation not supported yet.
            break;

        case XAsyncOp::Cleanup:
            ctx->invoker->Release();
            delete ctx;
            break;

        case XAsyncOp::GetResult:
            RtlCopyMemory( data->buffer, &ctx->result, sizeof(T) );
            break;

        case XAsyncOp::DoWork:
            hr = ctx->work( ctx->invoker, ctx->context, &ctx->result );
            XAsyncComplete( data->async, hr, sizeof(T) );
            break;
        }

        return hr;
    }

    std::atomic_long ref{ 1 };
    std::mutex lock;
    IXAsyncOperationCompletedHandlerImpl *completed = nullptr;
    XAsyncBlock block{};
    AsyncStatus status{};
};

#endif
