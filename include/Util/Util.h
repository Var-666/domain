#pragma once

#include <iostream>
#include <string>

namespace Util {

// 校验取值范围：不在 [minVal, maxVal] 则回退 defaultVal 并打印警告。
template <typename T>
T ClampWithWarning(const std::string& name, T value, T minVal, T maxVal, T defaultVal) {
    if (value < minVal || value > maxVal) {
        std::cerr << "[Config] invalid value for " << name << "=" << value << " (expect " << minVal << "-" << maxVal << "), fallback to "
                  << defaultVal << "\n";
        return defaultVal;
    }
    return value;
}

}  // namespace Util
