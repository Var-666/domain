#pragma once
#include "Config.h"
#include "MessageRouter.h"
#include "MessageLimiter.h"

void RegisterMiddlewares(MessageRouter& router, const Config& cfg);
