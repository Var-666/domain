#pragma once
#include "Config.h"
#include "MessageRouter.h"

void RegisterMiddlewares(MessageRouter& router, const Config& cfg);
