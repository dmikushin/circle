#ifndef WORKER_H
#define WORKER_H

#include "lanl_circle.hpp"
#include "token.hpp"

#define CIRCLE_MPI_ERROR 32

namespace circle {

namespace internal {

class Worker {
  Circle* parent;

  /**
   * @brief Function that actually does work, calls user callback.
   *
   * This is the main body of execution.
   *
   * - For every work loop execution, the following happens:
   *     -# Check for work requests from other ranks.
   *     -# If this rank doesn't have work, ask a random rank for work.
   *     -# If this rank has work, call the user callback function.
   *     -# If after requesting work, this rank still doesn't have any,
   *        check for termination conditions.
   */
  void mainLoop(State *sptr);

public :

  Worker(Circle* parent);

  template<typename ... Args>
  void log(LogLevel logLevel_, const char* filename, int lineno, Args&& ... args)
  {
    parent->log(logLevel_, filename, lineno, std::forward<Args>(args) ...);
  }

  int execute();

  /**
   * Call this function to read in libcircle restart files.
   */
  int8_t readRestarts();

  /**
   * Call this function to read in libcircle restart files.  Each rank
   * writes a file called circle<rank>.txt
   */
  int8_t checkpoint();
};

void reset_request_vector(State *st);

/* provides address of pointer, and if value of pointer is not NULL,
 * frees memory and sets pointer value to NULL */
template <typename T> void free(T **pptr) {
  if (pptr != NULL) {
    if (*pptr != NULL) {
      ::free(*pptr);
      *pptr = NULL;
    }
  }

  return;
}

} // namespace internal

} // namespace circle

#endif /* WORKER_H */

