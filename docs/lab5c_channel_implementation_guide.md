# `channel.hpp` 实现指引文档

## 1. 概述与架构设计

### 1.1 功能定位
本文件实现 C++20 协程环境下的 **Channel（通道）** 语义——Go 语言风格的多生产者/多消费者（MPMC）消息传递原语。协程通过 `co_await ch.send(val)` 向通道写入数据，通过 `co_await ch.recv()` 从通道读取数据。Channel 在满时阻塞发送者，在空时阻塞接收者，关闭后不再接受新数据。

### 1.2 核心特性

| 特性 | 无缓冲 channel `channel<T, 1>` | 有缓冲 channel `channel<T, N>` (N>1) |
| :--- | :--- | :--- |
| 容量 | 0 缓冲位（handoff 语义） | N-1 个缓冲槽位 |
| 发送阻塞条件 | 没有等待中的接收者 | 缓冲区已满 |
| 接收阻塞条件 | 没有等待中的发送者 | 缓冲区为空 |
| 关闭行为 | `send()` 返回 `false`，`recv()` 排空后返回 `nullopt` | 同左 |

### 1.3 核心设计思想
*   **复用已有组件**：使用 `condition_variable` + `mutex` 作为内部同步基础，而非重新实现等待队列。Channel 本身就是 "生产者-消费者" 的泛化，lab5b 中正是用 `condition_variable` 实现了生产者-消费者模型。
*   **模板参数化容量**：`channel<T, capacity>` 将 buffer 大小作为编译期常量，capacity=1 即为无缓冲 channel（buffer 仅作为 handoff 的中间变量），capacity>1 即为有缓冲 channel。
*   **协程友好**：`send()` 和 `recv()` 均返回协程 `task<bool>` / `task<optional<T>>`，阻塞时的挂起和唤醒完全基于调度器的协程恢复机制，不阻塞线程。

---

## 2. 前置依赖

| 依赖组件 | 路径 | 用途 |
| :--- | :--- | :--- |
| `condition_variable` | `coro/comp/condition_variable.hpp` | 实现 "缓冲区满 → 生产者等待" / "缓冲区空 → 消费者等待" 的条件同步 |
| `mutex` | `coro/comp/mutex.hpp` | 保护 channel 内部状态（buffer、计数器、关闭标志）的并发访问 |
| `spinlock` | `coro/spinlock.hpp` | 轻量级自旋锁，保护 waiter 链表的并发操作 |
| `task<T>` | `coro/task.hpp` | 协程返回类型 |

---

## 3. 数据结构设计

### 3.1 环形缓冲区（Ring Buffer）

使用定长 `std::array<T, capacity>` 作为底层存储，配合 `head` / `tail` 索引实现环形队列：

```
  buffer: [ _ , _ , val1 , val2 , val3 , _ , _ ]
            |               |                |
           tail           head              capacity

  读指针 (head)：指向下一个可读位置
  写指针 (tail)：指向下一个可写位置
  计数 count：当前缓冲区中的元素个数
```

无缓冲 channel（capacity=1）时，buffer 数组仅作为发送/接收之间的临时中转，不形成真正的缓冲队列。实现时可通过 `capacity == 1` 做编译期分支优化，也可统一处理——统一处理更简单且正确性易保证。

### 3.2 等待队列

发送者和接收者阻塞时，需要分别维护等待队列。最简单的方案是复用 `condition_variable` 的两个条件变量：

```
m_send_cv  — 发送者等待"缓冲区有空位"
m_recv_cv  — 接收者等待"缓冲区有数据"
```

关闭 channel 时，须唤醒**所有**阻塞的发送者和接收者，让它们检查 `m_closed` 标志后返回失败/排空。

### 3.3 推荐的类成员变量

```cpp
template<concepts::conventional_type T, size_t capacity = 1>
class channel
{
    using data_type = std::optional<T>;

    mutex              m_mtx;                          // 保护所有内部状态
    condition_variable m_send_cv;                      // 发送者等待条件
    condition_variable m_recv_cv;                      // 接收者等待条件
    std::array<T, capacity> m_buffer;                  // 环形缓冲区
    size_t m_head{0};                                  // 读指针
    size_t m_tail{0};                                  // 写指针
    size_t m_count{0};                                 // 当前元素数
    bool   m_closed{false};                            // 关闭标志
};
```

