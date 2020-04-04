/**
 * @file
 * The library source contains the internal implementation of each API hook.
 */

#include <mpi.h>
#include <stdlib.h>
#include <sigabrt.h>
#include <sigsegv.h>

#include "lanl_circle.hpp"
#include "circle_impl.hpp"
#include "log.hpp"
#include "token.hpp"
#include "worker.hpp"

using namespace circle;
using namespace circle::internal;

namespace {

class GlobalInit
{
  /** if we initialized MPI, remember that we need to finalize it */
  int must_finalize_mpi;

public :
  GlobalInit() : must_finalize_mpi(0) {
    SigAbrtHandler::enable();
    SigSegvHandler::enable();
  }

  ~GlobalInit() {
    /**
     * After your program has executed, give libcircle a chance to clean up after
     * itself by calling this. This should be called after all libcircle API calls.
     */
    if (must_finalize_mpi) {
      /* finalize MPI if we initialized it */
      MPI_Finalize();
    }
  }

  int init(int* argc, char **argv[]) {
    /* determine whether we need to initialize MPI,
     * and remember if we did so we finalize later */
    must_finalize_mpi = 0;
    int mpi_initialized;

    if (MPI_Initialized(&mpi_initialized) != MPI_SUCCESS) {
      // TODO LOG(LogLevel::Fatal, "Unable to initialize MPI.");
      return -1;
    }

    if (mpi_initialized)
      return 0;

    /* not already initialized, so intialize MPI now */
    if (MPI_Init(argc, argv) != MPI_SUCCESS) {
      // TODO LOG(LogLevel::Fatal, "Unable to initialize MPI.");
      return -1;
    }

    /* remember that we must finalize later */
    must_finalize_mpi = 1;

    return 0;
  }
};

} // namespace

/** Global initialization object that shall automatically destruct on exit. */
static GlobalInit globalInit;

/**
 * Initialize internal state needed by libcircle. This should be called before
 * any other libcircle API call.
 *
 * @param argc the number of arguments passed into the program.
 * @param argv the vector of arguments passed into the program.
 *
 * @return the rank value of the current process.
 */
int circle::init(int* argc, char **argv[]) {
  return globalInit.init(argc, argv);
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
  impl->queue->state->bcast_abort();
#endif
}

enum LogLevel Circle::getLogLevel() const {
  return impl->logLevel;
}

FILE* Circle::getLogStream() const {
  return impl->debugStream;
}

/**
 * Set the logging level that libcircle should use.
 *
 * @param level the logging level that libcircle should output.
 */
void Circle::setLogLevel(enum LogLevel logLevel_) {
  impl->logLevel = logLevel_;
}

/**
 * Returns an elapsed time on the calling processor for benchmarking purposes.
 *
 * @return time in seconds since an arbitrary time in the past.
 */
double wtime(void) { return MPI_Wtime(); }

