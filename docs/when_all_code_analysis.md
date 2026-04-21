# `when_all.hpp` 实现指引文档

## 1. 概述与架构设计

### 1.1 功能定位
本文件实现了 C++20 协程环境下的 `when_all` 语义：并发地启动一组协程任务，并在**所有任务均执行完毕后**恢复当前协程的执行。
与标准库或其它实现不同，此实现高度定制化，通过**预分配内存**和**无锁倒计数**实现了极低开销的任务汇聚。

### 1.2 核心设计思想
*   **惰性启动**：任务在包装时被创建，但立刻挂起（`initial_suspend` 返回 `suspend_always`），只有在调用 `co_await when_all(...)` 时才被统一提交给调度器。
*   **零拷贝结果收集**：对于非 `void` 返回类型的任务，返回值存放的内存由外层 `when_all` 容器（`std::array` 或 `std::vector`）预先分配，任务 Promise 内部仅持有一个指针指向该内存，避免了二次分配。
*   **类型分派优化**：通过 C++20 Concepts 强约束，将全 `void` 返回、全相同 POD 返回、以及范围式返回进行模板特化分离，杜绝了复杂的 `std::variant` 开销。

---

## 2. 前置依赖解析

在深入实现逻辑前，需明确代码依赖的外部组件（假设位于同项目的其它模块中）：

| 依赖组件 | 职责假设 | 在本文件中的使用方式 |
| :--- | :--- | :--- |
| `coro/comp/latch.hpp` | 提供一个轻量级的线程安全/协程安全的倒计数同步原语。 | 初始化计数值为任务数量；任务结束时调用 `count_down()`；调用者通过 `wait()` 获取 awaiter 阻塞等待。 |
| `coro/concepts/awaitable.hpp` | 提供概念约束：`awaitable`、`awaitable_traits`、`conventional_type`、`all_void_type` 等。 | 用于在编译期萃取任务的返回值类型 `rt`，并约束模板特化路径。 |
| `coro/scheduler.hpp` | 提供协程调度器及 `submit_to_scheduler(handle)` 接口。 | 在任务真正开始时，将协程句柄抛入调度队列。 |

---

## 3. 逻辑拆分与实现步骤

整个实现可按**自底向上**的顺序拆分为 6 个核心步骤：

### 步骤 1：实现底层任务骨架 (`when_all_task_promise_base`)
**目的**：定义所有 `when_all` 内部任务共用的生命周期行为。
**实现细节**：
1.  **构造与挂起**：`initial_suspend` 必须返回 `std::suspend_always`，确保 `make_when_all_task` 协程创建后立刻暂停，将控制权交还给调用方。
2.  **结束与通知**：`final_suspend` 返回自定义的 `completion_notifier`。在其 `await_suspend` 中调用 `m_latch->count_down()`。这是整个机制的核心：任务执行完的最后一步，自动递减计数器。
3.  **异常处理**：`unhandled_exception` 留空（注释标明 Keep simple）。*注意：实际生产中应考虑捕获异常并在 `when_all` 层面抛出。*
4.  **状态注入**：提供 `start(latch& l)` 方法，供外部在启动前注入倒计数器的指针。

### 步骤 2：实现返回值分派
**目的**：处理 `void` 和非 `void` 任务的返回值存储差异。
**实现细节**：
1.  **非 void 特化 (`when_all_task_promise<T>`)**：
    *   增加成员 `storage_type m_data`（实质为 `T*` 指针）。
    *   提供 `set_pointer(T* ptr)` 接口。
    *   `return_value(T value)` 中执行 `*m_data = std::move(value);`。**关键点：不发生内存分配，直接写入外部预分配的地址。**
2.  **void 特化 (`when_all_task_promise<void>`)**：
    *   无需指针成员。
    *   仅实现 `return_void()`。

### 步骤 3：封装协程句柄 (`when_all_task`)
**目的**：提供 RAII 管理，并封装“启动”动作。
**实现细节**：
1.  持有 `std::coroutine_handle<promise_type>`。
2.  析构函数中检查句柄有效性并调用 `destroy()`，防止协程泄漏。
3.  实现 `start(latch& l, storage_type p)`：
    *   若非 `void`，先调用 `promise().set_pointer(p)` 绑定结果内存。
    *   调用 `promise().start(l)` 绑定计数器。
    *   调用 `submit_to_scheduler(m_handle)` 真正将任务投入执行池。

### 步骤 4：实现类型擦除与包装工厂 (`make_when_all_task`)
**目的**：将用户传入的任意 `awaitable`（如 `task<T>`、`generator` 等）统一转化为 `when_all_task<T>`。
**实现细节**：
1.  这是一个模板协程。
2.  内部逻辑极简：`co_return co_await std::forward<awaitable>(a);`
3.  因为 Step 1 中设定了 `suspend_always`，执行到 `co_await` 前就会返回，这里的代码实际上是在任务被调度器唤醒后才执行的。

