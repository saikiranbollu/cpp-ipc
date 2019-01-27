#pragma once

#include <Windows.h>

#include <algorithm>
#include <iterator>
#include <atomic>
#include <array>
#include <tuple>

#include "rw_lock.h"
#include "pool_alloc.h"

#include "platform/to_tchar.h"
#include "platform/detail.h"

namespace ipc {
namespace detail {

class waiter {
    std::atomic<long> counter_ { 0 };

public:
    using handle_t = HANDLE;

    constexpr static handle_t invalid() {
        return NULL;
    }

    handle_t open(char const * name) {
        if (name == nullptr || name[0] == '\0') return invalid();
        return ::CreateSemaphore(NULL, 0, LONG_MAX, ipc::detail::to_tchar(name).c_str());
    }

    void close(handle_t h) {
        if (h == invalid()) return;
        ::CloseHandle(h);
    }

    static bool wait_all(std::tuple<waiter*, handle_t> const * all, std::size_t size) {
        if (all == nullptr || size == 0) {
            return false;
        }
        auto hs = static_cast<handle_t*>(mem::alloc(sizeof(handle_t[size])));
        IPC_UNUSED_ auto guard = unique_ptr(hs, [size](void* p) { mem::free(p, sizeof(handle_t[size])); });
        std::size_t i = 0;
        for (; i < size; ++i) {
            auto& info = all[i];
            if ((std::get<0>(all[i]) == nullptr) ||
                (std::get<1>(all[i]) == invalid())) continue;
            std::get<0>(info)->counter_.fetch_add(1, std::memory_order_relaxed);
            hs[i] = std::get<1>(all[i]);
        }
        std::atomic_thread_fence(std::memory_order_release);
        return ::WaitForMultipleObjects(hs, i, FALSE, INFINITE) != WAIT_FAILED;
    }

    bool wait(handle_t h) {
        if (h == invalid()) return false;
        counter_.fetch_add(1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        return ::WaitForSingleObject(h, INFINITE) == WAIT_OBJECT_0;
    }

    void notify(handle_t h) {
        if (h == invalid()) return;
        for (unsigned k = 0;;) {
            auto c = counter_.load(std::memory_order_acquire);
            if (c == 0) return;
            if (counter_.compare_exchange_weak(c, c - 1, std::memory_order_release)) {
                break;
            }
            ipc::yield(k);
        }
        ::ReleaseSemaphore(h, 1, NULL);
    }

    void broadcast(handle_t h) {
        if (h == invalid()) return;
        ::ReleaseSemaphore(h, counter_.exchange(0, std::memory_order_acquire), NULL);
    }
};

} // namespace detail
} // namespace ipc
