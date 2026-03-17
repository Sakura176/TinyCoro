#include <iostream>
#include <coroutine>
#include <tuple>
#include <exception>
#include <mutex>
#include <atomic>
#include <type_traits>
#include <utility>

// 简化版本的concepts
namespace concepts {
    template<typename T>
    concept awaiter = requires(T t, std::coroutine_handle<> h) {
        { t.await_ready() } -> std::same_as<bool>;
        { t.await_suspend(h) };
        { t.await_resume() };
    };

    template<typename T>
    concept awaitable = awaiter<T> || requires(T t) {
        { t.operator co_await() } -> awaiter;
    } || requires(T t) {
        { operator co_await(t) } -> awaiter;
    };

    template<awaitable T>
    auto get_awaiter(T&& t) {
        if constexpr (awaiter<T>) {
            return std::forward<T>(t);
        } else if constexpr (requires { t.operator co_await(); }) {
            return t.operator co_await();
        } else {
            return operator co_await(t);
        }
    }

    template<awaitable T>
    using awaiter_return_type = decltype(get_awaiter(std::declval<T>()).await_resume());
}

// 简化版本的latch
class latch {
public:
    latch(std::uint64_t count) : m_count(count) {}
    
    void count_down() {
        if (m_count.fetch_sub(1) == 1) {
            // 简化：直接设置完成标志
            m_done = true;
        }
    }
    
    void wait() {
        while (!m_done) {
            // 忙等待
        }
    }
    
    std::atomic<std::uint64_t> m_count{0};
    std::atomic<bool> m_done{false};
};

namespace detail {
    template<typename T>
    struct result_wrapper {
        using type = T;
    };
    
    template<>
    struct result_wrapper<void> {
        using type = std::monostate;
    };
    
    template<typename T>
    using result_wrapper_t = typename result_wrapper<T>::type;
    
    template<typename Awaitable>
    using awaiter_return_type_t = concepts::awaiter_return_type<Awaitable>;
    
    template<typename Awaitable>
    using awaiter_type_t = decltype(concepts::get_awaiter(std::declval<Awaitable>()));
}

// when_all的awaiter实现
template<typename... Awaitables>
class when_all_awaiter {
private:
    using result_tuple_t = std::tuple<detail::result_wrapper_t<detail::awaiter_return_type_t<Awaitables>>...>;
    using awaiter_tuple_t = std::tuple<detail::awaiter_type_t<Awaitables>...>;
    
    awaiter_tuple_t awaiters;
    latch counter{sizeof...(Awaitables)};
    std::mutex exception_mutex;
    std::exception_ptr exception;
    std::coroutine_handle<> continuation;
    result_tuple_t results;

public:
    when_all_awaiter(Awaitables&&... awaitables) noexcept
        : awaiters(concepts::get_awaiter(std::forward<Awaitables>(awaitables))...)
    {}
    
    bool await_ready() const noexcept {
        return false; // 总是挂起
    }
    
    bool await_suspend(std::coroutine_handle<> h) noexcept {
        continuation = h;
        
        // 处理所有awaiter
        [this]<size_t... Is>(std::index_sequence<Is...>) {
            ((process_single_awaiter<Is>()), ...);
        }(std::index_sequence_for<Awaitables...>{});
        
        // 总是挂起
        return true;
    }
    
    template<size_t I>
    void process_single_awaiter() {
        auto& awaiter = std::get<I>(awaiters);
        
        if (awaiter.await_ready()) {
            // 立即处理
            handle_completion<I>();
        } else {
            // 挂起
            awaiter.await_suspend(continuation);
        }
    }
    
    template<size_t I>
    void handle_completion() {
        try {
            using awaiter_type = std::tuple_element_t<I, awaiter_tuple_t>;
            using return_type = decltype(std::declval<awaiter_type>().await_resume());
            
            if constexpr (!std::is_void_v<return_type>) {
                std::get<I>(results) = std::get<I>(awaiters).await_resume();
            } else {
                std::get<I>(results) = std::monostate{};
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock(exception_mutex);
            if (!exception) {
                exception = std::current_exception();
            }
        }
        
        counter.count_down();
        
        if (counter.m_count.load() == 0) {
            if (continuation) {
                continuation.resume();
            }
        }
    }
    
    result_tuple_t await_resume() {
        counter.wait();
        
        if (exception) {
            std::rethrow_exception(exception);
        }
        
        return std::move(results);
    }
};

// when_all函数
template<concepts::awaitable... Awaitables>
auto when_all(Awaitables&&... awaitables) noexcept
    -> when_all_awaiter<Awaitables...>
{
    return {std::forward<Awaitables>(awaitables)...};
}

// 测试协程
struct task {
    struct promise_type {
        task get_return_object() { return {}; }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };
};

task test_when_all() {
    auto awaiter1 = []() -> std::suspend_never { return {}; }();
    auto awaiter2 = []() -> std::suspend_never { return {}; }();
    
    co_await when_all(awaiter1, awaiter2);
    
    std::cout << "when_all test passed!" << std::endl;
}

int main() {
    std::cout << "Testing when_all implementation..." << std::endl;
    
    // 简单的编译测试
    test_when_all();
    
    return 0;
}