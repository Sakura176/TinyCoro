# when_all 使用示例

## 一、基本用法

### 1.1 等待多个任务完成
```cpp
#include "coro/coro.hpp"

using namespace coro;

task<int> compute_value(int x) {
    // 模拟耗时计算
    co_await std::suspend_always{};
    co_return x * 2;
}

task<std::string> fetch_data(int id) {
    // 模拟网络请求
    co_await std::suspend_always{};
    co_return "data_" + std::to_string(id);
}

task<> example_basic() {
    log::info("开始执行多个任务...");
    
    // 并发执行三个任务
    auto [value1, value2, data] = co_await when_all(
        compute_value(10),
        compute_value(20),
        fetch_data(100)
    );
    
    log::info("任务完成:");
    log::info("  value1 = {}", value1);  // 20
    log::info("  value2 = {}", value2);  // 40
    log::info("  data = {}", data);      // "data_100"
    
    co_return;
}
```

### 1.2 处理void返回类型
```cpp
task<void> log_message(const std::string& msg) {
    log::info("日志: {}", msg);
    co_return;
}

task<int> get_count() {
    co_return 42;
}

task<> example_void_types() {
    // 混合void和非void类型
    auto [_, count] = co_await when_all(
        log_message("任务开始"),
        get_count()
    );
    
    log::info("计数: {}", count);  // 42
    // _ 是 std::monostate，忽略即可
    
    co_return;
}
```

## 二、范围循环使用

### 2.1 遍历所有结果
```cpp
task<int> square(int x) {
    co_return x * x;
}

task<> example_range_loop() {
    // 计算1-5的平方
    auto results = co_await when_all(
        square(1),
        square(2),
        square(3),
        square(4),
        square(5)
    );
    
    log::info("平方结果:");
    for (auto& result : results) {
        log::info("  {}", result);
    }
    // 输出: 1, 4, 9, 16, 25
    
    co_return;
}
```

### 2.2 结果处理与转换
```cpp
task<> example_process_results() {
    auto results = co_await when_all(
        square(1),
        square(2),
        square(3)
    );
    
    int sum = 0;
    for (auto& result : results) {
        sum += result;
    }
    
    log::info("平方和: {}", sum);  // 1 + 4 + 9 = 14
    
    co_return;
}
```

## 三、异常处理

### 3.1 异常传播
```cpp
task<int> might_fail(bool should_fail) {
    if (should_fail) {
        throw std::runtime_error("任务失败!");
    }
    co_return 100;
}

task<> example_exception() {
    try {
        auto [result1, result2] = co_await when_all(
            might_fail(false),  // 正常完成
            might_fail(true)    // 抛出异常
        );
        
        // 不会执行到这里
        log::info("结果: {}, {}", result1, result2);
    }
    catch (const std::exception& e) {
        log::error("捕获异常: {}", e.what());  // "任务失败!"
    }
    
    co_return;
}
```

### 3.2 部分成功处理
```cpp
task<int> safe_compute(int x) {
    try {
        if (x < 0) {
            throw std::invalid_argument("负数无效");
        }
        co_return x * 2;
    }
    catch (...) {
        log::warn("计算失败，返回默认值");
        co_return -1;  // 返回错误码
    }
}

task<> example_partial_success() {
    auto [a, b, c] = co_await when_all(
        safe_compute(5),   // 成功: 10
        safe_compute(-3),  // 失败: -1
        safe_compute(8)    // 成功: 16
    );
    
    log::info("结果: {}, {}, {}", a, b, c);  // 10, -1, 16
    
    co_return;
}
```

## 四、嵌套与组合

