# `channel.hpp` 代码审查报告

**审查日期**: 2026-05-29
**审查文件**: `include/coro/comp/channel.hpp`
**审查范围**: 未提交的工作区改动（`git diff HEAD -- include/coro/comp/channel.hpp`）

---

## 1. 总体评估

| 维度 | 评级 | 说明 |
| :--- | :--- | :--- |
| 正确性 | ❌ 阻塞性故障 | 3 个关键 bug 导致死锁；测试全部挂起 |
| 线程安全 | ❌ 缺陷 | lock_guard 返回值未捕获，临界区无保护 |
| 代码结构 | ⚠ 合理 | 类成员设计、整体思路正确 |

**测试结果**: 25/25 测试挂起（死锁），首个测试 `ChannelStringTest.StringChannelProducerConsumer` 即超时，未能执行到断言。

---

## 2. 关键缺陷

### Bug 1 (致命) — `lock_guard` 返回值未被捕获，锁立即释放

**位置**: `channel.hpp:68`, `channel.hpp:87`

**代码**:
```cpp
co_await m_mtx.lock_guard();  // ❌ 返回值（lock_guard 对象）被丢弃
```

**根因分析**:

`co_await m_mtx.lock_guard()` 的执行流程：
1. 创建 `guard_awaiter` → `await_ready()` 返回 `false`（mutex 实现总是先挂起）
2. `await_suspend()` → `register_waiter()` → 若 mutex 空闲则 CAS 获取锁，返回 `false`（不挂起）
3. `await_resume()` → 返回 `lock_guard<mutex>` **临时对象**
4. 临时对象在表达式末尾析构 → `~lock_guard()` 调用 `m_mtx.unlock()`

**结果**：锁在第 68 行获取，同一行释放。后续所有 `send()`/`recv()` 代码在**无锁保护**的情况下执行。

**修复**:
```cpp
// ✓ 捕获 lock_guard 返回值，延长其生命周期
auto lock = co_await m_mtx.lock_guard();
// lock 在此作用域结束时析构，自动释放 mutex
```

---

### Bug 2 (致命) — `send()` 等待条件错误

**位置**: `channel.hpp:69`

**代码**:
```cpp
while (m_count == 0 && !m_closed)  // ❌ 缓冲区空时等待
```

**根因分析**:

| 条件 | 当前行为 | 正确行为 |
| :--- | :--- | :--- |
| `m_count == 0`（缓冲区空） | 发送者**挂起** | 发送者**应写入** |
| `m_count == capacity`（缓冲区满） | 发送者**不挂起**（覆盖数据！） | 发送者**应挂起** |

`m_count == 0` 表示缓冲区为空——这是**接收者**的等待条件，不是发送者的。发送者应当等待缓冲区有空位，即 `m_count >= capacity`。

**影响**: 
- capacity=1：死锁（初始 count=0，发送者挂起，接收者也挂起）
- capacity>1：缓冲区空时死锁，缓冲区满时数据被覆盖

**修复**:
```cpp
while (m_count >= capacity && !m_closed)  // ✓ 缓冲区满时才等待
```

---

### Bug 3 (致命) — `recv()` 等待条件错误

**位置**: `channel.hpp:88`

**代码**:
```cpp
while (m_count >= capacity && !m_closed)  // ❌ 缓冲区满时等待
```

**根因分析**:

| 条件 | 当前行为 | 正确行为 |
| :--- | :--- | :--- |
| `m_count == 0`（缓冲区空） | 接收者**不等待**（读垃圾数据） | 接收者**应挂起** |
| `m_count == capacity`（缓冲区满） | 接收者**挂起** | 接收者**应读取** |

`m_count >= capacity` 是发送者的等待条件，不是接收者的。接收者应当等待缓冲区有数据，即 `m_count == 0`。

**影响**: 
- 接收者在缓冲区为空时读取未初始化数据
- `m_count--` 从 0 下溢为 `SIZE_MAX`，后续循环行为完全错乱
- 最终双方死锁