---

## 4. 实现步骤

按自底向上的顺序拆分为 4 个步骤：

### 步骤 1：实现 `send(value)`

**语义**：向 channel 写入一个值。若 channel 已关闭，立即返回 `false`。若缓冲区满，挂起协程直到有空位或被关闭唤醒。

**实现逻辑**：

```
1. co_await m_mtx.lock_guard()  — 获取 mutex 保护
2. while (缓冲区满 && !m_closed):
       co_await m_send_cv.wait(m_mtx)  — 释放 mutex 并挂起
   // 被唤醒后重新获取 mutex，再次检查条件
3. if (m_closed):
       m_send_cv.notify_all()  — 唤醒其他阻塞的发送者
       co_return false
4. 将 value 写入 buffer[m_tail]，m_tail = (m_tail + 1) % capacity
5. m_count++
6. m_recv_cv.notify_one()  — 唤醒一个等待的接收者
7. co_return true
```

**关键细节**：
- `condition_variable::wait()` 在挂起前会自动释放 mutex，唤醒后自动重新获取 mutex，保证谓词检查的原子性。
- 步骤 2 使用 `while` 而非 `if` 检查条件，处理虚假唤醒和 `notify_all` 后的竞争。
- 关闭 channel 时须唤醒其他发送者（步骤 3），避免它们永久阻塞。

### 步骤 2：实现 `recv()`

**语义**：从 channel 读取一个值。若缓冲区为空且 channel 已关闭，返回 `nullopt`。若缓冲区为空且未关闭，挂起协程直到有数据或被关闭唤醒。

**实现逻辑**：

```
1. co_await m_mtx.lock_guard()
2. while (缓冲区空 && !m_closed):
       co_await m_recv_cv.wait(m_mtx)
3. if (缓冲区空 && m_closed):
       m_recv_cv.notify_all()  — 唤醒其他阻塞的接收者
       co_return std::nullopt
4. 取出 buffer[m_head]，m_head = (m_head + 1) % capacity
5. m_count--
6. m_send_cv.notify_one()  — 唤醒一个等待的发送者
7. co_return std::optional<T>(value)
```

**与 send 的对称性**：
- `send` 写入 → notify `m_recv_cv`
- `recv` 读出 → notify `m_send_cv`
- 两步合起来就是经典的 "生产者增加信号量 / 消费者增加空位信号量" 模型。

### 步骤 3：实现 `close()`

**语义**：关闭 channel，拒绝后续写入，允许读取剩余数据。

**实现逻辑**：

```
1. m_mtx.lock()（或使用 lock_guard）
2. m_closed = true
3. m_send_cv.notify_all()  — 唤醒所有阻塞的发送者
4. m_recv_cv.notify_all()  — 唤醒所有阻塞的接收者
5. m_mtx.unlock()
```

**关键细节**：
- 必须在获取 mutex 后修改 `m_closed` 标志，保证与 `send`/`recv` 中检查标志的原子性。
- 同时唤醒发送者和接收者，让它们各自根据 `m_closed` 做退出处理。

### 步骤 4：深入理解协程环境下的锁管理

#### 4.1 核心问题：协程帧与 RAII 的交互

传统线程编程中，`lock_guard` 在栈上分配，函数返回时自动析构释放锁——这是确定性的、同步的。但在 C++20 协程中，执行流可能被多次挂起和恢复，**栈变量在挂起时被销毁**，只有协程帧（coroutine frame）中的变量能够跨挂起点存活。

编译器将协程体中的所有局部变量分析后，将跨越 `co_await` 的变量放入堆分配的协程帧中。这意味着：

```cpp
task<bool> send(T value)
{
    auto guard = co_await m_mtx.lock_guard(); // guard 存放在协程帧中
    while (满 && !关闭)
        co_await m_send_cv.wait(m_mtx);       // 挂起时 guard 仍然存活 ✓
    // guard 在协程退出时析构，自动释放锁
}
```

`guard` 的析构时机是协程**帧销毁**时（即 `send()` 协程执行完毕或被销毁时），而非某次 `co_await` 挂起时。这是协程与普通函数最关键的区别之一。

#### 4.2 `condition_variable::wait()` 内部的锁管理机制

理解 `wait()` 内部的锁行为是正确实现 channel 的前提。以下是 `wait()` 的完整执行流程：

