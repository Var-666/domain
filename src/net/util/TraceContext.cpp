#include "TraceContext.h"

namespace {
// 默认填充为 "-"，避免日志出现空白 trace/session
thread_local std::string g_traceId{"-"};
thread_local std::string g_sessionId{"-"};
}  // namespace

void TraceContext::SetTraceId(const std::string& id) { g_traceId = id; }

void TraceContext::SetSessionId(const std::string& id) { g_sessionId = id; }

const std::string& TraceContext::GetTraceId() { return g_traceId; }

const std::string& TraceContext::GetSessionId() { return g_sessionId; }

TraceContext::Guard::Guard(const std::string& trace, const std::string& session) {
    prevTrace_ = g_traceId;
    prevSession_ = g_sessionId;
    g_traceId = trace;
    g_sessionId = session;
}

TraceContext::Guard::~Guard() {
    g_traceId = prevTrace_;
    g_sessionId = prevSession_;
}
