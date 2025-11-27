#pragma once

#include <string>

// 简单的线程/协程本地 trace/session 上下文，用于日志填充。
class TraceContext {
  public:
    // 设置当前 traceId/sessionId
    static void SetTraceId(const std::string& id);
    static void SetSessionId(const std::string& id);
    static const std::string& GetTraceId();
    static const std::string& GetSessionId();

    // RAII 方式设置 trace/session，析构时恢复原值。
    class Guard {
      public:
        Guard(const std::string& trace, const std::string& session);
        ~Guard();

      private:
        std::string prevTrace_;
        std::string prevSession_;
    };
};
