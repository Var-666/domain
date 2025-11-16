# Simple Boost.Asio Echo Server

> 轻量级的异步 TCP 服务骨架，集成配置/日志/限流/监控/信号优雅停机等生产要素；业务层通过路由 + 中间件快速接入。

---

## 🔍 功能亮点

- **异步 I/O + 线程池**：Boost.Asio 驱动，`ConnectionManager`/`IdleConnectionManager` 管理连接生命周期，`ThreadPool` 支持优先级队列。
- **协议与路由**：`LengthHeaderCodec` 负责帧编解码；`MessageRouter`+`RouteRegistry` 按 `msgType` 分发，支持中间件链（限流/日志/鉴权占位）。
- **按消息限流**：`MessageLimiter` 从 Lua 配置读取 per-msgType QPS/并发上限，超限可计错并丢弃；可在 middleware 层定制回执。
- **运维友好**：Lua 配置、spdlog 异步日志（Console + Rotating File）、`MetricsRegistry` 指标打印，CrashHandler 捕获致命信号输出回溯，信号监听支持优雅停机。

## 📁 目录一览

| 路径 | 说明 |
| --- | --- |
| `src/` | 核心实现：AsioServer/AsioConnection/Config/Codec/Logging/Router/Middlewares/InitServer 等。 |
| `include/` | 公共头：Buffer/ThreadPool/Metrics/ConnectionManager/MessageRouter/MessageLimiter/RouteRegistry 等。 |
| `config.lua` | Lua 配置文件，分 server/threadPool/limits/log 四个区块。 |
| `clientTest.cpp` | 简易客户端，用于手动验证协议或压力测试。 |

## 🚀 快速启动

1. **安装依赖**：Boost.Asio、spdlog、Lua、CMake/C++ 编译器等。
2. **构建项目**：
   ```bash
   cmake -S . -B build
   cmake --build build
   ```
3. **运行可执行文件**：
   ```bash
   ./build/server
   ```
   默认会加载工程根目录下的 `config.lua`。
4. **验证协议**：使用 `clientTest.cpp`、`netcat` 或其他客户端发送长度帧（[4B len][2B msgType][body]），服务器会 echo 并在 stdout/log 输出。

## 🛠️ 可配置项（参考 `config.lua`）

- `server.*`：端口、io/worker 线程数、空闲超时、队列长度。
- `threadPool.maxQueueSize`：后台任务队列上限。
- `limits.*`：全局 in-flight 限流、单连接发送缓冲上限。
- `log.*`：日志级别、异步队列、flush 周期、console/file 开关与策略。
- `messageLimits.{msgType}`：按消息类型的限流（enabled/maxQps/maxConcurrent）。

## 🧠 后续建议

1. 利用 `RouteRegistry` 拆分业务模块，`MessageRouter` 中间件加入鉴权/监控上报、限流回执。
2. 让配置驱动更多策略（线程池扩缩容、路由开关、限流回执），并可选支持热加载。
3. 编写集成测试（启动 server + clientTest 心跳/echo/未知命令）并接入 CI，验证路由/限流/优雅停机行为。
