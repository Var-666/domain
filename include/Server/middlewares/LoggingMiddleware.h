#pragma once

#include "Config.h"
#include "MessageRouter.h"

// 简单日志中间件（按配置开启）
Middleware BuildLoggingMiddleware(const Config& cfg);
