# when_all 实现要点快速参考

## 一、核心设计思想

### 1.1 基本概念
- **目的**：并发执行多个异步任务，等待所有完成
- **输入**：一组awaitable对象
- **输出**：包含所有结果的std::tuple
- **特性**：类型安全、异常安全、线程安全

### 1.2 关键设计决策
1. **统一结果类型**：void → std::monostate
2. **并发控制**：latch（倒计数器）
3. **异常处理**：捕获第一个异常
4. **迭代支持**：为tuple实现begin/end

## 二、核心数据结构

### 2.1 类型转换系统
```cpp
// void类型包装器
template<typename T> struct result_wrapper { using type = T; };
template<> struct result_wrapper<void> { using type = std::monostate; };

// 类型推导
template<typename Awaitable>
using awaiter_return_type_t = 
    typename concepts::awaitable_traits<Awaitable>::awaiter_return_type;
```

### 2.2 when_all_awaiter 类模板
```cpp
template<typename... Awaitables>
class when_all_awaiter {
private:
    using result_tuple_t = std::tuple<
        detail::result_wrapper_t<detail::awaiter_return_type_t<Awaitables>>...
    >;
    using awaiter_tuple_t = std::tuple<detail::awaiter_type_t<Awaitables>...>;
    
    // 成员变量
    awaiter_tuple_t awaiters;          // 存储所有awaiter
    latch counter{sizeof...(Awaitables)}; // 任务计数器
    std::mutex exception_mutex;        // 异常保护锁
    std::exception_ptr exception;      // 存储的异常
    result_tuple_t results;            // 结果集合
};
```

## 三、实现流程

### 3.1 构造阶段
```cpp
when_all_awaiter(Awaitables&&... awaitables) noexcept
    : awaiters(concepts::get_awaiter(std::forward<Awaitables>(awaitables))...)
{}
```

### 3.2 Awaiter三件套

#### 3.2.1 await_ready()
```cpp
bool await_ready() const noexcept {
    // 检查所有awaiter是否都已就绪
    bool all_ready = true;
    std::apply([&](const auto&... aws) {
        ((aws.await_ready() ? void() : (all_ready = false)), ...);
    }, awaiters);
    return all_ready;
}
```

#### 3.2.2 await_suspend()
```cpp
bool await_suspend(std::coroutine_handle<> h) noexcept {
    // 启动所有awaiter
    start_all_awaiters(std::index_sequence_for<Awaitables...>{});
    return true;  // 总是挂起，在await_resume中等待
}
```

#### 3.2.3 await_resume()
```cpp
result_tuple_t await_resume() {
    counter.wait();  // 等待所有任务完成
    
    if (exception) {
        std::rethrow_exception(exception);
    }
    
    return std::move(results);
}
```

### 3.3 单个任务处理
```cpp
template<size_t I>
void handle_single_completion() noexcept {
    try {
        using return_type = decltype(std::get<I>(awaiters).await_resume());
        if constexpr (!std::is_void_v<return_type>) {
            std::get<I>(results) = std::get<I>(awaiters).await_resume();
        } else {
            std::get<I>(results) = std::monostate{};
        }
    } catch (...) {
        std::lock_guard<std::mutex> lock(exception_mutex);
        if (!exception) exception = std::current_exception();
    }
    counter.count_down();
}
```

## 四、迭代器支持

### 4.1 tuple_iterator 实现
```cpp
template<typename Tuple>
class tuple_iterator {
    const Tuple& tuple;
    size_t index;
    
    template<size_t CurrentIndex>
    static auto get_element(const Tuple& t, size_t target_index) -> decltype(auto) {
        if constexpr (CurrentIndex < std::tuple_size_v<Tuple>) {
            return target_index == CurrentIndex 
                ? std::get<CurrentIndex>(t)
                : get_element<CurrentIndex + 1>(t, target_index);
        }
        throw std::out_of_range("tuple index out of range");
    }
    
public:
    auto operator*() const -> decltype(auto) {
        return get_element<0>(tuple, index);
    }
    // ... 其他迭代器操作
};
```

### 4.2 begin/end 函数
```cpp
template<typename... Args>
auto begin(const std::tuple<Args...>& tuple) {
    return detail::tuple_iterator<std::tuple<Args...>>(tuple, 0);
}

template<typename... Args>
auto end(const std::tuple<Args...>& tuple) {
    return detail::tuple_iterator<std::tuple<Args...>>(tuple, sizeof...(Args));
}
```

## 五、使用示例

### 5.1 基本用法
```cpp
auto [result1, result2, result3] = co_await when_all(
    async_task1(),
    async_task2(),
    async_task3()
);
```

### 5.2 范围循环
```cpp
auto results = co_await when_all(task1(), task2(), task3());
for (auto& result : results) {
    process(result);
}
```

### 5.3 混合类型
```cpp
auto [value, _] = co_await when_all(
    compute_value(),      // 返回int
    log_completion()      // 返回void
);
```

## 六、面试要点总结

### 6.1 设计模式
- **模板方法模式**：await_ready/suspend/resume
- **观察者模式**：任务完成通知
- **RAII模式**：资源自动管理

### 6.2 关键技术
1. **变参模板**：支持任意数量参数
2. **完美转发**：避免不必要的拷贝
3. **SFINAE/概念**：类型约束检查
4. **编译时递归**：元编程技巧
5. **内存序**：正确的并发语义

### 6.3 性能优化
- **零动态分配**：使用栈上存储
- **最小化锁**：只在必要时同步
- **编译时计算**：减少运行时开销
- **内联优化**：关键路径函数

### 6.4 错误处理
- **异常安全**：保证资源不泄漏
- **第一个异常**：只传播第一个异常
- **线程安全**：正确处理并发访问

## 七、常见问题

### Q1: 为什么需要result_wrapper？
A: std::tuple不能包含void类型，需要std::monostate作为占位符。

### Q2: latch和condition_variable的区别？
A: latch更轻量，专为一次性同步设计，避免虚假唤醒。

### Q3: 如何处理任务取消？
A: 需要扩展设计，添加取消标志和传播机制。

### Q4: 内存泄漏风险？
A: 使用RAII管理所有资源，确保异常安全。

### Q5: 与std::async的对比？
A: 协程更轻量，支持结构化并发，避免回调地狱。

## 八、扩展方向

### 8.1 功能扩展
- when_all_ready：不等待完成
- when_any：等待任意一个完成
- 超时支持：添加超时机制
- 进度回调：任务完成通知

### 8.2 性能优化
- 无锁实现：消除锁竞争
- 批量操作：优化大量任务
- 缓存优化：改善内存访问模式

### 8.3 生态集成
- 协程调度器集成
- 网络库适配
- 并行算法支持

---

## 快速检查清单

✅ 类型安全：编译时类型检查  
✅ 异常安全：资源不泄漏  
✅ 线程安全：正确同步  
✅ 零开销：最小运行时开销  
✅ 易用性：简洁的API设计  
✅ 扩展性：支持功能扩展  

*最后更新：2024年*