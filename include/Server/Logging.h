#pragma once

#include <memory>

#include "Config.h"

namespace Logging {
    void InitFromConfig();
    void shutdown();
}  // namespace Logging
