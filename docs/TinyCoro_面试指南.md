# TinyCoro 项目面试指南

## 项目概述

TinyCoro 是一个基于 C++20 协程技术和 Linux io_uring 技术相结合的高性能异步协程库。项目核心设计理念是：

1. **高性能**：利用 io_uring 的高效 I/O 操作和协程的轻量级切换，在 I/O 密集型负载下表现出色
2. **易用性**：允许开发者以同步的方式编写异步执行的代码，降低维护成本
3. **协程安全**：提供协程安全的同步组件，以协程 suspend 代替线程阻塞

## 一、整体架构说明

### 1.1 架构分层

```
应用层 (Application)
    │
    ├── 同步组件 (mutex, latch, channel, etc.)
    │
    ├── 网络模块 (TCP server/client)
    │
调度层 (Scheduler)
    │
    ├── 调度器 (scheduler) - 单例模式，管理所有 context
    │
    ├── 分发器 (dispatcher) - Round-robin 任务分发
    │
执行层 (Execution)
    │
    ├── 上下文 (context) - 每个工作线程对应一个 context
    │
    ├── 引擎 (engine) - 核心执行单元，管理任务队列和 I/O 操作
    │
基础设施层 (Infrastructure)
    │
    ├── 协程任务 (task) - 协程封装，支持返回值和异常
    │
    ├── io_uring 代理 (uring_proxy) - io_uring 封装
    │
    ├── 无锁队列 (atomic_queue) - 高性能任务队列
```

### 1.2 核心数据流

```
1. 用户创建协程任务 (task)
2. 任务提交到调度器 (scheduler)
3. 调度器通过分发器 (dispatcher) 将任务分配到某个 context
4. context 将任务放入其引擎 (engine) 的任务队列
5. 引擎执行任务：
   a. 如果任务是 I/O 操作，通过 io_uring 提交
   b. 如果任务需要等待资源，协程挂起 (suspend)
   c. 当 I/O 完成或资源可用时，协程恢复 (resume)
6. 任务完成，结果返回给调用者
```

### 1.3 目录结构详解

```
tinyCoro/
├── benchmark/          # 压测程序，测试 echo server 性能
├── benchtests/         # Google Benchmark 性能测试
├── config/            # 配置文件，包含运行模式等配置项
├── examples/          # 使用示例
├── include/coro/      # 核心头文件
│   ├── comp/          # 协程同步组件 (lab4, lab5)
│   │   ├── mutex.hpp           # 互斥锁
│   │   ├── latch.hpp           # 倒计时锁
│   │   ├── channel.hpp         # 通道
│   │   ├── condition_variable.hpp # 条件变量
│   │   ├── event.hpp           # 事件通知
│   │   ├── wait_group.hpp      # 等待组
│   │   └── when_all.hpp        # 等待所有任务完成
│   ├── concepts/      # C++20 concepts 定义
│   ├── detail/        # 辅助工具
│   │   ├── container.hpp       # 通用容器，用于存储协程返回值
│   │   ├── types.hpp           # 类型定义
│   │   └── void_value.hpp      # void 类型特化
│   ├── net/           # 网络模块 (lab3)
│   │   ├── io_info.hpp         # I/O 信息结构
│   │   ├── base_awaiter.hpp    # 基础 awaiter
│   │   ├── io_awaiter.hpp      # I/O awaiter
│   │   └── tcp.hpp             # TCP 封装
│   ├── atomic_que.hpp          # 无锁队列封装
│   ├── context.hpp            # 上下文管理 (lab2b)
│   ├── dispatcher.hpp         # 任务分发器
│   ├── engine.hpp             # 执行引擎 (lab2a)
│   ├── log.hpp                # 日志模块
│   ├── meta_info.hpp          # 元信息，全局/线程局部变量
│   ├── scheduler.hpp          # 调度器
│   ├── spinlock.hpp           # 自旋锁
│   ├── task.hpp               # 协程任务封装 (lab1)
│   ├── uring_proxy.hpp        # io_uring 代理
│   └── utils.hpp              # 工具函数
├── scripts/           # 辅助脚本，如火焰图生成
├── src/              # 核心源文件
├── tests/            # 功能测试和内存安全测试
└── third_party/      # 第三方依赖
```

### 1.4 核心设计理念

