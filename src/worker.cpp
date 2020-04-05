/**
 * @file
 *
 * The abstraction of a worker process.
 */

#include <arpa/inet.h>
#include <dirent.h>
#include <inttypes.h>
#include <mpi.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "lanl_circle.hpp"
#include "circle_impl.hpp"
#include "log.hpp"
#include "token.hpp"
#include "worker.hpp"

namespace circle {
namespace internal {

int8_t ABORT_FLAG;

} // namespace impl
} // namespace circle

using namespace circle;
using namespace circle::internal;

/*
 * Define as described in gethostent(3) for backwards compatibility.
 */
#ifndef h_addr
#define h_addr h_addr_list[0]
#endif /* h_addr */

/**
 * @brief Function to be called in the event of an MPI error.
 *
 * This function get registered with MPI to be called
 * in the event of an MPI Error.  It attempts
 * to checkpoint.
 */
#pragma GCC diagnostic ignored "-Wunused-parameter"
static void MPI_error_handler(MPI_Comm *comm, int *err, ...) {
  const char *bt = backtrace(1);
#if 0
  // TODO Attach circle to communicator with MPI_Comm_create_keyval
  char name[MPI_MAX_OBJECT_NAME];
  int namelen;
  MPI_Comm_get_name(*comm, name, &namelen);

  if (*err == CIRCLE_MPI_ERROR) {
    LOG(LogLevel::Error, "Libcircle received abort signal, checkpointing.");
  } else {
    char error[MPI_MAX_ERROR_STRING];
    int error_len = 0;
    MPI_Error_string(*err, error, &error_len);
    LOG(LogLevel::Error, "MPI Error in Comm [%s]: %s", name, error);
    LOG(LogLevel::Error, "Backtrace:\n%s\n", bt);
    LOG(LogLevel::Error, "Libcircle received MPI error, checkpointing.");
  }
  
  checkpoint();
#endif
  abort();
}
#pragma GCC diagnostic warning "-Wunused-parameter"

/**
 * Call this function to read in libcircle restart files.
 */
int8_t Worker::readRestarts() {
  return parent->impl->queue->read(parent->getRank());
}

/**
 * Call this function to read in libcircle restart files.  Each rank
 * writes a file called circle<rank>.txt
 */
int8_t Worker::checkpoint() {
  return parent->impl->queue->write(parent->getRank());
}

Worker::Worker(Circle* parent_) : parent(parent_) { }

/**
 * @brief Sets up libcircle, calls work loop function
 *
 * - Main worker function. This function:
 *     -# Initializes MPI
 *     -# Initializes internal libcircle data structures
 *     -# Calls libcircle's main work loop function.
 *     -# Checkpoints if abort has been called by a rank.
 */
int Worker::execute() {
  /* initialize all local state variables */
  State state(parent);

  /* get MPI communicator */
  MPI_Comm& comm = parent->impl->comm;

  /* get our rank and the size of the communicator */
  int rank, size;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &size);

  /* setup an MPI error handler */
  MPI_Errhandler circle_err;
  MPI_Comm_create_errhandler(MPI_error_handler, &circle_err);
  MPI_Comm_set_errhandler(comm, circle_err);

  /* print settings of some runtime tunables */
  if ((parent->impl->runtimeFlags & RuntimeFlags::SplitEqual) !=
      RuntimeFlags::None) {
    LOG(LogLevel::Debug, "Using equalized load splitting.");
  }

  if ((parent->impl->runtimeFlags & RuntimeFlags::SplitRandom) !=
      RuntimeFlags::None) {
    LOG(LogLevel::Debug, "Using randomized load splitting.");
  }

  if ((parent->impl->runtimeFlags & RuntimeFlags::CreateGlobal) !=
      RuntimeFlags::None) {
    LOG(LogLevel::Debug, "Create callback enabled on all ranks.");
  } else {
    LOG(LogLevel::Debug, "Create callback enabled on rank 0 only.");
  }

  if ((parent->impl->runtimeFlags & RuntimeFlags::TermTree) !=
      RuntimeFlags::None) {
    LOG(LogLevel::Debug, "Using tree termination detection.");
  } else {
    LOG(LogLevel::Debug, "Using circle termination detection.");
  }

  LOG(LogLevel::Debug, "Tree width: %d", parent->impl->tree_width);
  LOG(LogLevel::Debug, "Reduce period (secs): %d", parent->impl->reduce_period);

  /**********************************
   * this is where the heavy lifting is done
   **********************************/

  /* add initial work to queues by calling create_cb,
   * only invoke on master unless CREATE_GLOBAL is set */
  if (rank == 0 || (parent->impl->runtimeFlags & RuntimeFlags::CreateGlobal) !=
                       RuntimeFlags::None) {
    (*(parent->create_cb))(parent);
  }

  /* work until we get a terminate message */
  state.mainLoop();

  /* we may have dropped out early from an abort signal,
   * in which case we should checkpoint here */
  if (ABORT_FLAG) {
    checkpoint();
  }

  /**********************************
   * end work
   **********************************/

  /* optionally print summary info */
  if (parent->impl->logLevel >= LogLevel::Info) {
    state.printSummary();
  }

  /* restore original error handler and free our custom one */
  MPI_Comm_set_errhandler(comm, MPI_ERRORS_ARE_FATAL);
  MPI_Errhandler_free(&circle_err);

  return 0;
}