```
1. await_ready() → 总是返回 false（CV 等待总是挂起）
2. await_suspend(handle):
   a. 保存协程句柄
   b. 调用 register_lock():
      - 若 predicate 已满足 → return false（不挂起，协程继续执行）
      - 若 predicate 不满足:
        - 将当前 awaiter 注册到 CV 的等待链表
        - 调用 m_mtx.unlock() 释放 mutex ← 关键！
        - return true（协程挂起）
3. [协程挂起，其他协程可以获取 mutex]
4. [被 notify_one/notify_all 唤醒]
5. resume():
   a. 重新检查 predicate（处理虚假唤醒）
   b. 尝试重新获取 mutex（try_lock 或注册为 mutex 等待者）
   c. 获取 mutex 后，恢复协程执行
6. await_resume() → 协程从 co_await 点继续执行
```

**关键认知**：`register_lock()` 中释放了 mutex，`resume()` 中重新获取了 mutex。因此：
- 进入 `wait()` 前：调用者持有 mutex ✓
- `wait()` 挂起期间：mutex 已释放，其他协程可以操作 channel ✓
- `wait()` 返回后：调用者重新持有 mutex ✓

#### 4.3 不需要 predicate 的 wait 用法

当使用不带 predicate 的 `wait(m_mtx)` 时，`register_lock()` 总是无条件释放 mutex 并挂起协程——因为没有 predicate 可供检查。此时调用者必须在 while 循环中自行检查条件：

```cpp
// 方案 A：不带 predicate，手动 while 循环
while (m_count >= capacity && !m_closed)  // ← 持锁检查条件
{
    co_await m_send_cv.wait(m_mtx);       // ← 释放锁、挂起、唤醒、重获锁
}
// wait 返回后，锁已被重新持有，再次检查 while 条件
```

#### 4.4 使用 predicate 的 wait 用法（推荐）

带 predicate 的 `wait(m_mtx, pred)` 更安全，因为 predicate 在 `register_lock()` 和 `resume()` 中都会被检查：

```cpp
// 方案 B：带 predicate，处理虚假唤醒更健壮
auto pred = [this] { return m_count < capacity || m_closed; };
while (m_count >= capacity && !m_closed)
{
    co_await m_send_cv.wait(m_mtx, pred);
}
```

对于带 predicate 的版本，`register_lock()` 中的行为是：
- `pred()` 返回 true → 不挂起，协程继续（条件已满足）
- `pred()` 返回 false → 释放 mutex，挂起

`resume()` 中的行为是：
- `pred()` 返回 true → 获取 mutex，恢复协程
- `pred()` 返回 false → 重新注册到 CV 等待队列（虚假唤醒处理）

#### 4.5 为什么 `close()` 不能使用 `lock_guard()`

`close()` 方法不需要挂起，因此可以直接在栈上使用 RAII：

```cpp
auto close() noexcept -> void
{
    auto guard = m_mtx.lock_guard();  // guard 在栈上（同步代码，无 co_await）
    m_closed = true;
    m_send_cv.notify_all();
    m_recv_cv.notify_all();
} // guard 析构，释放锁
```

注意：由于 `close()` 不包含 `co_await`，它不是协程，`lock_guard()` 返回的守卫对象在栈上正常析构即可。

---

## 5. 关键执行时序

以 单生产者→单消费者，无缓冲 channel（capacity=1）为例：

```text
[Producer Coroutine]                              [Consumer Coroutine]
       |                                                 |
       |----> ch.send(42)                                |
       |         mutex.lock() ✓                           |
       |         缓冲区满？否 (count=0)                    |
       |         buffer[0] = 42, count=1                  |
       |         mutex.unlock()                           |
       |         recv_cv.notify_one() ──────────────>     |
       |                                                 |----> ch.recv()
       |                                                 |         mutex.lock() ✓
       |                                                 |         缓冲区空？否 (count=1)
       |                                                 |         取出 42, count=0
       |                                                 |         mutex.unlock()
       |                                                <── send_cv.notify_one()
       |<── [Producer 被唤醒（无实际操作，                |
       |     因为它并未等待）]                              |
```

带阻塞的时序（有缓冲，缓冲区已满）：

