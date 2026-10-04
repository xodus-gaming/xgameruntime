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
private:
    XAsyncBlock* asyncBlock;
    struct Data {
        XAsyncBlock asyncBlock;
        // Custom data for the async operation
    };

    std::unique_ptr<Data> data;
public:
    XAsync(XAsyncBlock* block) : asyncBlock(block), data(std::make_unique<Data>()) {}

    XAsync() : data(std::make_unique<Data>()) {
        asyncBlock = &data->asyncBlock;
    }

    XAsync& withQueue(XTaskQueueHandle queue) {
        asyncBlock->queue = queue;
        return *this;
    }

    XAsync& then(std::function<void(std::variant<T, HRESULT> result)> callback) {
        // asyncBlock->callback = callback;

        return *this;
    }

    HRESULT begin(std::function<std::variant<T, HRESULT, XAsync<T>>(XAsyncBlock*)> work) {
        HRESULT r = XAsyncBegin(asyncBlock, data.get(), nullptr/*(const void*)(HRESULT(XAsync<int>::*)(std::function<std::variant<int, HRESULT, XAsync<int>> (XAsyncBlock *)>))&XAsync<T>::begin*/, __FUNCTION__, [](XAsyncOp op, const XAsyncProviderData* data) -> HRESULT {
            switch (op) {
                case XAsyncOp::Begin:
                    // Handle begin
                    break;
                case XAsyncOp::DoWork:
                    // Handle work
                    break;
                case XAsyncOp::GetResult:
                    // Handle get result
                    break;
                case XAsyncOp::Cancel:
                    // Handle cancel
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
};
}