### 4.1 嵌套when_all
```cpp
task<int> nested_task(int level, int value) {
    log::info("层级 {} 任务开始", level);
    co_await std::suspend_always{};
    co_return value * level;
}

task<> example_nested() {
    // 外层when_all包含两个内层when_all
    auto [outer1, outer2] = co_await when_all(
        []() -> task<std::tuple<int, int>> {
            auto [a, b] = co_await when_all(
                nested_task(1, 10),
                nested_task(1, 20)
            );
            co_return std::make_tuple(a, b);
        }(),
        
        []() -> task<std::tuple<int, int>> {
            auto [c, d] = co_await when_all(
                nested_task(2, 30),
                nested_task(2, 40)
            );
            co_return std::make_tuple(c, d);
        }()
    );
    
    auto [a, b] = outer1;
    auto [c, d] = outer2;
    
    log::info("嵌套结果: {}, {}, {}, {}", a, b, c, d);
    // 输出: 10, 20, 60, 80
    
    co_return;
}
```

### 4.2 与其他同步原语组合
```cpp
#include "coro/comp/mutex.hpp"

mutex g_mutex;
std::vector<int> g_results;

task<> worker(int id) {
    auto guard = co_await g_mutex.lock_guard();
    
    // 在锁保护下执行多个任务
    auto [a, b] = co_await when_all(
        []() -> task<int> { co_return id * 2; }(),
        []() -> task<int> { co_return id * 3; }()
    );
    
    g_results.push_back(a + b);
    
    co_return;
}

task<> example_with_mutex() {
    // 多个worker并发执行
    co_await when_all(
        worker(1),
        worker(2),
        worker(3)
    );
    
    log::info("最终结果:");
    for (auto& result : g_results) {
        log::info("  {}", result);
    }
    // 输出: 5 (1*2 + 1*3), 10, 15
    
    co_return;
}
```

## 五、实际应用场景

### 5.1 并行数据加载
```cpp
struct UserData {
    std::string name;
    std::vector<int> scores;
    std::map<std::string, std::string> metadata;
};

task<UserData> load_user_profile(int user_id) {
    log::info("加载用户 {} 的资料", user_id);
    
    auto [name, scores, meta] = co_await when_all(
        load_user_name(user_id),
        load_user_scores(user_id),
        load_user_metadata(user_id)
    );
    
    co_return UserData{name, scores, meta};
}

task<> example_data_loading() {
    // 并行加载多个用户的资料
    auto [user1, user2] = co_await when_all(
        load_user_profile(1001),
        load_user_profile(1002)
    );
    
    log::info("用户1: {}", user1.name);
    log::info("用户2: {}", user2.name);
    
    co_return;
}
```

### 5.2 批量API调用
```cpp
task<std::string> call_api(const std::string& endpoint, const std::string& params) {
    // 模拟API调用
    co_await std::suspend_always{};
    co_return "响应: " + endpoint + "?" + params;
}

task<> example_batch_api() {
    // 并发调用多个API
    auto [user_api, product_api, order_api] = co_await when_all(
        call_api("/api/users", "id=123"),
        call_api("/api/products", "category=electronics"),
        call_api("/api/orders", "status=pending")
    );
    
    log::info("API响应:");
    log::info("  用户API: {}", user_api);
    log::info("  产品API: {}", product_api);
    log::info("  订单API: {}", order_api);
    
    co_return;
}
```

### 5.3 并行计算
```cpp
task<std::vector<double>> process_chunk(const std::vector<double>& data, int chunk_id) {
    log::info("处理数据块 {}", chunk_id);
    
    std::vector<double> result;
    result.reserve(data.size());
    
    for (double value : data) {
        // 模拟耗时计算
        result.push_back(std::sqrt(value) * std::log(value + 1.0));
    }
    
    co_return result;
}

task<> example_parallel_compute() {
    std::vector<std::vector<double>> all_data = {
        {1.0, 2.0, 3.0},
        {4.0, 5.0, 6.0},
        {7.0, 8.0, 9.0}
    };
    
    // 并行处理所有数据块
    auto [chunk1, chunk2, chunk3] = co_await when_all(
        process_chunk(all_data[0], 1),
        process_chunk(all_data[1], 2),
        process_chunk(all_data[2], 3)
    );
    
    // 合并结果
    std::vector<double> final_result;
    final_result.insert(final_result.end(), chunk1.begin(), chunk1.end());
    final_result.insert(final_result.end(), chunk2.begin(), chunk2.end());
    final_result.insert(final_result.end(), chunk3.begin(), chunk3.end());
    
    log::info("处理完成，结果大小: {}", final_result.size());
    
    co_return;
}
```