1. **协程而非线程**：使用协程的轻量级切换替代线程的昂贵上下文切换
2. **异步非阻塞 I/O**：结合 io_uring 实现真正的高性能异步 I/O
3. **协程安全同步**：重新设计同步原语，避免线程阻塞
4. **分层解耦**：各模块职责清晰，便于测试和扩展
5. **无锁数据结构**：在高并发场景下减少竞争，提高性能

## 二、各组件模块详细说明

### 2.1 协程任务模块 (task.hpp)

#### 2.1.1 核心类

```cpp
template<typename return_type = void>
class task;  // 协程任务封装

namespace detail {
    struct promise_base;          // 基础 promise
    template<typename return_type>
    struct promise;               // 带返回值的 promise
}
```

#### 2.1.2 关键机制

1. **协程生命周期管理**：
   - `initial_suspend()`: 返回 `suspend_always`，协程创建后立即挂起
   - `final_suspend()`: 控制协程完成后的行为，返回自定义 awaiter
   - `unhandled_exception()`: 处理协程内未捕获的异常

2. **返回值存储**：
   - 使用 `container<return_type>` 存储协程返回值
   - 支持 void 类型特化
   - 支持移动语义，避免不必要的拷贝

3. **continuation 机制**：
   - 通过 `m_continuation` 存储父协程句柄
   - 协程完成后自动恢复父协程

4. **三种协程类型**：
   - `Normal`: 普通协程，完成后返回结果
   - `Detach`: 分离协程，独立执行，不返回结果
   - `Clean`: 清理协程，用于资源回收

#### 2.1.3 面试重点

- **理解 C++20 协程的三要素**：`promise_type`, `coroutine_handle`, `awaiter`
- **掌握协程状态机**：创建→挂起→恢复→完成
- **理解异常在协程中的传播机制**

### 2.2 执行引擎模块 (engine.hpp)

#### 2.2.1 核心职责

1. **任务队列管理**：使用无锁队列存储待执行协程
2. **I/O 操作处理**：通过 io_uring 提交和完成 I/O 请求
3. **协程调度**：执行任务，处理挂起和恢复

#### 2.2.2 关键数据结构

```cpp
class engine {
private:
    context& m_ctx;                      // 所属上下文
    uring_proxy m_uring;                 // io_uring 实例
    mpmc_queue<task_handle> m_task_que;  // 任务队列
    // ...
};
```

#### 2.2.3 核心流程

1. **任务执行循环**：
   ```cpp
   while (!m_stop) {
       if (!exec_one_task()) {
           poll_submit();  // 处理 I/O
       }
   }
   ```

2. **I/O 操作处理**：
   - 将 I/O 请求提交到 io_uring 的 SQ (Submission Queue)
   - 轮询 CQ (Completion Queue) 获取完成事件
   - 恢复等待 I/O 完成的协程

#### 2.2.4 面试重点

- **理解 io_uring 的工作原理**：SQ/CQ 环形队列，共享内存
- **掌握异步 I/O 与协程的结合方式**
- **了解无锁队列在高并发场景下的优势**

### 2.3 上下文和调度模块 (context.hpp, scheduler.hpp)

#### 2.3.1 上下文 (context)

**职责**：
- 每个工作线程对应一个 context
- 封装一个 engine 实例
- 管理线程的生命周期

**关键方法**：
- `run()`: 启动执行引擎
- `stop()`: 停止执行引擎
- `submit_task()`: 向引擎提交任务

#### 2.3.2 调度器 (scheduler)

**设计模式**：单例模式

**职责**：
- 管理所有 context 实例
- 控制系统的启动和停止
- 通过 dispatcher 分发任务

**两种运行模式**：
1. **长期运行模式** (kLongRunMode=true)
   - context 永不停止
   - 适用于服务器等长期运行的应用

2. **短期运行模式** (kLongRunMode=false)
   - context 执行完任务后立即停止
   - 适用于批处理等短期任务

#### 2.3.3 分发器 (dispatcher)

**策略**：Round-robin（轮询）

**职责**：决定将任务分配给哪个 context

#### 2.3.4 面试重点

- **理解多线程与协程的结合方式**
- **掌握任务分发策略及其优缺点**
- **了解不同运行模式的适用场景**

### 2.4 同步组件模块 (comp/)

#### 2.4.1 设计理念

传统同步原语（如 mutex）会阻塞线程，不适合协程环境。TinyCoro 重新实现同步组件，核心思想是：