```text
[Producer]                                         [Consumer]
    |                                                  |
    |----> ch.send(99)                                 |
    |         mutex.lock() ✓                            |
    |         缓冲区满？是                               |
    |         co_await send_cv.wait(mtx)                |
    |            → mutex.unlock(), 协程挂起              |
    |                                                  |----> ch.recv()
    |                                                  |         mutex.lock() ✓
    |                                                  |         取出数据, count--
    |                                                  |         mutex.unlock()
    |                                                  |         send_cv.notify_one()
    |<── [Producer 被唤醒]                               |
    |         mutex.lock() ✓ (wait 内部)                |
    |         重新检查条件：缓冲区有空位 ✓                |
    |         写入 buffer, count++                      |
    |         mutex.unlock()                            |
    |         recv_cv.notify_one()                      |
```

---

## 6. 常见陷阱与详细解析

### 6.1 陷阱一：环形缓冲区空/满歧义 —— `head == tail` 的双重含义

**这是 channel 实现中最隐蔽的错误。** 在环形缓冲区中，`head == tail` 同时表示"缓冲区为空"和"缓冲区已满"两种截然不同的状态。

```
空缓冲区:  [ _, _, _, _ ]      满缓冲区:  [ A, B, C, D ]
            ↑                         ↑
           head=0                    head=0
           tail=0                    tail=0   (tail 绕回)
```

**为什么仅用 `head == tail` 判断是错误的**：

```cpp
// ❌ 错误写法 — capacity > 1 时存在歧义
auto send(T value) -> task<bool>
{
    // ...
    while (m_head == m_tail && !m_closed)  // 空和满都会进入等待！
        co_await m_send_cv.wait(m_mtx);
    // ...
}
```

- 当缓冲区为空（count=0, head=tail=0）时，生产者应**立即写入**，但被错误地挂起
- 当缓冲区为空且消费者也被错误地挂起时，**双方互相等待，造成死锁**

**正确做法**：引入 `m_count` 显式追踪元素数量：

```cpp
// ✓ 正确写法 — 使用 m_count 消除歧义
while (m_count >= capacity && !m_closed)       // 发送者：count == capacity 才满
    co_await m_send_cv.wait(m_mtx);

while (m_count == 0 && !m_closed)              // 接收者：count == 0 才空
    co_await m_recv_cv.wait(m_mtx);
```

**`m_count`、`m_head`、`m_tail` 三者关系**：
- `m_count == 0` ⇔ 缓冲区为空（此时 `head == tail`）
- `m_count == capacity` ⇔ 缓冲区为满（此时 `head == tail`）
- `0 < m_count < capacity` ⇔ 缓冲区有数据但未满（此时 `head != tail`）

**capacity=1 的特例**：当 capacity=1 时，`head == tail` 恒成立（单个元素写入后 head 和 tail 总是指向同一位置），此时 `m_count` 是区分空/满的唯一依据。必须使用 `m_count >= capacity` 和 `m_count == 0` 来判断。

### 6.2 陷阱二：`notify_one()` 的放置位置 —— "Hurry Up and Wait" 问题

如果 `notify_one()` 放在临界区（持有 mutex）内调用，会发生什么？

```cpp
// △ 临界区内通知（正确但不高效）
m_buffer[m_tail] = std::move(value);
m_count++;
m_recv_cv.notify_one();  // 被唤醒的接收者立即尝试获取 mutex
// 但 mutex 还被当前协程持有！接收者获取失败 → 再次阻塞
// 造成一次无意义的上下文切换
```

**时序分析**：

```
[Producer]                          [Consumer (被 notify 唤醒)]
  |                                       |
  | 持有 mutex                            |
  | 写入 buffer                           |
  | recv_cv.notify_one() ──────────>      |
  |                                    resume() 调用 try_lock() → 失败（Producer 仍持有）
  |                                    注册为 mutex 等待者，再次挂起
  | mutex.unlock()                        |
  |                                    mutex::unlock() 转移锁 → Consumer 再次被唤醒
  |                                    获取 mutex，继续执行
```

**推荐做法**：将 `notify_one()` 放在临界区之外：

```cpp
// ✓ 推荐写法
{
    auto lock = co_await m_mtx.lock_guard();
    m_buffer[m_tail] = std::move(value);
    m_count++;
}  // lock_guard 析构，释放 mutex
m_recv_cv.notify_one();  // 此时消费者可以直接获取 mutex
```

**两种写法的对比**：

