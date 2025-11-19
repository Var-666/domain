#include "GlobalState.h"

std::atomic<int> gInflight{0};
const int kMaxInflight = 50000;  // 根据你业务需求调