**当资源不可用时，挂起协程而非阻塞线程**

#### 2.4.2 核心组件

1. **mutex (互斥锁)**
   - 协程安全的互斥锁
   - 使用等待队列管理等待的协程
   - 支持 `lock()` 和 `try_lock()` 操作

2. **latch (倒计时锁)**
   - 类似 `std::latch`，但协程安全
   - 用于等待多个协程完成

3. **channel (通道)**
   - 用于协程间通信
   - 支持生产者和消费者模式
   - 缓冲区满/空时挂起协程

4. **condition_variable (条件变量)**
   - 协程安全的条件变量
   - 与 mutex 配合使用

5. **wait_group (等待组)**
   - 等待一组协程完成
   - 类似 Go 语言的 WaitGroup

6. **when_all (等待所有)**
   - 等待多个可等待对象完成
   - 返回所有结果的 tuple

#### 2.4.3 实现模式

所有同步组件都遵循相似的模式：

```cpp
class sync_primitive {
public:
    auto operation() -> awaiter_type {
        // 检查资源是否可用
        if (resource_available()) {
            // 立即返回
            co_return result;
        } else {
            // 创建 awaiter，加入等待队列
            // 挂起协程
            co_await awaiter{*this};
        }
    }
    
private:
    std::atomic<...> m_state;      // 状态
    wait_queue<...> m_waiters;     // 等待队列
};
```

#### 2.4.4 面试重点

- **理解协程安全同步原语与传统同步原语的区别**
- **掌握等待队列的实现方式**
- **了解无锁数据结构在同步组件中的应用**

### 2.5 网络模块 (net/)

#### 2.5.1 核心类

1. **uring_proxy**: io_uring 的 C++ 封装
2. **io_awaiter**: 等待 I/O 操作完成的 awaiter
3. **tcp**: TCP 服务器和客户端的封装

#### 2.5.2 工作流程

```cpp
// 异步读取示例
task<void> async_read(int fd, void* buf, size_t count) {
    co_await io_awaiter{fd, buf, count, IORING_OP_READ};
    // 读取完成，继续执行
}
```

#### 2.5.3 面试重点

- **理解 io_uring 在网络编程中的优势**
- **掌握异步 I/O 与协程的结合方式**
- **了解 TCP 服务器的实现原理**

## 三、扩展方向和应用说明

### 3.1 性能优化方向

1. **io_uring 高级特性**：
   - 使用 `IORING_SETUP_COOP_TASKRUN` 减少系统调用
   - 实现 I/O 操作的批处理提交
   - 使用 `IORING_REGISTER_FILES` 注册文件描述符，减少开销

2. **调度算法优化**：
   - 实现基于负载的调度策略
   - 支持任务优先级
   - 实现工作窃取 (work stealing)

3. **内存管理优化**：
   - 实现协程栈的池化分配
   - 使用内存池管理频繁分配的小对象
   - 优化缓存局部性

### 3.2 功能扩展方向

1. **更多同步原语**：
   - semaphore (信号量)
   - barrier (屏障)
   - rw_lock (读写锁)

2. **网络协议支持**：
   - UDP 协议
   - TLS/SSL 加密
   - HTTP/WebSocket 协议

3. **定时器支持**：
   - 基于 io_uring 的定时器
   - 支持超时和延迟执行

4. **跨平台支持**：
   - Windows IOCP 支持
   - macOS kqueue 支持

### 3.3 应用场景

1. **高性能网络服务器**：
   - HTTP/HTTPS 服务器
   - 游戏服务器
   - 实时通信服务器

2. **I/O 密集型应用**：
   - 数据库代理
   - 文件处理服务
   - 数据流处理

3. **微服务架构**：
   - 服务网关
   - RPC 框架
   - 消息队列

### 3.4 与其他技术的对比

| 特性 | TinyCoro | 线程池 | Go goroutine | asyncio |
|------|----------|--------|--------------|---------|
| 切换开销 | 极低 (协程) | 高 (线程) | 极低 (协程) | 低 (协程) |
| I/O 性能 | 极高 (io_uring) | 中等 (epoll) | 高 (epoll) | 高 (epoll) |
| 内存使用 | 低 | 高 | 极低 | 低 |
| 编程模型 | 同步风格 | 回调/Future | 同步风格 | async/await |
| 生态系统 | C++ 库 | 标准库 | 完整语言 | Python 标准库 |