| 方面 | notify 在临界区内 | notify 在临界区外 |
| :--- | :--- | :--- |
| 正确性 | ✓ 正确 | ✓ 正确 |
| 性能 | △ 可能多余的 mutex 竞争 | ✓ 被唤醒者可直接获取锁 |
| 代码简洁性 | ✓ 代码更紧凑 | △ 需要控制 lock_guard 作用域 |

notify 放在临界区内也能保证正确性，只是效率稍低。如果你对性能不敏感，放在临界区内更简洁。

### 6.3 陷阱三：虚假唤醒与 while 循环的必要性

条件变量可能发生**虚假唤醒**（spurious wakeup）：协程在没有显式 `notify` 的情况下被唤醒。

此外，`notify_all()` 会唤醒**所有**等待者，但只有一个能满足条件。例如 channel 只有 1 个空位，但 `notify_all()` 唤醒了 5 个发送者——其中 4 个必须重新等待。

```cpp
// ❌ 错误 — 使用 if，一次唤醒后不重新检查
if (m_count >= capacity && !m_closed)  // 只检查一次
{
    co_await m_send_cv.wait(m_mtx);
}
// 可能在被虚假唤醒或 notify_all 后，m_count 仍然 >= capacity！
// 此时写入 buffer 会覆盖未读取的数据 → 数据丢失

// ✓ 正确 — 使用 while，唤醒后重新检查条件
while (m_count >= capacity && !m_closed)  // 循环检查
{
    co_await m_send_cv.wait(m_mtx);
}
// 被唤醒后自动回到 while 条件判断：
//   - 条件仍不满足 → 重新进入 wait（处理虚假唤醒 / notify_all 竞争）
//   - 条件已满足 → 退出循环，安全写入
```

**虚假唤醒的来源**：
1. **OS 调度器的伪唤醒信号**：底层 futex/eventfd 可能产生伪唤醒
2. **`notify_all()` 导致的过度唤醒**：多个等待者竞争同一个资源
3. **`close()` 唤醒后条件变更**：被唤醒后发现 channel 已关闭

### 6.4 陷阱四：`close()` 的级联通知 —— 忘记 `notify_all` 的后果

当 `close()` 被调用时，可能有大量协程阻塞在 `m_send_cv` 和 `m_recv_cv` 上。`close()` 通过 `notify_all` 唤醒它们，但被唤醒的协程也必须继续传播通知。

```cpp
// close() 唤醒所有等待者
auto close() noexcept -> void
{
    auto guard = m_mtx.lock_guard();
    m_closed = true;
    m_send_cv.notify_all();  // 唤醒所有阻塞的发送者
    m_recv_cv.notify_all();  // 唤醒所有阻塞的接收者
}
```

**send 被唤醒后检测到 m_closed，必须唤醒其他可能还在等待的发送者**：

```cpp
// ✓ send 中的级联通知
while (m_count >= capacity && !m_closed)
    co_await m_send_cv.wait(m_mtx);

if (m_closed)
{
    m_send_cv.notify_all();  // ← 关键！唤醒其他还在等待的发送者
    co_return false;
}
```

**为什么需要级联通知？**

```
时间线：
1. 5 个 Producer 阻塞在 m_send_cv.wait()
2. close() 调用 notify_all() → 唤醒全部 5 个
3. Producer#1 重获 mutex，发现 m_closed=true，返回 false
4. 如果 Producer#1 不调用 notify_all()...
5. Producer#2~#5 的情况取决于具体实现：
   - 使用 wait(mtx)：它们已被唤醒，在 resume() 中竞争 mutex，最终都能执行
   - 使用 wait(mtx, pred)：pred() 中有 m_closed 检查，它们也能检测到并退出

但是！如果在步骤 2 和步骤 3 之间有新的 Producer 调用了 send() 并注册到了 CV 链表上...
```

**更安全的做法**：在检测到 `m_closed` 后始终调用 `notify_all()`，确保不会遗漏任何等待者：

```cpp
// recv 同理
while (m_count == 0 && !m_closed)
    co_await m_recv_cv.wait(m_mtx);

if (m_count == 0 && m_closed)
{
    m_recv_cv.notify_all();  // 唤醒其他阻塞的接收者
    co_return std::nullopt;
}
```

### 6.5 陷阱五：move 语义 —— 避免不必要的深拷贝

