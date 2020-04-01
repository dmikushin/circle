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

/** The debug stream for all logging messages. */
FILE *circle::debug_stream;

/** The current log level of library logging output. */
enum circle::LogLevel circle::debug_level;

/** The rank value of the current node. */
int32_t circle::global_rank;

/** if we initialized MPI, remember that we need to finalize it */
static int must_finalize_mpi;

/** Communicator names **/
static char WORK_COMM_NAME[32] = "Libcircle Work Comm";
static char TOKEN_COMM_NAME[32] = "Libcircle Token Comm";

/** A struct which holds a reference to all input given through the API. */
namespace circle {
namespace impl {

circle::Circle circle;

} // namespace impl
} // namespace circle

using namespace circle::impl;

/** Handle to the queue */
extern circle::WorkQueue queue_handle;

circle::WorkQueue *circle::get_handle() { return &queue_handle; }

/**
 * Initialize internal state needed by libcircle. This should be called before
 * any other libcircle API call.
 *
 * @param argc the number of arguments passed into the program.
 * @param argv the vector of arguments passed into the program.
 *
 * @return the rank value of the current process.
 */
int32_t circle::init(int argc, char *argv[],
                     circle::RuntimeFlags runtimeFlags) {
  circle::debug_stream = stdout;
  circle::debug_level = circle::LogLevel::Fatal;

  memset(&circle::impl::circle, 0, sizeof(circle::impl::circle));

  /* initialize reduction period to 0 seconds
   * to disable reductions by default */
  circle::impl::circle.reduce_period = 0;

  circle::impl::circle.impl = new circle::internal::CircleImpl();

  /* initialize width of communication tree */
  circle::impl::circle.impl->tree_width = 64;

  circle::impl::circle.setRuntimeFlags(runtimeFlags);

  /* determine whether we need to initialize MPI,
   * and remember if we did so we finalize later */
  must_finalize_mpi = 0;
  int mpi_initialized;

  if (MPI_Initialized(&mpi_initialized) != MPI_SUCCESS) {
    LOG(circle::LogLevel::Fatal, "Unable to initialize MPI.");
    return -1;
  }

  if (!mpi_initialized) {
    /* not already initialized, so intialize MPI now */
    if (MPI_Init(&argc, &argv) != MPI_SUCCESS) {
      LOG(circle::LogLevel::Fatal, "Unable to initialize MPI.");
      return -1;
    }

    /* remember that we must finalize later */
    must_finalize_mpi = 1;
  }

  MPI_Comm_dup(MPI_COMM_WORLD, &circle::impl::circle.impl->comm);
  MPI_Comm_set_name(circle::impl::circle.impl->comm, WORK_COMM_NAME);
  MPI_Comm_rank(circle::impl::circle.impl->comm, &circle::global_rank);

  circle::impl::circle.impl->queue = circle::internal_queue_init();

  if (circle::impl::circle.impl->queue == NULL) {
    return -1;
  } else {
    return circle::global_rank;
  }
}

/**
 * Processing and creating work is done through callbacks. Here's how we tell
 * libcircle about our function which creates an initial workload. This call
 * is optional.
 *
 * @param func the callback to be used in the creation stage.
 */
void circle::cb_create(circle::cb func) { circle::impl::circle.create_cb = func; }

/**
 * Change run time flags
 */
void circle::Circle::setRuntimeFlags(circle::RuntimeFlags runtimeFlags_) {
  runtimeFlags = runtimeFlags_;
  LOG(circle::LogLevel::Debug, "Circle options set: %X", runtimeFlags);
}

/**
 * Change the width of the k-ary communication tree.
 */
void circle::set_tree_width(int width) { circle::impl::circle.impl->tree_width = width; }

/**
 * Change the number of seconds between consecutive reductions.
 */
void circle::set_reduce_period(int secs) { circle::impl::circle.reduce_period = secs; }

/**
 * After you give libcircle a way to create work, you need to tell it how that
 * work should be processed.
 *
 * @param func the callback to be used in the process stage.
 */
void circle::cb_process(circle::cb func) {
  if (circle::impl::circle.create_cb == NULL) {
    circle::impl::circle.create_cb = func;
  }

  circle::impl::circle.process_cb = func;
}

/**
 * This function will be invoked on all processes to get initial input
 * data for the reduction.
 *
 * @param func the callback to be used to provide data for reduction.
 */
void circle::cb_reduce_init(circle::cb_reduce_init_fn func) {
  circle::impl::circle.reduce_init_cb = func;
}

/**
 * This function will be invoked on processes to execute the reduction
 * tree.
 *
 * @param func the callback to be used to combine data during reduction.
 */
void circle::cb_reduce_op(circle::cb_reduce_op_fn func) {
  circle::impl::circle.reduce_op_cb = func;
}

/**
 * This function will be invoked on the root (rank 0) to provide the
 * final result of the reduction.
 *
 * @param func the callback to be provide reduction output on root.
 */
void circle::cb_reduce_fini(circle::cb_reduce_fini_fn func) {
  circle::impl::circle.reduce_fini_cb = func;
}

/**
 * Call this function to give libcircle initial reduction data.
 *
 * @param buf pointer to buffer holding reduction data
 * @param size size of buffer in bytes
 */
void circle::reduce(const void *buf, size_t size) {
  /* free existing buffer memory if we have any */
  circle::free(&circle::impl::circle.reduce_buf);

  /* allocate memory to copy reduction data */
  if (size > 0) {
    /* allocate memory */
    void *copy = malloc(size);

    if (copy == NULL) {
      LOG(circle::LogLevel::Fatal,
          "Unable to allocate %llu bytes for reduction buffer.",
          (unsigned long long)size);
      /* TODO: bail with fatal error */
      return;
    }

    /* copy data from user buffer */
    memcpy(copy, buf, size);

    /* store buffer on input state */
    circle::impl::circle.reduce_buf = copy;
    circle::impl::circle.reduce_buf_size = size;
  }
}

/**
 * Once you've defined and told libcircle about your callbacks, use this to
 * execute your program.
 */
void circle::begin(void) { circle::worker(); }

/**
 * Call this function to have all ranks dump a checkpoint file and exit.
 */
void circle::abort(void) { circle::bcast_abort(); }

/**
 * After your program has executed, give libcircle a chance to clean up after
 * itself by calling this. This should be called after all libcircle API calls.
 */
void circle::finalize(void) {
  circle::internal_queue_free(circle::impl::circle.impl->queue);

  /* free buffer holding user reduction data */
  circle::free(&circle::impl::circle.reduce_buf);

  /* free off MPI resources and shut it down */
  MPI_Comm_free(&circle::impl::circle.impl->comm);

  if (must_finalize_mpi) {
    /* finalize MPI if we initialized it */
    MPI_Finalize();
  }

  circle::debug_stream = NULL;
}

/**
 * Set the logging level that libcircle should use.
 *
 * @param level the logging level that libcircle should output.
 */
void circle::enable_logging(enum circle::LogLevel level) {
  circle::debug_level = level;
}

/**
 * Returns an elapsed time on the calling processor for benchmarking purposes.
 *
 * @return time in seconds since an arbitrary time in the past.
 */
double circle::wtime(void) { return MPI_Wtime(); }

/* EOF */
