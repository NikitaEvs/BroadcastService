#include <cstring>
#include <iostream>
#include <sstream>

#include "util/logging.hpp"

#define CANNOT_FAIL(code, message)                                             \
  do {                                                                         \
    if (code == -1) {                                                          \
      std::stringstream ss;                                                    \
      ss << "Syscall failed with message: " << message                         \
         << " | errno: " << std::strerror(errno) << std::endl;                 \
      LOG_ERROR("SYS", ss.str());                                              \
      exit(1);                                                                 \
    }                                                                          \
  } while (false)