**修复**:
```cpp
while (m_count == 0 && !m_closed)  // ✓ 缓冲区空时才等待
```

---

### Bug 4 (严重) — `close()` 未获取锁

**位置**: `channel.hpp:104`

**代码**:
```cpp
auto close() noexcept -> void
{
    m_mtx.lock_guard();  // ❌ 没有 co_await，锁机制不触发
    m_closed = true;
    ...
}
```

**根因分析**:

`close()` 不是协程（无 `co_await`）。`m_mtx.lock_guard()` 返回 `guard_awaiter` 临时对象，但没有 `co_await` 就不会调用 `await_ready()` / `await_suspend()` / `await_resume()`——锁永远不会被获取。`m_closed = true` 的写入与 `send()`/`recv()` 中对 `m_closed` 的读取形成数据竞争（C++ 标准下为未定义行为）。

**修复方案**（协作式调度器不需要锁）:

```cpp
auto close() noexcept -> void
{
    // 协作式调度器中，两次 co_await 之间其他协程无法打断当前执行流
    // m_closed 的写入天然是"原子"的，无需持锁
    m_closed = true;
    m_send_cv.notify_all();
    m_recv_cv.notify_all();
}
```

> **关于 try_lock 自旋**：在协作式调度器中 `while (!m_mtx.try_lock()) {}` 会**死锁**——自旋不让出执行权，持有锁的协程无法运行释放锁。`co_await` + `lock_guard` 也不可行，因为 `close()` 返回 `void` 而非协程类型，且调用方以同步方式调用。正确做法是利用协作式调度器的特性：`m_closed` 赋值在两次 `co_await` 之间完成，天然互斥。

---

### Bug 5 (中等) — `recv()` 关闭判断使用 `m_head == m_tail` 而非 `m_count == 0`

**位置**: `channel.hpp:90`

**代码**:
```cpp
if (m_head == m_tail && m_closed)  // ❌ 对 capacity>1 存在歧义
```

**根因分析**: 环形缓冲区中 `head == tail` 同时代表"空"和"满"两种状态。当 capacity>1 时：
- 空缓冲区：head==tail, count==0
- 满缓冲区：head==tail, count==capacity

如果缓冲区满时 channel 被关闭，`m_head == m_tail` 为 true，接收者会错误地返回 `nullopt` 而**丢失缓冲区中的全部数据**。

**修复**:
```cpp
if (m_count == 0 && m_closed)  // ✓ 只有确实为空时才返回 nullopt
```

---

### Bug 6 (中等) — `recv()` 缺少 `std::move`

**位置**: `channel.hpp:95`, `channel.hpp:99`

**代码**:
```cpp
auto val = m_buffer[m_head];                  // ❌ 拷贝
co_return std::optional<T>(val);              // ❌ 再拷贝（共 2 次拷贝）
```

**修复**:
```cpp
auto val = std::move(m_buffer[m_head]);                        // ✓ 移动
co_return std::optional<T>(std::move(val));                    // ✓ 移动
```

对 `std::string` 等堆分配类型，拷贝是 O(n) 操作，移动是 O(1)。

---

## 3. 性能建议

### 建议 1 — `notify_one()` 移至锁外

**位置**: `channel.hpp:81`, `channel.hpp:98`

**当前**: `notify_one()` 在持有 `m_mtx` 时调用。

**问题**: 被唤醒的协程立即尝试获取 mutex，但 mutex 仍被通知者持有 → 获取失败 → 再次挂起到 mutex 等待队列 → 多余的上下文切换（"Hurry Up and Wait"）。

**建议**:
```cpp
{
    auto lock = co_await m_mtx.lock_guard();
    // ... 临界区操作 ...
}  // lock 析构，释放 mutex
m_recv_cv.notify_one();  // 被唤醒者可以直接获取 mutex
co_return true;
```

> 这是性能优化而非正确性修复，临界区内 notify 也是正确的。

---

## 4. 缺陷对照总表