对于 `std::string`、`std::vector` 等堆分配类型，拷贝意味着额外的内存分配和数据复制。

```cpp
// ❌ 错误 — 拷贝写入，触发不必要的堆分配
m_buffer[m_tail] = value;           // 调用了 T 的拷贝赋值运算符
auto result = m_buffer[m_head];     // 调用了 T 的拷贝构造函数

// ✓ 正确 — 移动语义，仅转移所有权（O(1)）
m_buffer[m_tail] = std::move(value);                   // T 的移动赋值
auto result = std::optional<T>(std::move(m_buffer[m_head])); // T 的移动构造
```

**`std::move` 的影响**：

| 类型 | 拷贝开销 | 移动开销 |
| :--- | :--- | :--- |
| `int`, `size_t` 等基础类型 | 无差别（两者都是寄存器/栈复制） | 同左 |
| `std::string` | O(n) 堆分配 + 字符复制 | O(1) 指针交换 |
| `std::vector<T>` | O(n) 堆分配 + 元素复制 | O(1) 指针交换 |

即使对于基础类型，`std::move` 也没有负面影响——编译器会将其优化为普通复制。因此统一使用 `std::move` 是最佳实践。

### 6.6 陷阱六：`waker` 链表竞争 —— `notify_one` 与 `register_cv` 的并发

虽然 channel 的 `m_mtx` 保护了数据状态，但 condition_variable 内部使用 `spinlock m_lock` 保护 waiter 链表。当以下两个操作并发时：

- **Thread A**：在 `notify_one()` 中从链表摘下第一个等待者
- **Thread B**：在 `register_cv()` 中向链表追加新的等待者

`notify_one()` 的实现确保操作顺序为：`lock → 摘除节点 → unlock → resume()`。将 `resume()` 放在 unlock 之后是因为 `resume()` 中可能调用 `register_cv()` 重新注册（spurious wakeup 时），而 `register_cv()` 也需要 `m_lock`——如果在持锁时调用 resume，会造成 spinlock 死锁。

**这由 condition_variable 内部保证，channel 实现者无需担心此细节**——但这解释了为什么 notify 的实现分两步（先摘链表再 resume）。

### 6.7 陷阱七：`close()` 的线程安全

`close()` 对 `m_closed` 的修改必须在持有 `m_mtx` 的前提下进行，否则与 `send()`/`recv()` 中对 `m_closed` 的读取形成数据竞争（data race），在 C++ 标准下是未定义行为。

```cpp
// ❌ 错误 — 无锁修改 m_closed，与 send/recv 中的读取形成 data race
auto close() noexcept -> void
{
    m_closed = true;  // 未定义行为：多线程环境下的非原子写入
    m_send_cv.notify_all();
    m_recv_cv.notify_all();
}

// ✓ 正确 — 持锁修改 m_closed
auto close() noexcept -> void
{
    auto guard = m_mtx.lock_guard();
    m_closed = true;
    m_send_cv.notify_all();
    m_recv_cv.notify_all();
}
```

**注意**：`bool` 在 C++ 中不是原子类型。即使在某些平台上 bool 读写可能碰巧是原子的，依赖这种巧合也是不可移植且危险的。

### 6.8 陷阱八：`recv()` 的关闭判断条件

`recv()` 退出条件需要双重检查：缓冲区为空 **且** channel 已关闭。

```cpp
// ❌ 错误 — 收到 m_closed 后立刻退出，不排空缓冲区
if (m_closed)
{
    m_recv_cv.notify_all();
    co_return std::nullopt;  // 缓冲区中可能还有未消费的数据！
}

// ✓ 正确 — 排空缓冲区后才退出
if (m_count == 0 && m_closed)
{
    m_recv_cv.notify_all();
    co_return std::nullopt;
}
```

**时序示例**：

```
1. Producer 发送 42，buffer=[42], count=1, tail=1
2. close() 设置 m_closed=true, notify_all()
3. Consumer 被唤醒，m_count=1, m_closed=true
4. 若只用 m_closed 判断 → 直接返回 nullopt，丢失了 42！
5. 若用 m_count==0 && m_closed → 先取出 42，下一次循环再返回 nullopt
```

### 6.9 陷阱九：condition_variable::wait() 与 lock_guard 的交互

当 `lock_guard` 的作用域跨越多次 `wait()` 调用时，需理解 wait 内部的锁管理：

