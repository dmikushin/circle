#include <mpi.h>
#include <sigabrt.h>
#include <sigsegv.h>
#include <stdlib.h>

#include "circle.hpp"
#include "lanl_circle.hpp"
#include "log.hpp"
#include "token.hpp"

using namespace circle;
using namespace circle::internal;

/** Communicator names **/
static char WORK_COMM_NAME[32] = "Libcircle Work Comm";

namespace {

class GlobalInit {
  /** if we initialized MPI, remember that we need to finalize it */
  int must_finalize_mpi;

public:
  GlobalInit() : must_finalize_mpi(0) {
    SigAbrtHandler::enable();
    SigSegvHandler::enable();
  }

  ~GlobalInit() {
    /**
     * After your program has executed, give Circle a chance to clean up
     * after itself by calling this. This should be called after all Circle
     * API calls.
     */
    if (must_finalize_mpi) {
      /* finalize MPI if we initialized it */
      MPI_Finalize();
    }
  }

  int init(int *argc, char **argv[]) {
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
 * Initialize internal state needed by Circle. This should be called before
 * any other Circle API call.
 *
 * @param argc the number of arguments passed into the program.
 * @param argv the vector of arguments passed into the program.
 *
 * @return the rank value of the current process.
 */
int circle::init(int *argc, char **argv[]) {
  return globalInit.init(argc, argv);
}

/**
 * @brief Sets up Circle, calls work loop function
 *
 * - Main worker function. This function:
 *     -# Initializes MPI
 *     -# Initializes internal Circle data structures
 *     -# Calls Circle's main work loop function.
 *     -# Checkpoints if abort has been called by a rank.
 */
void CircleImpl::execute() {
  /* initialize all local state variables */
  State state(parent, processCallback, reduceInitCallback,
              reduceOperationCallback, reduceFinalizeCallback, comm, queue,
              reduce_buf, reduce_buf_size);

  /* print settings of some runtime tunables */
  if ((runtimeFlags & RuntimeFlags::SplitEqual) != RuntimeFlags::None) {
    LOG(LogLevel::Debug, "Using equalized load splitting.");
  }

  if ((runtimeFlags & RuntimeFlags::SplitRandom) != RuntimeFlags::None) {
    LOG(LogLevel::Debug, "Using randomized load splitting.");
  }

  if ((runtimeFlags & RuntimeFlags::CreateGlobal) != RuntimeFlags::None) {
    LOG(LogLevel::Debug, "Create callback enabled on all ranks.");
  } else {
    LOG(LogLevel::Debug, "Create callback enabled on rank 0 only.");
  }

  if ((runtimeFlags & RuntimeFlags::TermTree) != RuntimeFlags::None) {
    LOG(LogLevel::Debug, "Using tree termination detection.");
  } else {
    LOG(LogLevel::Debug, "Using circle termination detection.");
  }

  LOG(LogLevel::Debug, "Tree width: %d", tree_width);
  LOG(LogLevel::Debug, "Reduce period (secs): %d", reduce_period);

  /**********************************
   * this is where the heavy lifting is done
   **********************************/

  /* add initial work to queues by calling create_cb,
   * only invoke on master unless CREATE_GLOBAL is set */
  if (rank == 0 ||
      (runtimeFlags & RuntimeFlags::CreateGlobal) != RuntimeFlags::None) {
    parent->impl->createCallback(parent);
  }

  /* work until we get a terminate message */
  state.mainLoop();

  /**********************************
   * end work
   **********************************/

  /* optionally print summary info */
  if (parent->impl->logLevel >= LogLevel::Info) {
    state.printSummary();
  }
}

/**
 * Once you've defined and told Circle about your callbacks, use this to
 * execute your program.
 */
void Circle::execute() { impl->execute(); }

/**
 * Call this function to have all ranks dump a checkpoint file and exit.
 */
void Circle::abort(void) {
#if 0
  // TODO
  impl->queue->state->bcast_abort();
#endif
}

enum LogLevel Circle::getLogLevel() const { return impl->logLevel; }

FILE *Circle::getLogStream() const { return impl->logStream; }

/**
 * Set the logging level that Circle should use.
 *
 * @param level the logging level that Circle should output.
 */
void Circle::setLogLevel(enum LogLevel logLevel_) {
  impl->logLevel = logLevel_;
}

/**
 * Returns an elapsed time on the calling processor for benchmarking purposes.
 *
 * @return time in seconds since an arbitrary time in the past.
 */
double circle::wtime() { return MPI_Wtime(); }

/**
 * Initialize a Circle instance for parallel processing.
 */
Circle::Circle(circle::CallbackFunc createCallback,
               circle::CallbackFunc processCallback,
               RuntimeFlags runtimeFlags) {
  impl = new CircleImpl(this, createCallback, processCallback, runtimeFlags);
}

/**
 * Initialize a Circle instance for parallel processing and reduction.
 */
Circle::Circle(circle::CallbackFunc createCallback,
               circle::CallbackFunc processCallback,
               circle::CallbackFunc reduceInitCallback,
               circle::reduceOperationCallbackFunc reduceOperationCallback,
               circle::reduceFinalizeCallbackFunc reduceFinalizeCallback,
               circle::RuntimeFlags runtimeFlags) {
  impl = new CircleImpl(this, createCallback, processCallback,
                        reduceInitCallback, reduceOperationCallback,
                        reduceFinalizeCallback, runtimeFlags);
}

Circle::~Circle() { delete impl; }

int Circle::getRank() const { return impl->rank; }

enum RuntimeFlags Circle::getRuntimeFlags() const { return impl->runtimeFlags; }

/**
 * Change run time flags
 */
void Circle::setRuntimeFlags(enum RuntimeFlags runtimeFlags_) {
  impl->runtimeFlags = runtimeFlags_;
  LOG(LogLevel::Debug, "Circle options set: %X", impl->runtimeFlags);
}

/**
 * Wrapper for pushing an element on the queue
 *
 */
int Circle::enqueue(const std::vector<uint8_t> &element) {
  return impl->queue->push(element);
}

int Circle::enqueue(const std::string &element) {
  std::vector<uint8_t> content(element.begin(), element.end());
  return enqueue(content);
}

/**
 * Wrapper for popping an element
 */
int Circle::dequeue(std::vector<uint8_t> &element) {
  return impl->queue->pop(element);
}

int Circle::dequeue(std::string &element) {
  std::vector<uint8_t> content;
  int result = dequeue(content);
  element.resize(content.size());
  std::copy(content.begin(), content.end(), element.begin());
  return result;
}

/**
 * Wrapper for getting the local queue size
 */
uint32_t Circle::getLocalQueueSize() { return (uint32_t)impl->queue->count; }

int Circle::getTreeWidth() const { return impl->tree_width; }

/**
 * Change the width of the k-ary communication tree.
 */
void Circle::setTreeWidth(int width) { impl->tree_width = width; }

int Circle::getReducePeriod() const { return impl->reduce_period; }

/**
 * Change the number of seconds between consecutive reductions.
 */
void Circle::setReducePeriod(int secs) { impl->reduce_period = secs; }

/**
 * Call this function to give Circle initial reduction data.
 *
 * @param buf pointer to buffer holding reduction data
 * @param size size of buffer in bytes
 */
void Circle::reduce(const void *buf, size_t size) {
  /* free existing buffer memory if we have any */
  free(&impl->reduce_buf);

  /* allocate memory to copy reduction data */
  if (size > 0) {
    /* allocate memory */
    void *copy = malloc(size);

    if (copy == NULL) {
      LOG(LogLevel::Fatal,
          "Unable to allocate %llu bytes for reduction buffer.",
          (unsigned long long)size);
      /* TODO: bail with fatal error */
      return;
    }

    /* copy data from user buffer */
    memcpy(copy, buf, size);

    /* store buffer on input state */
    impl->reduce_buf = copy;
    impl->reduce_buf_size = size;
  }
}

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
 * Initialize a Circle instance for parallel processing.
 */
CircleImpl::CircleImpl(Circle *parent_, circle::CallbackFunc createCallback_,
                       circle::CallbackFunc processCallback_,
                       RuntimeFlags runtimeFlags_)
    : createCallback(createCallback_), processCallback(processCallback_),
      reduceInitCallback(nullptr), reduceOperationCallback(nullptr),
      reduceFinalizeCallback(nullptr),
      /* initialize reduction period to 0 seconds
       * to disable reductions by default */
      reduce_period(0), runtimeFlags(runtimeFlags_), logStream(stdout),
      reduce_buf(nullptr), logLevel(LogLevel::Fatal), rank(-1),
      parent(parent_) {
  queue = new Queue(parent);

  /* initialize width of communication tree */
  tree_width = 64;

  MPI_Comm_dup(MPI_COMM_WORLD, &comm);
  MPI_Comm_set_name(comm, WORK_COMM_NAME);
  MPI_Comm_rank(comm, &rank);

  /* setup an MPI error handler */
  MPI_Comm_create_errhandler(MPI_error_handler, &circle_err);
  MPI_Comm_set_errhandler(comm, circle_err);
}

/**
 * Initialize a Circle instance for parallel processing and reduction.
 */
CircleImpl::CircleImpl(
    Circle *parent_, circle::CallbackFunc createCallback_,
    circle::CallbackFunc processCallback_,
    circle::CallbackFunc reduceInitCallback_,
    circle::reduceOperationCallbackFunc reduceOperationCallback_,
    circle::reduceFinalizeCallbackFunc reduceFinalizeCallback_,
    circle::RuntimeFlags runtimeFlags_)
    : createCallback(createCallback_), processCallback(processCallback_),
      reduceInitCallback(reduceInitCallback_),
      reduceOperationCallback(reduceOperationCallback_),
      reduceFinalizeCallback(reduceFinalizeCallback_),
      /* initialize reduction period to 0 seconds
       * to disable reductions by default */
      reduce_period(0), runtimeFlags(runtimeFlags_), logStream(stdout),
      reduce_buf(nullptr), logLevel(LogLevel::Fatal), rank(-1),
      parent(parent_) {
  queue = new Queue(parent);

  /* initialize width of communication tree */
  tree_width = 64;

  MPI_Comm_dup(MPI_COMM_WORLD, &comm);
  MPI_Comm_set_name(comm, WORK_COMM_NAME);
  MPI_Comm_rank(comm, &rank);

  /* setup an MPI error handler */
  MPI_Comm_create_errhandler(MPI_error_handler, &circle_err);
  MPI_Comm_set_errhandler(comm, circle_err);
}

CircleImpl::~CircleImpl() {
  delete queue;

  /* free buffer holding user reduction data */
  free(&reduce_buf);

  /* restore original error handler and free our custom one */
  MPI_Comm_set_errhandler(comm, MPI_ERRORS_ARE_FATAL);
  MPI_Errhandler_free(&circle_err);

  /* free off MPI resources and shut it down */
  MPI_Comm_free(&comm);
}

/**
 * Call this function to read in Circle restart files.
 */
int8_t Circle::readRestarts() { return impl->readRestarts(); }

/**
 * Call this function to read in Circle restart files.  Each rank
 * writes a file called circle<rank>.txt
 */
int8_t Circle::checkpoint() { return impl->checkpoint(); }

CircleImpl *Circle::getImpl() { return impl; }

/**
 * Call this function to read in Circle restart files.
 */
int8_t CircleImpl::readRestarts() { return queue->read(parent->getRank()); }

/**
 * Call this function to read in Circle restart files.  Each rank
 * writes a file called circle<rank>.txt
 */
int8_t CircleImpl::checkpoint() { return queue->write(parent->getRank()); }
