#ifndef CIRCLE_IMPL_H
#define CIRCLE_IMPL_H

#include "lanl_circle.hpp"
#include "queue.hpp"

#include <mpi.h>

namespace circle {

namespace internal {

class CircleImpl {

public : // TODO remove

  Circle* parent;

  MPI_Comm comm;

  // TODO Move to impl.
  void *reduce_buf;
  size_t reduce_buf_size;
  int reduce_period;
  LogLevel logLevel;
  RuntimeFlags runtimeFlags;

  /** The debug stream for all logging messages. */
  FILE *logStream;

  /* width of internal communication k-ary tree */
  int tree_width;

  int rank;
 
  Queue* queue;

public :

  CircleImpl(Circle* parent, RuntimeFlags runtimeFlags);

  ~CircleImpl();

  template<typename ... Args>
  void log(LogLevel logLevel_, const char* filename, int lineno, Args&& ... args)
  {
    if (parent)
      parent->log(logLevel_, filename, lineno, std::forward<Args>(args) ...);
  }

  friend class Circle;
};

} // namespace internal
} // namespace circle

#endif /* CIRCLE_IMPL_H */