| # | 严重度 | 位置 | 问题 | 影响 |
| :--- | :--- | :--- | :--- | :--- |
| 1 | 🔴 致命 | :68, :87 | lock_guard 返回值未捕获 | 临界区完全无保护 |
| 2 | 🔴 致命 | :69 | send 等待条件 `m_count == 0` 应为 `m_count >= capacity` | 死锁 + 数据覆盖 |
| 3 | 🔴 致命 | :88 | recv 等待条件 `m_count >= capacity` 应为 `m_count == 0` | 读未初始化数据 + 死锁 |
| 4 | 🟠 严重 | :104 | close() 无 co_await，锁不生效 | data race |
| 5 | 🟡 中等 | :90 | recv 关闭判断用 `head==tail` 而非 `count==0` | capacity>1 时数据丢失 |
| 6 | 🟡 中等 | :95, :99 | recv 缺少 std::move | 不必要的深拷贝 |

---

## 5. 修复后的完整参考代码

```cpp
template<concepts::conventional_type T, size_t capacity = 1>
class channel
{
    using data_type = std::optional<T>;

    mutex                   m_mtx;
    condition_variable      m_send_cv;
    condition_variable      m_recv_cv;
    std::array<T, capacity> m_buffer;
    size_t                  m_head{0};
    size_t                  m_tail{0};
    size_t                  m_count{0};
    bool                    m_closed{false};

public:
    template<typename value_type>
        requires(std::is_constructible_v<T, value_type &&>)
    auto send(value_type&& value) noexcept -> task<bool>
    {
        auto lock = co_await m_mtx.lock_guard();   // Bug 1: 捕获返回值

        while (m_count >= capacity && !m_closed)    // Bug 2: 修复等待条件
        {
            co_await m_send_cv.wait(m_mtx);
        }
        if (m_closed)
        {
            m_send_cv.notify_all();
            co_return false;
        }
        m_buffer[m_tail] = std::move(value);
        m_tail = (m_tail + 1) % capacity;
        m_count++;
    }  // 建议 1: notify 放锁外
    m_recv_cv.notify_one();
    co_return true;
}

auto recv() noexcept -> task<data_type>
{
    data_type result;
    {
        auto lock = co_await m_mtx.lock_guard();   // Bug 1: 捕获返回值

        while (m_count == 0 && !m_closed)           // Bug 3: 修复等待条件
        {
            co_await m_recv_cv.wait(m_mtx);
        }
        if (m_count == 0 && m_closed)               // Bug 5: 用 m_count 判断
        {
            m_recv_cv.notify_all();
            co_return std::nullopt;
        }
        result = std::optional<T>(std::move(m_buffer[m_head])); // Bug 6: move
        m_head = (m_head + 1) % capacity;
        m_count--;
    }
    m_send_cv.notify_one();
    co_return result;
}

auto close() noexcept -> void
{
    // 协作式调度器：无需持锁（两次 co_await 之间天然互斥）
    m_closed = true;
    m_send_cv.notify_all();
    m_recv_cv.notify_all();
}
};
```

---

## 6. 根因总结

三个最严重的问题是连锁反应：

1. **Bug 2 + Bug 3（条件互换）** 是核心逻辑错误——`send()` 和 `recv()` 的等待条件写反了。这通常是因为混淆了"谁等谁"的对应关系：**发送者等"有空位"（count < capacity），接收者等"有数据"（count > 0）**。当前代码恰好颠倒。

2. **Bug 1（lock_guard 未捕获）** 使得 Bug 2/3 在单线程协程下不会立即暴露为数据竞争——因为协程在 `co_await` 处才切换，而 `lock_guard` 的析构发生在 `co_await` 之前。但这意味着**所有共享状态的访问都没有互斥保护**，一旦引入多线程调度将出现未定义行为。

3. **Bug 4（close 无锁）** 是协程 API 设计理解不足——`lock_guard()` 返回的是 awaiter，不是 RAII 守卫本身。真正的守卫需要通过 `co_await` 获取。
