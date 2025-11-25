#pragma once

namespace CrashHandler {
    // 注册信号处理器（SIGSEGV/SIGABRT/...）
    void init();
    // 可选：程序退出前恢复默认信号处理器
    void restoreDefault();
}  // namespace CrashHandler
