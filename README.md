# Simple Boost.Asio Echo Server

> 轻量级的异步 TCP 服务骨架，集成了配置、日志、限流、监控等生产级模块，业务层通过回调快速接入。

---

## 🔍 功能亮点

- **异步连接与线程池**：基于 Boost.Asio + `ThreadPool`，通过 `ConnectionManager`/`IdleConnectionManager` 做连接生命周期管理与空闲清理。
- **协议层**：`LengthHeaderCodec` 负责长度前缀 + 消息类型的解码/编码，方便插入任意业务逻辑（示例为 echo）。
- **运维友好**：Lua 配置、spdlog 异步日志（Console + Rotating File）、`MetricsRegistry` 指标打印，便于调参和监控状态。

## 📁 目录一览

| 路径 | 说明 |
| --- | --- |
| `src/` | 各模块实现：AsioServer/AsioConnection/Config/Codec/Logging 等。 |
| `include/` | 公共头，包含 Buffer、BufferPool、ThreadPool、Metrics、ConnectionManager 等。 |
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
4. **验证协议**：使用 `clientTest.cpp`、`netcat` 或其他客户端发送长度帧（[4B len][2B msgType][body]），服务器会 echo 并在 stdout 输出日志。

## 🛠️ 可配置项（参考 `config.lua`）

- `server.port`、`ioThreadsCount`、`workerThreadsCount`：控制监听端口与线程数量。
- `threadPool.maxQueueSize`：限制后台任务队列最大长度，超出会在 `ThreadPool::submit` 抛出异常。
- `limits.maxInflight`, `maxSendBufferBytes`：全局的 in-flight 限流与单连接发送缓冲背压。
- `log`：`level`/`asyncQueueSize`/`flushIntervalMs` + console/file 的开关和文件策略。

## 🧠 后续建议

1. 替换 `LengthHeaderCodec` 的 `FrameCallback` 为真实业务处理，或在 `server.setMessageCallback` 中接入 protobuf/json/数据库等。
2. 把配置项与 `ThreadPool`、限流等真正联动，增加热加载/命令行覆盖优先级。
3. 撰写简易单测或集成测试（例如启动 server + 连接发送 frame）并纳入 CI，提升回归保障。