## 四、面试角度的问题及解答

### 4.1 基础概念类问题

#### Q1: 协程和线程的主要区别是什么？

**A:**
1. **调度方式**：线程由操作系统内核调度，协程由用户程序调度
2. **切换开销**：线程切换需要内核介入，开销大（微秒级）；协程切换在用户态完成，开销小（纳秒级）
3. **内存占用**：线程栈通常较大（MB级），协程栈较小（KB级）
4. **并发数量**：线程数量受内核限制，协程数量可达百万级
5. **阻塞影响**：线程阻塞会导致整个线程停止；协程阻塞只会影响当前协程

**TinyCoro 应用**：TinyCoro 利用协程的轻量级特性，实现高并发 I/O 操作，避免线程切换开销。

#### Q2: C++20 协程的核心组件有哪些？

**A:**
1. **coroutine handle**：协程句柄，用于恢复和销毁协程
2. **promise type**：承诺类型，定义协程的行为和存储结果
3. **awaiter**：等待器，定义 `co_await` 的行为
4. **coroutine traits**：协程特性，用于推导 promise type

**TinyCoro 示例**：
```cpp
// task 类定义了 promise_type
template<typename T>
class task {
public:
    struct promise_type {
        task get_return_object();
        std::suspend_always initial_suspend();
        auto final_suspend() noexcept;
        void return_value(T value);
        void unhandled_exception();
    };
};
```

#### Q3: io_uring 相比 epoll 有哪些优势？

**A:**
1. **零拷贝**：io_uring 使用共享内存，减少数据拷贝
2. **批处理**：支持批量提交和完成 I/O 请求
3. **轮询模式**：可配置为纯轮询模式，完全避免系统调用
4. **更多操作**：支持更多类型的 I/O 操作（如 statx, openat 等）
5. **内存排序**：天然支持内存排序，简化异步编程

**TinyCoro 集成**：TinyCoro 通过 `uring_proxy` 封装 io_uring，提供简单的 C++ API，并与协程无缝集成。

### 4.2 项目设计类问题

#### Q4: TinyCoro 的调度器是如何工作的？

**A:**
TinyCoro 采用三级调度架构：

1. **scheduler** (单例)：管理所有 context，决定何时启动/停止
2. **dispatcher**：采用 round-robin 策略将任务分发到各个 context
3. **context** (每个工作线程一个)：包含 engine，实际执行任务

**工作流程**：
1. 用户创建任务并提交到 scheduler
2. scheduler 通过 dispatcher 选择 target context
3. 任务被放入 context 的 engine 的任务队列
4. engine 执行任务，处理 I/O 和协程切换

**设计优势**：
- 解耦任务分发和执行
- 支持多种调度策略
- 易于扩展（如实现基于负载的调度）

#### Q5: 如何实现协程安全的 mutex？

**A:**
传统 mutex 会阻塞线程，不适合协程。TinyCoro 的协程安全 mutex 实现：

```cpp
class mutex {
public:
    auto lock() -> mutex_awaiter {
        // 尝试获取锁
        if (try_lock()) {
            co_return;  // 立即返回
        }
        
        // 创建 awaiter 并加入等待队列
        mutex_awaiter awaiter{*this};
        m_wait_queue.push(&awaiter);
        
        // 挂起协程，等待锁可用
        co_await awaiter;
    }
    
    void unlock() {
        // 释放锁，唤醒等待队列中的下一个协程
        if (auto* next = m_wait_queue.pop()) {
            next->resume();  // 恢复协程
        }
    }
    
private:
    std::atomic<bool> m_locked{false};
    wait_queue<mutex_awaiter*> m_wait_queue;
};
```

**关键点**：
1. 使用原子操作管理锁状态
2. 等待队列存储等待的协程
3. unlock 时唤醒等待队列中的下一个协程
4. 协程挂起而非线程阻塞

#### Q6: when_all 是如何实现等待多个协程完成的？

**A:**
`when_all` 的实现原理：

1. **计数器机制**：使用 `latch` 或原子计数器跟踪未完成的任务数
2. **并行执行**：所有输入协程并行执行
3. **结果收集**：每个协程完成时存储结果并减少计数器
4. **等待完成**：当计数器归零时，恢复等待的协程

