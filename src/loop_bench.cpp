#include "rt_platform.h"

#include <cstdio>

int main() {
  std::puts(rt::platform_name());
  return 0;
}
