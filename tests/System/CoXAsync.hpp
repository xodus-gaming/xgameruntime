#pragma once
#include <xasync.h>
#include <xasyncprovider.h>
#include <memory>
#include <optional>
#include <variant>
#include <functional>

#include <coroutine>

namespace CoXAsync {

template<class T>
struct promise;

template<class T>
struct coroutine : std::coroutine_handle<promise<T>>
{
    using promise_type = CoXAsync::promise<T>;
};

template<class T>
struct Result
{
    std::variant<T, HRESULT> result;
    Result(T value) { 
        static_assert(!std::is_same_v<T, HRESULT>, "Return type must not match HRESULT");
        this->result = value; 
    }
    Result(HRESULT hr) { this->result = hr; }
    HRESULT get_status() const { return std::holds_alternative<HRESULT>(result) ? std::get<HRESULT>(result) : S_OK; }
    T get_value() const { return std::get<T>(result); }
};

template<class T>
struct awaitable {
    promise<T>* promise = nullptr;
    bool await_ready() noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept {
        promise->invoke_continuation();
    }
    void await_resume() noexcept {
    }
};

template<class T>
class promise
{
    std::variant<T, HRESULT> result = E_FAIL;
public:
    std::function<void(HRESULT, SIZE_T)> continuation;
    coroutine<T> get_return_object() { return { coroutine<T>::from_promise(*this) }; }
    std::suspend_always initial_suspend() noexcept { return {}; }
    awaitable<T> final_suspend() noexcept {
        return { this };
    }
    void return_value(T value) { 
        static_assert(!std::is_same_v<T, HRESULT>, "Return type must not match HRESULT");
        this->result = value;
    }
    void return_value(HRESULT hr) {
        this->result = hr;
    }
    void unhandled_exception() {}
    HRESULT get_status() const { return std::holds_alternative<HRESULT>(result) ? std::get<HRESULT>(result) : S_OK; }
    T get_value() const { return std::get<T>(result); }
    void invoke_continuation() {
        if (continuation){
            if (std::holds_alternative<T>(result)) {
                continuation(S_OK, sizeof(T));
            }
            else {
                continuation(get_status(), 0);
            }
        }
    }
};

template<class T> class XAsync {
public:
    class Context;
    using work_callback = std::function<coroutine<T>(Context context)>;
    using store_result_callback = std::function<HRESULT(void* buffer, size_t size)>;
private:
    XAsyncBlock* asyncBlock = nullptr;
    struct Data {
        XAsyncBlock asyncBlock;
        XAsyncBlock* providerBlock;
        work_callback work;
        coroutine<T> coro;
        std::atomic<bool> isCanceled{false};
        store_result_callback store_result;

        ~Data() {
            coro.destroy();
        }
    };
public:
    std::unique_ptr<Data> data;

    struct switch_to_worker {
        Context context;
        UINT32 delay = 0;
        bool await_ready() { return false; }
        void await_suspend(std::coroutine_handle<> h)
        {
            XAsyncSchedule(context.data->providerBlock, delay);
        }
        void await_resume() { }
    };
    struct store_result {
        Context context;
        size_t required_buffer_size;
        store_result_callback store_result;

        bool await_ready() { return false; }
        void await_suspend(std::coroutine_handle<> h)
        {
            context.data->store_result = std::move(store_result);
            XAsyncComplete(context.data->providerBlock, S_OK, required_buffer_size);
        }
        void await_resume() { }
    };
    class Context {
    public:
        Data* data;
        Context(Data* block) : data(block) {}
        switch_to_worker switchToWorker() { return { *this, 0 }; }
        switch_to_worker delay(UINT32 d) { return { *this, d }; }
        store_result storeResult(size_t required_buffer_size, store_result_callback store_result) { return { *this, required_buffer_size, std::move(store_result) }; }
    };

    XAsync(XAsyncBlock* block, work_callback work) : asyncBlock(block), data(std::make_unique<Data>()) {
        data->work = std::move(work);
    }

    XAsync(work_callback work) : data(std::make_unique<Data>()) {
        asyncBlock = &data->asyncBlock;
        data->work = std::move(work);
    }

    XAsync& withQueue(XTaskQueueHandle queue) {
        asyncBlock->queue = queue;
        return *this;
    }

    HRESULT begin() {
        HRESULT r = XAsyncBegin(asyncBlock, data.get(), /*(const void*)(HRESULT(XAsync<T>::*)())&XAsync<T>::begin*/ nullptr, __FUNCTION__, [](XAsyncOp op, const XAsyncProviderData* data) -> HRESULT {
            Data* contextData = static_cast<Data*>(data->context);
            switch (op) {
                case XAsyncOp::Begin:
                    contextData->providerBlock = data->async;
                    contextData->coro = contextData->work(Context(contextData));
                    contextData->coro.promise().continuation = [providerBlock = contextData->providerBlock](HRESULT hr, SIZE_T length) {
                        XAsyncComplete(providerBlock, hr, length);
                    };
                    XAsyncSchedule(data->async, 0);
                    break;
                case XAsyncOp::DoWork:
                    contextData->coro.resume();
                    if (!contextData->coro.done()) {
                        return E_PENDING;
                    }
                    break;
                case XAsyncOp::GetResult:
                    if (contextData->store_result) {
                        contextData->store_result(data->buffer, data->bufferSize);
                        contextData->store_result = nullptr;
                        contextData->coro.resume();
                    } else if (contextData->coro.done()) {
                        HRESULT status = contextData->coro.promise().get_status();
                        if (FAILED(status)) {
                            return status;
                        }
                        new(data->buffer) T(contextData->coro.promise().get_value());
                        return S_OK;
                    } else {
                        return E_PENDING;
                    }
                    break;
                case XAsyncOp::Cancel:
                    contextData->isCanceled = true;
                    break;
                case XAsyncOp::Cleanup: {
                    std::unique_ptr<Data> cleanupPtr(static_cast<Data*>(data->context));
                    break;
                }
                default:
                    break;
            }
            return S_OK;
        });
        if (FAILED(r)) {
            return r;
        }
        data.release();
        return S_OK;
    }

    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<> h)
    {
        asyncBlock->callback = [](XAsyncBlock* asyncBlock) {
            std::coroutine_handle<>::from_address(asyncBlock->context).resume();
        };
        asyncBlock->context = h.address();
        this->begin();
    }
    Result<T> await_resume() { 
        HRESULT hr = XAsyncGetStatus(asyncBlock, false);
        if (FAILED(hr)) {
            return hr;
        }
        size_t resultSize = 0;
        XAsyncGetResultSize(asyncBlock, &resultSize);
        if (resultSize != sizeof(T)) {
            return E_FAIL;
        }
        T result;
        XAsyncGetResult(asyncBlock, nullptr, resultSize, &result, nullptr);
        return result;
    }
    ~XAsync() {
        if(data.get())
            begin();
    }
};
}