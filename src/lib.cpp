/**
 * @file
 * The library source contains the internal implementation of each API hook.
 */

#include <mpi.h>
#include <stdlib.h>

#include "lanl_circle.hpp"
#include "circle_impl.hpp"
#include "log.hpp"
#include "token.hpp"
#include "worker.hpp"

using namespace circle;
using namespace circle::internal;

/** if we initialized MPI, remember that we need to finalize it */
static int must_finalize_mpi;

/**
 * Initialize internal state needed by libcircle. This should be called before
 * any other libcircle API call.
 *
 * @param argc the number of arguments passed into the program.
 * @param argv the vector of arguments passed into the program.
 *
 * @return the rank value of the current process.
 */
int circle::init(int argc, char *argv[]) {
  /* determine whether we need to initialize MPI,
   * and remember if we did so we finalize later */
  must_finalize_mpi = 0;
  int mpi_initialized;

  if (MPI_Initialized(&mpi_initialized) != MPI_SUCCESS) {
    // TODO LOG(LogLevel::Fatal, "Unable to initialize MPI.");
    return -1;
  }

  if (!mpi_initialized) {
    /* not already initialized, so intialize MPI now */
    if (MPI_Init(&argc, &argv) != MPI_SUCCESS) {
      // TODO LOG(LogLevel::Fatal, "Unable to initialize MPI.");
      return -1;
    }

    /* remember that we must finalize later */
    must_finalize_mpi = 1;
  }

  return 0;
}

/**
 * Once you've defined and told libcircle about your callbacks, use this to
 * execute your program.
 */
void Circle::execute() { 
  Worker worker(this);
  worker.execute(); 
}

/**
 * Call this function to have all ranks dump a checkpoint file and exit.
 */
void Circle::abort(void) {
#if 0
  // TODO
  impl->queue.state->bcast_abort();
#endif
}

/**
 * After your program has executed, give libcircle a chance to clean up after
 * itself by calling this. This should be called after all libcircle API calls.
 */
void circle::finalize(void) {
  if (must_finalize_mpi) {
    /* finalize MPI if we initialized it */
    MPI_Finalize();
  }
}

/**
 * Set the logging level that libcircle should use.
 *
 * @param level the logging level that libcircle should output.
 */
void Circle::enableLogging(enum LogLevel logLevel_) {
  logLevel = logLevel_;
}

/**
 * Returns an elapsed time on the calling processor for benchmarking purposes.
 *
 * @return time in seconds since an arbitrary time in the past.
 */
double wtime(void) { return MPI_Wtime(); }

