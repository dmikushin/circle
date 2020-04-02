#ifndef LOG_H
#define LOG_H

#include "lanl_circle.hpp"

#include <stdint.h>
#include <stdio.h>
#include <time.h>

namespace circle {

#define LOG(level, ...)                                                        \
  do {                                                                         \
    if (level <= circle::internal::circle.logLevel) {                              \
      fprintf(circle::debug_stream, "%d:%d:%s:%d:", (int)time(NULL),           \
              circle::global_rank, __FILE__, __LINE__);                        \
      fprintf(circle::debug_stream, __VA_ARGS__);                              \
      fprintf(circle::debug_stream, "\n");                                     \
      fflush(circle::debug_stream);                                            \
    }                                                                          \
  } while (0)

extern FILE *debug_stream;
extern int32_t global_rank;

} // namespace circle

#endif /* LOG_H */
