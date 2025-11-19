config = {
  -- server 相关配置
  server = {
    port = 8080,               -- 监听端口
    ioThreadsCount = 2,        -- Asio I/O 线程数（<=0 会自动用 CPU 核心数）
    workerThreadsCount = 4,    -- 后台业务线程池大小（<=0 同样自动取核心数）
    IdleTimeoutMs = 60000,     -- 空闲连接超时毫秒数，超过则由 IdleConnectionManager 关闭
  },

  -- 线程池限流：控制任务队列最多能积压多少任务
  threadPool = {
    workerThreadsCount = 4,    -- 后台业务线程池大小（<=0 同样自动取核心数）
    maxQueueSize = 10000,      -- 0 表示不限制，其他为硬限制（超过会抛异常）

    minThreads = 2,
    maxThreads = 8,

    autoTune = true,

    -- 根据 maxQueueSize 选定水位（这里直接写数值，避免 Lua 解析时 nil）
    highWatermark = 7000,      -- ~70% 的 maxQueueSize
    lowWatermark = 1000,       -- ~10% 的 maxQueueSize
    upThreshold = 3,
    downThreshold = 10,
  },

  -- 全局限制
  limits = {
    maxInflight = 10000,       -- 同一时刻正在处理的请求数上限（超过会直接 drop）
    maxSendBufferBytes = 4 * 1024 * 1024   -- 单连接发送缓冲区最大字节数（背压用）
  },

  -- 日志：基于 spdlog 异步 logger，支持控制台 + 文件
  log = {
    level = 'info',            -- 日志级别：trace/debug/info/warn/error/critical/off
    asyncQueueSize = 8192,     -- 异步日志队列长度
    flushIntervalMs = 1000,    -- 日志自动刷新间隔（ms）

    console = {
      enable = true,           -- 是否输出到终端
    },

    file = {
      enable = true,           -- 是否写 rotating 文件
      baseName = 'server',     -- 日志前缀，实际文件是 baseName.log
      maxSizeMb = 100,         -- 单个文件最大 MB
      maxFiles = 5,            -- 最多保留几个轮转日志
    },
  },

  -- 按 msgType 的限流配置
  messageLimits = {
    -- 心跳：一般不单独限流（enabled=false）
    [1] = {
      enabled = false,
    },
    -- echo：示例，限制 QPS 和并发
    [2] = {
      enabled = true,
      maxQps = 10000,
      maxConcurrent = 1000,
    },
    -- 以后你有重型请求，比如排行榜查询，可以限得更严
    [200] = {
      enabled = true,
      maxQps = 100,
      maxConcurrent = 10,
    } 
  }

}

-- 兼容下划线命名：有些解析器会读取 config.thread_pool
config.thread_pool = config.threadPool