**简化实现**：
```cpp
template<typename... Tasks>
auto when_all(Tasks&&... tasks) -> task<std::tuple<...>> {
    // 创建 latch，初始值为任务数量
    latch counter(sizeof...(Tasks));
    
    // 并行执行所有任务
    auto results = std::make_tuple(启动任务(tasks, counter)...);
    
    // 等待所有任务完成
    co_await counter;
    
    // 返回结果
    co_return results;
}
```

**关键优化**：
- 使用 `std::apply` 和 `std::index_sequence` 遍历变参模板
- 支持异常传播，任一任务失败则整体失败
- 零开销抽象，编译时展开

### 4.3 性能优化类问题

#### Q7: TinyCoro 如何实现高性能？

**A:**
TinyCoro 的性能优化策略：

1. **协程轻量级切换**：避免线程上下文切换开销
2. **io_uring 高效 I/O**：零拷贝、批处理、轮询模式
3. **无锁数据结构**：减少线程竞争，提高并发性能
4. **内存池**：减少动态内存分配开销
5. **编译时优化**：使用模板和 constexpr，零运行时开销

**性能数据**：
- 协程切换开销：约 50ns (线程切换约 1-2μs)
- echo server QPS：100万+ (1KB 负载，100并发连接)
- 内存占用：每个协程约 1KB (每个线程约 8MB)

#### Q8: 如何处理协程中的异常？

**A:**
TinyCoro 的异常处理机制：

1. **异常捕获**：在 `promise_type::unhandled_exception()` 中捕获异常
2. **异常存储**：将异常指针存储在 promise 对象中
3. **异常传播**：在 `await_resume()` 中检查并重新抛出异常
4. **链式传播**：异常沿协程调用链向上传播

**实现示例**：
```cpp
struct promise_type {
    std::exception_ptr m_exception{nullptr};
    
    void unhandled_exception() {
        m_exception = std::current_exception();
    }
    
    T await_resume() {
        if (m_exception) {
            std::rethrow_exception(m_exception);
        }
        return std::move(m_value);
    }
};
```

**最佳实践**：
- 在协程边界处理异常
- 使用 `when_all` 时注意异常传播
- 避免异常对性能的影响（高频路径使用错误码）

#### Q9: 如何避免协程的内存泄漏？

**A:**
内存泄漏预防策略：

1. **RAII 管理**：`task` 对象析构时自动销毁协程
2. **final_suspend 机制**：确保协程完成后正确清理
3. **detach 模式**：分离协程自行管理生命周期
4. **智能指针**：使用智能指针管理共享资源

**关键代码**：
```cpp
auto final_suspend() noexcept {
    struct final_awaiter {
        bool await_ready() noexcept { return false; }
        
        std::coroutine_handle<> await_suspend(
            std::coroutine_handle<promise_type> h) noexcept {
            // 清理逻辑
            if (h.promise().m_state == CoroType::Normal) {
                return h.promise().m_continuation;
            }
            return std::noop_coroutine();
        }
        
        void await_resume() noexcept {}
    };
    return final_awaiter{};
}
```

### 4.4 系统设计类问题

#### Q10: 如果让你设计一个基于 TinyCoro 的 HTTP 服务器，你会如何设计？

**A:**
**架构设计**：
```
HTTP Server
├── Listener 协程：接受新连接
├── Connection 池：管理活跃连接
├── Request 解析器：解析 HTTP 请求
├── Handler 分发器：路由到对应的处理函数
├── Response 构建器：构建 HTTP 响应
└── Writer 协程：发送响应
```

**关键设计**：
1. **连接管理**：每个连接一个协程，轻量级，支持百万连接
2. **I/O 多路复用**：使用 io_uring 高效处理大量 I/O
3. **零拷贝发送**：利用 io_uring 的零拷贝特性发送文件
4. **连接池**：复用连接，减少创建销毁开销
5. **异步处理**：所有阻塞操作（如数据库查询）都异步化

**性能优化**：
1. **内存池**：为 HTTP 请求/响应对象实现内存池
2. **缓存**：缓存频繁访问的静态文件
3. **批处理**：批量处理 I/O 操作
4. **协程池**：避免频繁创建销毁协程

### 4.5 故障排除类问题

#### Q11: 如果协程出现死锁，如何调试？

**A:**
**调试策略**：

1. **日志追踪**：
   ```cpp
   // 在关键路径添加日志
   log::debug("coro {} waiting for mutex", coro_id);
   co_await mutex.lock();
   log::debug("coro {} acquired mutex", coro_id);
   ```