### 步骤 5：实现定长容器 Awaitable (Tuple 版本)
**目的**：处理编译期确定数量的任务汇聚（对应变参模板 `when_all`）。
**实现细节**：
1.  **空元组特化**：直接返回 `std::tuple{}`，`await_ready` 返回 `true`，无任何阻塞。
2.  **全 void 特化**：继承 `when_all_ready_awaitable_base`。在 `operator co_await` 中，使用折叠表达式 `(tasks.start(latch), ...)` 启动所有任务，返回 `latch.wait()` 的 awaiter。
3.  **全相同 POD 特化 (解决 CWG #1430 缺陷)**：
    *   成员变量增加 `storage_type m_data`（即 `std::array<T, N>`）。
    *   `operator co_await` 返回自定义的 `awaiter` 结构体。该结构体组合了 `latch.wait()` 的 awaiter 和对 `m_data` 的引用。
    *   启动时，利用折叠表达式和一个递增的 `index`，将 `m_data[i]` 的地址作为第二个参数传给 `tasks.start(latch, &m_data[i])`。
    *   恢复时 (`await_resume`)，将 `m_data` 移动返回。

### 步骤 6：实现动态容器 Awaitable (Range 版本)
**目的**：处理运行时确定数量的任务汇聚（如传入 `std::vector<task<T>>`）。
**实现细节**：
1.  **非 void 特化**：类似 Step 5 的全相同 POD，但存储容器换成了 `std::vector<T>`。在 `operator co_await` 时根据 `size()` 预先 `resize` vector，通过循环 `for (auto& t : tasks)` 启动并传递指针。
2.  **void 特化**：通过 `requires(std::is_void_v<...>)` 约束。无需分配结果内存，直接循环调用 `tasks.start(latch)`，返回底层的 `latch.wait()` awaiter。

### 步骤 7：组装公共 API (`when_all` 函数重载)
**目的**：提供简洁的用户接口，隐藏内部复杂的模板推导。
**实现细节**：
1.  **变参版本**：接收 `awaitables_type...`。利用 `std::make_tuple` 结合 `make_when_all_task`，推导出 `std::tuple<when_all_task<T1>, when_all_task<T2>...>`，传入对应的 Awaitable 类。
2.  **Range 版本**：接收 `range_type&&`。推导值类型，构建 `std::vector<when_all_task<T>>`。若原 Range 支持 `sized_range`，则调用 `reserve` 优化内存分配。遍历原 range，`emplace_back` 新任务，最后将 vector 移动传入 Range Awaitable。

---

## 4. 关键执行时序图

以 `auto [r1, r2] = co_await when_all(task1(), task2());` 为例：

```text
[Caller Coroutine]
       |
       |----> 1. 调用 when_all(task1, task2)
       |         -> 创建 task1_handle (挂起于 initial_suspend)
       |         -> 创建 task2_handle (挂起于 initial_suspend)
       |         -> 返回 when_all_ready_awaitable (内部含 array<ret, 2> 和 latch(2))
       |
       |----> 2. 执行 co_await (触发 operator co_await)
       |         -> task1.start(latch, &array[0])
       |              -> 绑定指针
       |              -> latch 计数器仍为 2
       |              -> submit_to_scheduler(task1_handle)  ------+
       |                                                         | (异步并发执行)
       |         -> task2.start(latch, &array[1])                v
       |              -> submit_to_scheduler(task2_handle)  [Scheduler 线程池]
       |                                                       task1 执行完毕
       |         -> latch.wait() 返回 awaiter                   -> 写入 array[0]
       |              -> Caller 挂起于 latch.wait()              -> final_suspend
                                                                -> latch.count_down() (变1)
                                                               task2 执行完毕
                                                                -> 写入 array[1]
                                                                -> final_suspend
                                                                -> latch.count_down() (变0)
                                                                         |
       |<----------------------------------------------------------------+
       |
       |----> 3. latch.wait() 满足条件，恢复 Caller
       |         -> await_resume 触发
       |         -> std::move(array) 返回
       |
       |----> 4. 结构化绑定解包 r1, r2
```

---

## 5. 代码审查与潜在改进点

虽然代码设计精巧，但在实际工程落地前需注意以下几点：

1.  **异常安全致命缺陷**：
    `unhandled_exception()` 为空。如果某个子任务抛出异常，协程会在此终止并携带异常，但 `final_suspend` 仍会执行 `count_down`。最终 `when_all` 会在另一个任务正常结束时误以为所有任务成功，导致未定义行为或静默错误。
    *改进建议*：在 Promise 中增加 `std::exception_ptr`，在 `return_value`/`return_void` 时记录成功状态，`unhandled_exception` 时记录异常。在 `await_resume` 时检查是否有异常，若有则重新抛出（可包装在 `when_all_error` 中）。
2.  **CWG #1430 的局限性**：
    当前代码强制要求非 void 情况下，所有任务返回值必须是**完全相同的 POD 类型**。这意味着无法执行 `when_all(task_returning_int(), task_returning_string())`。
    *改进建议*：如果需要支持异构返回类型，必须放弃 `std::array`，转而使用 `std::tuple<std::optional<T1>, std::optional<T2>...>`，但这会显著增加模板元编程的复杂度和运行期开销。当前设计（强制同类型）在性能上是更优的选择。
3.  **生命周期风险**：
    `when_all_ready_awaitable` 内部的 `m_data` 是局部成员。在非 void 的 `awaiter` 中，通过引用捕获了 `m_data`。必须确保 `awaiter` 的生命周期不长于 `when_all_ready_awaitable` 对象本身。当前由于 `co_await` 表达式的临时值生命周期被延长至表达式结束，因而是安全的，但在重构时需格外小心。
4.  **移动语义缺失**：
    `when_all_task` 禁用了拷贝和移动。这意味着包含任务的 `std::vector` 或 `std::tuple` 本身也是不可移动的（只能作为右值被完美转发一次）。这在目前的设计下是合理的，因为移动协程句柄容易引发悬垂指针，但限制了某些高级用法的灵活性。
