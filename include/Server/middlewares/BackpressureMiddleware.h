#pragma once

#include "Config.h"
#include "MessageRouter.h"

// 构建背压拒绝低优先级消息的中间件（协程版）
CoMiddleware BuildBackpressureMiddleware(const Config& cfg);