```cpp
{
    auto lock = co_await m_mtx.lock_guard();  // 获取 mutex
    
    // lock 持有 mutex ✓
    
    while (m_count >= capacity && !m_closed)
    {
        co_await m_send_cv.wait(m_mtx, pred);
        // ↑ 挂起前：register_lock() 调用 m_mtx.unlock()
        // ↑ 唤醒后：resume() 调用 try_lock() 或 register_waiter() 重新获取 mutex
        // ↑ co_await 返回时：mutex 已被重新持有 ✓
    }
    
    // lock 仍然持有 mutex ✓（因为 wait 返回时已重新获取）
    
}  // lock 析构，调用 m_mtx.unlock()
```

**关键保证**：无论 `wait()` 内部如何操作 mutex，`wait()` 返回时调用者一定重新持有 mutex。因此 `lock_guard` 的作用域覆盖整个 while 循环是安全的。

### 6.10 陷阱十：`send()` 和 `recv()` 的对称性

channel 的核心是生产者和消费者的对称设计。违反对称性必然引入 bug。

| 操作 | send() | recv() |
| :--- | :--- | :--- |
| 等待条件 | `m_count >= capacity` | `m_count == 0` |
| 退出条件 | `m_closed` | `m_count == 0 && m_closed` |
| 操作 | 写入 buffer, `m_count++` | 读取 buffer, `m_count--` |
| 唤醒对方 | `m_recv_cv.notify_one()` | `m_send_cv.notify_one()` |
| 关闭时唤醒 | `m_send_cv.notify_all()` | `m_recv_cv.notify_all()` |
| 返回值 | `false`（关闭）/ `true`（成功） | `nullopt`（关闭）/ `optional(value)` |

理解并保持这种对称性，是正确实现 channel 的关键。

---

## 7. 与 lab5b（condition_variable）的关系

lab5b 的 `ConditionVarProducerConsumerTest` 测试的核心逻辑与 channel 实现几乎完全一致。以下是逐元素的映射关系：

```cpp
// lab5b: 生产者等待"缓冲区不满"，消费者等待"缓冲区不空"
co_await producer_cv.wait(mtx, [&] { return que.size() < capacity; });  // 生产者
co_await consumer_cv.wait(mtx, [&] { return !que.empty() || stop; });   // 消费者
```

| lab5b 概念 | channel 对应 | 说明 |
| :--- | :--- | :--- |
| `producer_cv` | `m_send_cv` | 发送者等待缓冲区有空位 |
| `consumer_cv` | `m_recv_cv` | 接收者等待缓冲区有数据 |
| `que.size() < capacity` | `m_count < capacity` | 判断是否有空位 |
| `!que.empty()` | `m_count > 0` | 判断是否有数据 |
| `stop` | `m_closed` | 停止标志 |
| `mtx` | `m_mtx` | 保护共享状态的互斥锁 |
| `que`（`std::queue`） | `m_buffer`（环形数组） | 数据存储 |

**迁移关键点**：

1. **条件谓词直接对应**：
   ```cpp
   // lab5b 生产者等待条件
   [&] { return que.size() < capacity; }
   // → channel 中等价为
   [this] { return m_count < capacity || m_closed; }
   // 注意：channel 版本增加了 m_closed 检查，确保 close 后能退出等待
   ```

2. **唤醒模式一致**：
   ```cpp
   // lab5b：生产者写入后唤醒消费者
   que.push(val);
   consumer_cv.notify_one();  // 对应 channel 中 m_recv_cv.notify_one()

   // lab5b：消费者读出后唤醒生产者
   que.pop();
   producer_cv.notify_one();  // 对应 channel 中 m_send_cv.notify_one()
   ```

3. **channel 相比 lab5b 的额外复杂性**：
   - lab5b 用 `std::queue` 天然区分空/满（`empty()`/`size()`），channel 必须用 `m_count` 替代
   - channel 引入了 `close()` 语义，需要级联 `notify_all()`
   - channel 使用 `m_mtx.lock_guard()` 返回 RAII 守卫，比 lab5b 的裸 `lock()/unlock()` 更安全
   - channel 的接口是协程化的（`task<bool>` / `task<optional<T>>`），而非普通函数

这意味着 channel 的实现可以直接从 lab5b 的生产者-消费者代码结构迁移，但需要注意上述额外复杂性。

