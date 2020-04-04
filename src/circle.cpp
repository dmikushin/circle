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

/** Communicator names **/
static char WORK_COMM_NAME[32] = "Libcircle Work Comm";

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
  return impl->logStream;
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

/**
 * Initialize a Circle instance for parallel processing.
 */
Circle::Circle(cb createCallback_, cb processCallback_,
  RuntimeFlags runtimeFlags_) :
  create_cb(createCallback_), process_cb(processCallback_),
  reduce_init_cb(nullptr), reduce_op_cb(nullptr), reduce_fini_cb(nullptr) {

  impl = new CircleImpl(this, runtimeFlags_);
}

/**
 * Initialize a Circle instance for parallel processing and reduction.
 */
Circle::Circle(circle::cb createCallback_, circle::cb processCallback_,
  circle::cb_reduce_init_fn reduceInitCallback_, circle::cb_reduce_op_fn reduceOperationCallback_,
  circle::cb_reduce_fini_fn reduceFinalizeCallback_,
  circle::RuntimeFlags runtimeFlags_) :
  create_cb(createCallback_), process_cb(processCallback_),
  reduce_init_cb(reduceInitCallback_), reduce_op_cb(reduceOperationCallback_),
  reduce_fini_cb(reduceFinalizeCallback_) {

  impl = new CircleImpl(this, runtimeFlags_);
}

Circle::~Circle() {
  delete impl;
}

int Circle::getRank() const { return impl->rank; }

/**
 * Change run time flags
 */
void Circle::setRuntimeFlags(RuntimeFlags runtimeFlags_) {
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

int Circle::enqueue(const std::string &element)
{
  std::vector<uint8_t> content(element.begin(), element.end());
  return enqueue(content);
}

/**
 * Wrapper for popping an element
 */
int Circle::dequeue(std::vector<uint8_t> &element)
{
  return impl->queue->pop(element);
}

int Circle::dequeue(std::string &element)
{
  std::vector<uint8_t> content;
  int result = dequeue(content);
  element.resize(content.size());
  std::copy(content.begin(), content.end(), element.begin());
  return result;
}

/**
 * Wrapper for getting the local queue size
 */
uint32_t Circle::localQueueSize() {
  return (uint32_t)impl->queue->count;
}

/**
 * Change the width of the k-ary communication tree.
 */
void Circle::setTreeWidth(int width) { impl->tree_width = width; }

/**
 * Change the number of seconds between consecutive reductions.
 */
void Circle::setReducePeriod(int secs) { impl->reduce_period = secs; }

/**
 * Call this function to give libcircle initial reduction data.
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

CircleImpl::CircleImpl(Circle* parent_, RuntimeFlags runtimeFlags_) :
  /* initialize reduction period to 0 seconds
   * to disable reductions by default */
  reduce_period(0), runtimeFlags(runtimeFlags_),
  logStream(stdout), reduce_buf(nullptr),
  logLevel(LogLevel::Fatal),
  rank(-1), parent(parent_) {
  queue = new Queue(parent);

  /* initialize width of communication tree */
  tree_width = 64;

  MPI_Comm_dup(MPI_COMM_WORLD, &comm);
  MPI_Comm_set_name(comm, WORK_COMM_NAME);
  MPI_Comm_rank(comm, &rank);
}

CircleImpl::~CircleImpl()
{
  delete queue;

  /* free buffer holding user reduction data */
  free(&reduce_buf);

  /* free off MPI resources and shut it down */
  MPI_Comm_free(&comm);
}

