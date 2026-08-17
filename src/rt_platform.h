#pragma once
#include <cstdint>

namespace rt {

const char* platform_name();
int64_t now_ns();
void sleep_until_ns(int64_t deadline_ns);

}
