#pragma once

#include "Config.h"
#include "MessageLimiter.h"
#include "MessageRouter.h"

// 构建按 msgType 限流的中间件
Middleware BuildRateLimitMiddleware(const Config& cfg, std::shared_ptr<MessageLimiter> limiter);
