/*
 * XAsync C++ Wrapper
 *
 * Copyright 2026 ChristopherHX
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

#include "private/logging.h"
#include <coroutine>
#include <cstring>
#include <functional>
#include <mutex>
#include <memory>
#include <semaphore>
#include <utility>
#include <asyncinfo.h>

#define E_ILLEGAL_DELEGATE_ASSIGNMENT                      _HRESULT_TYPEDEF_(0x80000018)

template<typename T>
using async_operation_callback = std::function<HRESULT( T* result )>;

namespace XGameRuntime
{
    interface IXAsyncOperationCompletedHandler
    {
    public:
        virtual ~IXAsyncOperationCompletedHandler() = default;
        virtual HRESULT STDMETHODCALLTYPE Invoke( XAsyncBlock *asyncBlock ) = 0;
    };

    using CompletionHandler = std::shared_ptr<IXAsyncOperationCompletedHandler>;

    template<typename T>
    class XAsync
    {
    public:
        XAsync() = default;
        virtual ~XAsync() = default;
        XAsync( const XAsync& ) = delete;
        XAsync& operator=( const XAsync& ) = delete;

        /* IAsyncOperation<TResult> methods */
        HRESULT WINAPI
        put_Completed( CompletionHandler routine ) noexcept
        {
            HRESULT hr;

            TRACE( "iface %p, routine %p\n", this, static_cast<PVOID>(&routine) );

            if ( !routine )
                return E_POINTER;

            {
                std::unique_lock<std::mutex> lock_guard( lock );

                if ( completionHandlerAssigned )
                    return E_ILLEGAL_DELEGATE_ASSIGNMENT;

                completed = std::move( routine );
                completionHandlerAssigned = true;

                if ( status > Started )
                {
                    completed.reset(); /* Prevent concurrent invoke. */
                    lock_guard.unlock();

                    routine->Invoke( &block );

                    return S_OK;
                }
            }

            return S_OK;
        }

        HRESULT WINAPI
        get_Completed( CompletionHandler *routine ) noexcept
        {
            TRACE( "iface %p, routine %p\n", this, routine );

            if ( !routine )
                return E_POINTER;

            {
                const std::lock_guard<std::mutex> lock_guard( lock );

                if ( !completionHandlerAssigned )
                    *routine = nullptr;
                else
                {
                    *routine = completed;
                }
            }

            return S_OK;
        }

        XAsyncBlock* WINAPI
        get_Block() noexcept
        {
            return &block;
        }

        HRESULT WINAPI
        GetResults( BOOLEAN wait, T *results )
        {
            HRESULT hr;

            TRACE( "iface %p, results %p\n", this, results );

            if ( !results )
                return E_POINTER;

            hr = XAsyncGetStatus( &block, wait );
            if ( FAILED( hr ) ) return hr;

            hr = XAsyncGetResult( &block, (PVOID)Create, sizeof(T), results, nullptr );
            if ( FAILED( hr ) ) return hr;

            return S_OK;
        }

        /* Internal methods */
        static HRESULT WINAPI
        Create( XTaskQueueHandle queue, async_operation_callback<T> work, std::shared_ptr<XAsync<T>> *out )
        {
            HRESULT hr = S_OK;
            auto impl = std::make_shared<XAsync<T>>();
            auto asyncctx = std::make_unique<AsyncContext>();

            auto completioncb = std::make_unique<CallbackThunk>(
            [impl]( XAsyncBlock* async )
            {
                async_completion_callback( *impl );
                // impl is released automatically.
            });

            TRACE( "queue %p, work %p, out %p\n", queue, static_cast<PVOID>(&work), out );

            static_assert( std::is_trivially_copyable_v<T> );
            static_assert( std::is_default_constructible_v<T> );

            impl->block.queue = queue;
            impl->block.context = completioncb.get();
            impl->block.callback = CallbackThunk::Callback;
            impl->completed.reset();
            impl->status = AsyncStatus::Started;

            asyncctx->work = std::move(work);

            hr = XAsyncBegin( &impl->block, asyncctx.get(), (PVOID)Create, __FUNCTION__, async_worker_callback );
            if ( FAILED( hr ) )
                return hr;

            if ( out )
                *out = impl;

            completioncb.release();
            asyncctx.release();
            return hr;
        }

    private:
        struct AsyncContext
        {
            T result;
            async_operation_callback<T> work;
        };

        class CallbackThunk
        {
        public:
            CallbackThunk() = default;

            explicit CallbackThunk( std::function<void(XAsyncBlock*)> func )
                : _func( std::move(func) )
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
        async_completion_callback( XAsync<T>& impl )
        {
            CompletionHandler handler;

            {
                const std::lock_guard<std::mutex> guard( impl.lock );
                impl.status = AsyncStatus::Completed;
                handler = std::move( impl.completed );
            }

            if ( handler )
                handler->Invoke( &impl.block );
        }

        static HRESULT CALLBACK
        async_worker_callback( XAsyncOp opCode, const XAsyncProviderData* data )
        {
            auto* ctx = static_cast<AsyncContext*>(data->context);
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
                delete ctx;
                break;

            case XAsyncOp::GetResult:
                std::memcpy( data->buffer, &ctx->result, sizeof(T) );
                break;

            case XAsyncOp::DoWork:
                hr = ctx->work( &ctx->result );
                XAsyncComplete( data->async, hr, sizeof(T) );
                break;
            }

            return hr;
        }

        std::mutex lock;
        CompletionHandler completed = nullptr;
        BOOLEAN completionHandlerAssigned = false;
        XAsyncBlock block{};
        AsyncStatus status{};
    };
}

namespace ABI::XGameRuntime
{
    template<typename T>
    class XAsync final
        : public ::XGameRuntime::XAsync<T>
    {
        using Base = ::XGameRuntime::XAsync<T>;

    public:
        // then should not be called twice!
        XAsync&
        then( std::function<void(XAsyncBlock*)> continuation )
        {
            if ( !continuation )
                return *this;

            this->put_Completed( std::make_shared<ContinuationHandler>( std::move(continuation) ) );

            return *this;
        }

        XAsync&
        wait()
        {
            check_hr_(XAsyncGetStatus(this->get_Block(), true));

            return *this;
        }

    private:
        class ContinuationHandler final
            : public ::XGameRuntime::IXAsyncOperationCompletedHandler
        {
        public:
            explicit ContinuationHandler( const std::function<void(XAsyncBlock*)>& continuation )
                : _continuation(continuation )
            {
            }

            HRESULT WINAPI
            Invoke( XAsyncBlock* asyncBlock ) override
            {
                _continuation( asyncBlock );
                return S_OK;
            }

        private:
            std::function<void(XAsyncBlock*)> _continuation{};
        };
    };

    template<typename T>
    class XAsyncAwaiter
    {
    public:
        explicit XAsyncAwaiter( const XAsync<T>& async )
            : _async(async)
        {
        }

        [[nodiscard]]
        bool await_ready() const noexcept
        {
            return false;
        }

        bool await_suspend( std::coroutine_handle<> handle )
        {
            _async.then( [handle]( XAsyncBlock* )
            {
                handle.resume();
            } );

            return true;
        }

        T await_resume()
        {
            T result;
            check_hr_( _async.GetResults( true, &result ) );
            return result;
        }

    private:
        const XAsync<T>& _async;
    };

    template<typename T>
    auto operator co_await( const XAsync<T>& async )
    {
        return XAsyncAwaiter<T>{ async };
    }
}

#endif
