#ifndef CIRCLE_IMPL_H
#define CIRCLE_IMPL_H

#include "lanl_circle.hpp"
#include "queue.hpp"

#include <mpi.h>

namespace circle {

namespace internal {

class CircleImpl {
  Circle *parent;

  circle::CallbackFunc createCallback;
  circle::CallbackFunc processCallback;

  circle::reduceInitCallbackFunc reduceInitCallback;
  circle::reduceOperationCallbackFunc reduceOperationCallback;
  circle::reduceFinalizeCallbackFunc reduceFinalizeCallback;

  MPI_Comm comm;
  MPI_Errhandler circle_err;

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

  Queue *queue;

public:
  /**
   * Initialize a Circle instance for parallel processing.
   */
  CircleImpl(Circle *parent, circle::CallbackFunc createCallback,
             circle::CallbackFunc processCallback,
             circle::RuntimeFlags runtimeFlags);

  /**
   * Initialize a Circle instance for parallel processing and reduction.
   */
  CircleImpl(Circle *parent, circle::CallbackFunc createCallback,
             circle::CallbackFunc processCallback,
             circle::reduceInitCallbackFunc reduceInitCallback,
             circle::reduceOperationCallbackFunc reduceOperationCallback,
             circle::reduceFinalizeCallbackFunc reduceFinalizeCallback,
             circle::RuntimeFlags runtimeFlags);

  ~CircleImpl();

  template <typename... Args>
  void log(LogLevel logLevel_, const char *filename, int lineno,
           Args &&... args) {
    if (parent)
      parent->log(logLevel_, filename, lineno, std::forward<Args>(args)...);
  }

  /**
   * Once you've defined and told Circle about your callbacks, use this to
   * execute your program.
   */
  void execute();

  /**
   * Call this function to read in libcircle restart files.
   */
  int8_t readRestarts();

  /**
   * Call this function to read in libcircle restart files.  Each rank
   * writes a file called circle<rank>.txt
   */
  int8_t checkpoint();

  friend class circle::Circle;
};

} // namespace internal
} // namespace circle

#endif /* CIRCLE_IMPL_H */
