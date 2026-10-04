#pragma once
#include <xasync.h>
#include <xasyncprovider.h>
#include <memory>
#include <optional>
#include <variant>
#include <functional>

// template<class T> class Result {
//     HRESULT hr;
//     std::optional<T> value;
//     std::variant<T, HRESULT, XAsync
// public:
//     Result(HRESULT hr) : hr(hr) {}
//     Result(T value) : hr(S_OK), value(value) {}
//     HRESULT getHr() const { return hr; }
//     std::optional<T> getValue() const { return value; }
// };
#include <coroutine>

namespace CoXAsync {

// struct task
// {
//     struct promise_type
//     {
//         task get_return_object() { return {}; }
//         std::suspend_always initial_suspend() { return {
//             // Call XAsyncBegin? / or is this the caller doing?
//         }; }
//         std::suspend_never final_suspend() noexcept { return {}; }
//         void return_void() {}
//         void unhandled_exception() {}
//     };
// };

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
    Result<T> result = E_FAIL;
    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<> h)
    {
        h.resume();
    }
    Result<T> await_resume() { return this->result; }
};

template<class T>
struct promise
{
    std::variant<T, HRESULT> result;
    coroutine<T> get_return_object() { return { coroutine<T>::from_promise(*this) }; }
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_value(T value) { 
        static_assert(!std::is_same_v<T, HRESULT>, "Return type must not match HRESULT");
        this->result = value; 
    }
    void return_value(HRESULT hr) { this->result = hr; }
    void unhandled_exception() {}
    HRESULT get_status() const { return std::holds_alternative<HRESULT>(result) ? std::get<HRESULT>(result) : S_OK; }
    T get_value() const { return std::get<T>(result); }
};

template<class T> class XAsync {
public:
    class Context;
    using work_callback = std::function<coroutine<T>(Context context)>;
    using store_result_callback = std::function<HRESULT(void* buffer, size_t* size)>;
private:
    XAsyncBlock* asyncBlock;
    struct Data {
        XAsyncBlock asyncBlock;
        XAsyncBlock* providerBlock;
        // Custom data for the async operation
        work_callback work;
        coroutine<T> coro;
        std::atomic<bool> isCanceled{false};
        store_result_callback store_result;
    };

    std::unique_ptr<Data> data;
public:
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
        Data* data;
    public:
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
        HRESULT r = XAsyncBegin(asyncBlock, data.get(), (const void*)(HRESULT(XAsync<T>::*)())&XAsync<T>::begin, __FUNCTION__, [](XAsyncOp op, const XAsyncProviderData* data) -> HRESULT {
            Data* contextData = static_cast<Data*>(data->context);
            switch (op) {
                case XAsyncOp::Begin:
                    // Handle begin
                    contextData->providerBlock = data->async;
                    contextData->coro = contextData->work(Context(&contextData->asyncBlock));
                    XAsyncSchedule(&contextData->asyncBlock, 0);
                    break;
                case XAsyncOp::DoWork:
                    // Handle work
                    contextData->coro.resume();
                    if (!contextData->coro.done()) {
                        return E_PENDING;
                    }
                    break;
                case XAsyncOp::GetResult:
                    // Handle get result
                    if (contextData->store_result) {
                        contextData->store_result(data->buffer, &contextData->required_buffer_size);
                    }
                    break;
                case XAsyncOp::Cancel:
                    // Handle cancel
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
        // Keep it alive delegate lifetime to provider
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
        XAsyncGetResult(asyncBlock, &result, resultSize);
        return result;
    }
    ~XAsync() {
        begin();
    } 
};
}