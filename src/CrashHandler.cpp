#include "CrashHandler.h"

#include <execinfo.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
    struct PrevHandlers {
        struct sigaction segv;
        struct sigaction abrt;
        struct sigaction fpe;
        struct sigaction ill;
        struct sigaction bus;
    };

    PrevHandlers gPrev{};
    bool gInstalled = false;

    void safeWrite(const char* msg) { ::write(STDERR_FILENO, msg, ::strlen(msg)); }

    void signalHandler(int sig, siginfo_t* info, void* ucontext) {
        (void) info;
        (void) ucontext;

        const char* name = "UNKNOWN";
        switch (sig) {
            case SIGSEGV:
                name = "SIGSEGV";
                break;
            case SIGABRT:
                name = "SIGABRT";
                break;
            case SIGFPE:
                name = "SIGFPE";
                break;
            case SIGILL:
                name = "SIGILL";
                break;
            case SIGBUS:
                name = "SIGBUS";
                break;
            default:
                break;
        }

        safeWrite("\n==== FATAL SIGNAL ====\n");
        safeWrite("Signal: ");
        safeWrite(name);
        safeWrite("\nBacktrace:\n");

        // 打印简单 backtrace 到 stderr
        void* array[64];
        int size = ::backtrace(array, 64);
        ::backtrace_symbols_fd(array, size, STDERR_FILENO);

        safeWrite("\n==== END BACKTRACE ====\n");

        // 恢复默认处理，然后再触发一次信号以生成 core / 被外部捕获
        ::signal(sig, SIG_DFL);
        ::raise(sig);
    }

    void installOne(int sig, struct sigaction& oldAct) {
        struct sigaction sa;
        sa.sa_sigaction = &signalHandler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_SIGINFO | SA_RESETHAND;  // 触发一次后恢复默认

        if (sigaction(sig, &sa, &oldAct) != 0) {
            // 这里不能用 printf/spdlog，只能悄悄失败
        }
    }
}  // namespace

namespace CrashHandler {
    void init() {
        if (gInstalled) {
            return;
        }
        gInstalled = true;

        installOne(SIGSEGV, gPrev.segv);
        installOne(SIGABRT, gPrev.abrt);
        installOne(SIGFPE, gPrev.fpe);
        installOne(SIGILL, gPrev.ill);
        installOne(SIGBUS, gPrev.bus);
    }

    void restoreDefault() {
        if (!gInstalled) {
            return;
        }
        gInstalled = false;

        sigaction(SIGSEGV, &gPrev.segv, nullptr);
        sigaction(SIGABRT, &gPrev.abrt, nullptr);
        sigaction(SIGFPE, &gPrev.fpe, nullptr);
        sigaction(SIGILL, &gPrev.ill, nullptr);
        sigaction(SIGBUS, &gPrev.bus, nullptr);
    }
}  // namespace CrashHandler