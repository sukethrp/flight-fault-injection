#include "rt_platform.h"

namespace rt {

const char* platform_name() {
#if defined(__APPLE__)
  return "darwin";
#elif defined(__linux__)
  return "linux";
#else
#error unsupported platform
#endif
}

}
