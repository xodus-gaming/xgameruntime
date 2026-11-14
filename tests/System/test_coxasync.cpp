/*
 * XThreading Tests
 *  From https://github.com/microsoft/libHttpClient
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

#include <thread>

#include <windows.h>
#include <xasync.h>
#include <xasyncprovider.h>
#include <xgameruntimeinit.h>
#include "coxasync.hpp"

TEST(CoXAsync, TestCoAsyncApi)
{
    std::cout << "Starting TestCoXAsyncApi\n";
    XAsyncBlock asyncBlock{};
    memset(&asyncBlock, 0, sizeof(asyncBlock));
    XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &asyncBlock.queue);
    {
        coxasync::XAsync<int>::work_callback work = [](coxasync::XAsync<int>::Context context) -> coxasync::coroutine<int> {
            std::cout << "Starting 4431\n";
            co_await coxasync::XAsync<void>([i = 23](coxasync::XAsync<void>::Context context) -> coxasync::coroutine<void>  {
                std::cout << "Starting nested async with i = " << i << "\n";
                co_await context.delay(500);
                co_return 1;
            }).withQueue(context.getOrCreateQueue());
            auto r = co_await coxasync::XAsync<int>([i = 23](coxasync::XAsync<int>::Context context) -> coxasync::coroutine<int>  {
                std::cout << "Starting nested async with i = " << i << "\n";
                co_await context.delay(500);
                std::cout << "afterd nested async with i = " << i << "\n";
                int ret = 46;
                std::cout << "end nested async with i = " << i << "\n";
                co_return ret;
            }).withQueue(context.getOrCreateQueue());
            std::cout << "Result from nested async: " << r.get_value() << "\n";

            co_await context.delay(1000);

            std::cout << "Result from nested async: after delay " << r.get_value() << "\n";
            co_return 42;
        };
        coxasync::XAsync<int>::begin(&asyncBlock, work);
    }
    int i = 0;
    while(XAsyncGetStatus(&asyncBlock, false) == E_PENDING) {
        std::cout << "waiting for async completion, iteration " << i++ << "\n";  
        XTaskQueueDispatch(asyncBlock.queue, XTaskQueuePort::Work, 0);
    } 

    std::cout << "waiting l0\n";
    HRESULT hr = XAsyncGetStatus(&asyncBlock, true);
    ASSERT_EQ(S_OK, hr);
    int ret = -1;
    hr = coxasync::XAsync<int>::getResult(&asyncBlock, sizeof(int), &ret, nullptr);
    ASSERT_EQ(S_OK, hr);
    std::cout << "done " << ret << "\n";
    ASSERT_EQ(42, ret);
}

TEST(CoXAsync, ReturnSharedPtr)
{
    std::cout << "Starting TestCoAsyncApi\n";
    XAsyncBlock asyncBlock{};
    memset(&asyncBlock, 0, sizeof(asyncBlock));
    XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &asyncBlock.queue);
    {
        coxasync::XAsync<int>::work_callback work = [](coxasync::XAsync<int>::Context context) -> coxasync::coroutine<int> {
            std::cout << "Starting 4431\n";
            auto r = co_await coxasync::XAsync<std::shared_ptr<int>>([i = 23](auto context) -> coxasync::coroutine<std::shared_ptr<int>> {
                std::cout << "Starting nested async with i = " << i << "\n";
                co_await context.delay(500);
                std::cout << "afterd nested async with i = " << i << "\n";
                int ret = 46;
                std::cout << "end nested async with i = " << i << "\n";
                co_return std::make_shared<int>(ret);
            }).withQueue(context.getOrCreateQueue());
            std::cout << "Result from nested async: " << *r.get_value().get() << "\n";

            co_await context.delay(1000);

            std::cout << "Result from nested async: after delay " << *r.get_value().get() << "\n";
            co_return 42;
        };
        coxasync::XAsync<int>::begin(&asyncBlock, work);
    }
    int i = 0;
    while(XAsyncGetStatus(&asyncBlock, false) == E_PENDING) {
        std::cout << "waiting for async completion, iteration " << i++ << "\n";  
        XTaskQueueDispatch(asyncBlock.queue, XTaskQueuePort::Work, 0);
    } 

    std::cout << "waiting l0\n";
    HRESULT hr = XAsyncGetStatus(&asyncBlock, true);
    ASSERT_EQ(S_OK, hr);
    int ret = -1;
    hr = coxasync::XAsync<int>::getResult(&asyncBlock, sizeof(int), &ret, nullptr);
    ASSERT_EQ(S_OK, hr);
    std::cout << "done " << ret << "\n";
    ASSERT_EQ(42, ret);
}

TEST(CoXAsync, DynamicReturn)
{
    std::cout << "Starting TestCoAsyncApi\n";
    XAsyncBlock asyncBlock{};
    memset(&asyncBlock, 0, sizeof(asyncBlock));
    {
        coxasync::XAsync<coxasync::dynamic_result>::work_callback work = [](coxasync::XAsync<coxasync::dynamic_result>::Context context) -> coxasync::coroutine<coxasync::dynamic_result> {
            std::string token = "Hello World";
            co_await context.storeResult(token.size(), [&token](void* buffer, size_t size) {
                if (buffer && size >= token.size()) {
                    memcpy(buffer, token.data(), token.size());
                }
                return S_OK;
            });
            co_return S_OK;
        };
        coxasync::XAsync<coxasync::dynamic_result>::begin(&asyncBlock, work);
    }
    int i = 0;

    std::cout << "waiting l0\n";
    HRESULT hr = XAsyncGetStatus(&asyncBlock, true);
    ASSERT_EQ(S_OK, hr);
    SIZE_T bufferSize = 0;
    hr = XAsyncGetResultSize(&asyncBlock, &bufferSize);
    ASSERT_EQ(S_OK, hr);
    std::cout << "Required buffer size: " << bufferSize << "\n";
    std::cout << "waiting l0\n";
    std::vector<char> ret;
    ret.resize(bufferSize);
    hr = coxasync::XAsync<coxasync::dynamic_result>::getResult(&asyncBlock, bufferSize, ret.data(), nullptr);
    ASSERT_EQ(S_OK, hr);
    std::cout << "Required buffer size: " << bufferSize << "\n";
    std::cout << "waiting l0 "<< ret.size() << " \n";
    std::cout << "done " << std::string(ret.begin(), ret.end()) << "\n";
    ASSERT_EQ("Hello World", std::string(ret.begin(), ret.end()));
}