---

## 8. 完整参考实现

以下是整合了所有陷阱经验的完整参考实现：

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
    size_t                  m_count{0};     // ← 陷阱一：必须用 count 区分空/满
    bool                    m_closed{false};

public:
    template<typename value_type>
        requires(std::is_constructible_v<T, value_type &&>)
    auto send(value_type&& value) noexcept -> task<bool>
    {
        // 获取锁（RAII guard 在协程帧中分配，跨挂起点安全 — 陷阱九）
        auto lock = co_await m_mtx.lock_guard();

        // 陷阱三：用 while 而非 if，处理虚假唤醒和 notify_all 竞争
        // 陷阱一：用 m_count >= capacity 而非 head == tail
        while (m_count >= capacity && !m_closed)
        {
            co_await m_send_cv.wait(m_mtx);
        }

        // 陷阱四：检测到 close 后须级联通知
        if (m_closed)
        {
            m_send_cv.notify_all();
            co_return false;
        }

        // 陷阱五：使用 std::move 避免不必要的深拷贝
        m_buffer[m_tail] = std::move(value);
        m_tail = (m_tail + 1) % capacity;
        m_count++;

        // lock_guard 在此自动析构释放锁
    } // 陷阱二：notify 放在锁外，避免 hurry-up-and-wait
    m_recv_cv.notify_one();
    co_return true;
}

auto recv() noexcept -> task<data_type>
{
    // 陷阱五：使用 std::move
    data_type result;
    {
        auto lock = co_await m_mtx.lock_guard();

        // 陷阱一：用 m_count == 0 而非 head == tail
        while (m_count == 0 && !m_closed)
        {
            co_await m_recv_cv.wait(m_mtx);
        }

        // 陷阱八：关闭判断必须同时检查 m_count == 0 和 m_closed
        //         确保排空所有缓冲数据后才返回 nullopt
        if (m_count == 0 && m_closed)
        {
            m_recv_cv.notify_all();  // 陷阱四：级联通知
            co_return std::nullopt;
        }

        result = std::optional<T>(std::move(m_buffer[m_head]));
        m_head = (m_head + 1) % capacity;
        m_count--;
    } // lock_guard 析构，释放 mutex

    // 陷阱二：notify 放在锁外，避免 hurry-up-and-wait
    // 陷阱十：对称性 — recv 唤醒 send_cv，与 send 唤醒 recv_cv 对称
    m_send_cv.notify_one();
    co_return result;
}

auto close() noexcept -> void
{
    // 陷阱七：必须在持有 mutex 的前提下修改 m_closed
    auto lock = m_mtx.lock_guard();
    m_closed = true;
    m_send_cv.notify_all();  // 唤醒所有阻塞的发送者
    m_recv_cv.notify_all();  // 唤醒所有阻塞的接收者
}
};
```

**设计决策记录**：

| 决策点 | 选择的方案 | 原因 |
| :--- | :--- | :--- |
| `send` 等待条件 | `m_count >= capacity` | 使用 count 避免 head==tail 的二义性（陷阱一） |
| 循环判断 | `while` 而非 `if` | 处理虚假唤醒和 notify_all 竞争（陷阱三） |
| 锁管理 | `lock_guard` + 作用域控制 | RAII 自动释放，作用域决定 notify 位置（陷阱二、陷阱九） |
| 关闭语义 | `m_closed` 检查嵌入等待条件 | close 后等待者立即退出，不遗漏通知（陷阱四） |
| 数据传递 | `std::move` | 避免堆分配类型的深拷贝（陷阱五） |
| `recv` 退出条件 | `m_count == 0 && m_closed` | 确保排空缓冲区（陷阱八） |
| `close` 同步 | 持 `m_mtx` 修改 `m_closed` | 避免 data race（陷阱七） |

---

## 9. 可能的扩展方向

1. **select 语义**：类似 Go 的 `select` 语句，允许同时等待多个 channel 操作。
2. **关闭后优雅排空**：`close()` 返回一个 `task<>`，在所有剩余数据被消费完后再 resolve。
3. **无缓冲严格 handoff**：capacity=1 时直接 handoff，无需通过 buffer 中转。
4. **迭代器接口**：channel 实现 `begin()/end()`，支持 `for (auto& val : ch)` 语法。