2. **协程 ID**：为每个协程分配唯一 ID，方便追踪

3. **死锁检测**：
   - 记录协程等待图
   - 检测循环等待
   - 超时机制，避免无限等待

4. **工具支持**：
   - 使用 gdb 调试协程
   - 自定义调试器扩展，显示协程状态
   - 性能分析工具（如 perf）定位瓶颈

**预防措施**：
1. 使用 `lock_guard` 确保锁的释放
2. 避免嵌套锁，或按固定顺序获取锁
3. 使用超时机制
4. 编写单元测试覆盖并发场景

#### Q12: 如何测试高并发场景下的协程安全性？

**A:**
**测试策略**：

1. **压力测试**：模拟高并发场景，测试系统稳定性
2. **竞态条件测试**：使用 ThreadSanitizer 检测数据竞争
3. **死锁检测**：使用锁层次分析工具
4. **内存测试**：使用 Valgrind 检测内存泄漏

**TinyCoro 的测试方案**：
1. **功能测试**：200+ 测试用例，覆盖所有功能
2. **内存安全测试**：使用 Valgrind 检查内存问题
3. **性能测试**：使用 Google Benchmark 进行性能对比
4. **并发测试**：多线程环境下的正确性测试

**测试工具**：
```shell
# 运行功能测试
make test-lab4a

# 运行内存安全测试  
make memtest-lab4a

# 运行性能测试
make benchtest-lab4a
```

## 五、学习建议和资源

### 5.1 学习路径建议

1. **第一阶段：基础概念**
   - 学习 C++20 协程基础
   - 了解 io_uring 基本原理
   - 阅读 TinyCoro 整体架构文档

2. **第二阶段：代码阅读**
   - 从 `task.hpp` 开始，理解协程封装
   - 阅读 `engine.hpp` 和 `scheduler.hpp`，理解调度机制
   - 选择一个同步组件（如 `mutex.hpp`），理解实现原理

3. **第三阶段：动手实践**
   - 完成 TinyCoroLab 实验
   - 基于 TinyCoro 实现一个小项目（如 echo server）
   - 尝试添加新功能（如定时器）

### 5.2 推荐资源

1. **官方文档**：
   - [C++20 协程规范](https://en.cppreference.com/w/cpp/language/coroutines)
   - [io_uring 手册](https://man7.org/linux/man-pages/man7/io_uring.7.html)

2. **参考项目**：
   - [libcoro](https://github.com/jbaldwin/libcoro)：C++20 协程库
   - [asio](https://github.com/chriskohlhoff/asio)：C++ 网络库
   - [seastar](https://github.com/scylladb/seastar)：高性能异步框架

3. **学习资料**：
   - 《C++ Concurrency in Action》
   - [协程介绍](https://lewissbaker.github.io/)
   - [io_uring 教程](https://unixism.net/loti/)

### 5.3 面试准备清单

1. **概念理解**：
   - [ ] 协程 vs 线程
   - [ ] C++20 协程三要素
   - [ ] io_uring 工作原理

2. **项目理解**：
   - [ ] TinyCoro 整体架构
   - [ ] 各模块职责和交互
   - [ ] 同步组件的实现原理

3. **代码能力**：
   - [ ] 能解释关键代码片段
   - [ ] 能设计简单的协程应用
   - [ ] 能分析并解决常见的并发问题

4. **系统设计**：
   - [ ] 基于 TinyCoro 设计高性能服务器
   - [ ] 分析系统瓶颈并提出优化方案
   - [ ] 设计测试方案确保系统稳定性

## 六、总结

TinyCoro 是一个优秀的 C++20 协程库项目，它展示了现代 C++ 异步编程的最佳实践：

1. **技术创新**：将 C++20 协程与 io_uring 结合，实现高性能异步 I/O
2. **工程实践**：模块化设计，完善的测试体系，良好的文档
3. **学习价值**：深入理解协程、异步编程、高性能服务器设计

通过深入学习和实践 TinyCoro，你不仅能掌握 C++20 协程和 io_uring 技术，还能提升系统设计和性能优化能力，为面试和工作打下坚实基础。

---

*本文档基于 TinyCoro 项目分析编写，旨在帮助开发者深入理解项目并准备相关面试。如有错误或不足，欢迎指正。*