## 六、性能优化技巧

### 6.1 避免不必要的拷贝
```cpp
task<std::vector<int>> generate_large_data() {
    std::vector<int> data(1000000);
    std::iota(data.begin(), data.end(), 0);
    co_return std::move(data);  // 使用移动语义
}

task<> example_move_semantics() {
    // 使用移动语义避免拷贝
    auto [data1, data2] = co_await when_all(
        generate_large_data(),
        generate_large_data()
    );
    
    log::info("数据大小: {}, {}", data1.size(), data2.size());
    
    co_return;
}
```

### 6.2 限制并发数量
```cpp
template<typename... Tasks>
task<std::vector<std::any>> limited_when_all(size_t max_concurrent, Tasks&&... tasks) {
    // 实现有限并发的when_all
    // 将任务分批执行，每批最多max_concurrent个
    // ...
    co_return results;
}

task<> example_limited_concurrency() {
    std::vector<task<int>> tasks;
    for (int i = 0; i < 100; ++i) {
        tasks.push_back(square(i));
    }
    
    // 限制最多10个并发任务
    auto results = co_await limited_when_all(10, std::move(tasks)...);
    
    co_return;
}
```

## 七、调试技巧

### 7.1 添加调试日志
```cpp
template<typename... Awaitables>
auto debug_when_all(const std::string& context, Awaitables&&... awaitables) {
    log::debug("[when_all] {}: 启动 {} 个任务", context, sizeof...(Awaitables));
    
    auto start_time = std::chrono::steady_clock::now();
    
    return when_all(std::forward<Awaitables>(awaitables)...)
        .then([context, start_time](auto&& results) {
            auto duration = std::chrono::steady_clock::now() - start_time;
            log::debug("[when_all] {}: 完成，耗时 {}ms", 
                      context, 
                      std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
            return std::forward<decltype(results)>(results);
        });
}

task<> example_debugging() {
    auto results = co_await debug_when_all(
        "示例任务",
        square(1),
        square(2),
        square(3)
    );
    
    co_return;
}
```

## 八、常见问题与解决方案

### 8.1 任务数量动态确定
```cpp
task<> example_dynamic_tasks() {
    std::vector<task<int>> tasks;
    
    // 动态创建任务
    for (int i = 0; i < 5; ++i) {
        tasks.push_back(square(i));
    }
    
    // 使用折叠表达式展开vector中的任务
    auto results = std::apply([](auto&&... ts) {
        return when_all(std::forward<decltype(ts)>(ts)...);
    }, std::tuple_cat(std::make_tuple(std::move(tasks)...)));
    
    // 注意：这需要一些模板技巧来正确处理
    
    co_return;
}
```

### 8.2 超时处理
```cpp
template<typename Duration, typename... Awaitables>
auto when_all_with_timeout(Duration timeout, Awaitables&&... awaitables) {
    // 实现带超时的when_all
    // 使用when_all组合原始任务和超时任务
    // 超时时取消未完成的任务
    // ...
}

task<> example_timeout() {
    try {
        auto results = co_await when_all_with_timeout(
            std::chrono::seconds(5),
            long_running_task(),
            another_task()
        );
        
        log::info("任务在超时前完成");
    }
    catch (const std::runtime_error& e) {
        log::error("任务超时: {}", e.what());
    }
    
    co_return;
}
```

---

## 总结

`when_all` 是一个强大的协程同步原语，适用于：

1. **并行执行**：多个独立任务的并发执行
2. **结果收集**：统一收集所有任务的结果
3. **资源加载**：并行加载多个资源
4. **批量处理**：批量API调用或数据处理
5. **复杂工作流**：构建复杂的异步工作流

使用时的注意事项：
- 合理控制并发数量，避免资源耗尽
- 正确处理异常，确保程序健壮性
- 使用移动语义优化性能
- 添加适当的日志和监控

通过合理使用 `when_all`，可以显著提高异步程序的性能和可读性。