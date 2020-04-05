#ifndef LANL_CIRCLE_H
#define LANL_CIRCLE_H

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

/**
 * The maximum length of a string value which is allowed to be placed on the
 * queue structure.
 */
#ifdef PATH_MAX
#define CIRCLE_MAX_STRING_LEN PATH_MAX
#else
#define CIRCLE_MAX_STRING_LEN (4096)
#endif

namespace circle {

/**
 * Run time flags for the behavior of splitting work.
 */
enum class RuntimeFlags : unsigned {
  None = 0,
  SplitRandom = 1 << 0,      /* Split work randomly. */
  SplitEqual = 1 << 1,       /* Split work evenly */
  CreateGlobal = 1 << 2,     /* Call create callback on all procs */
  TermTree = 1 << 3,         /* Use tree-based termination */
  DefaultFlags = SplitEqual, /* Default behavior is random work stealing */
};

inline constexpr RuntimeFlags operator&(RuntimeFlags x, RuntimeFlags y) {
  return static_cast<RuntimeFlags>(static_cast<unsigned>(x) &
                                   static_cast<unsigned>(y));
}

inline constexpr RuntimeFlags operator|(RuntimeFlags x, RuntimeFlags y) {
  return static_cast<RuntimeFlags>(static_cast<unsigned>(x) |
                                   static_cast<unsigned>(y));
}

/**
 * The various logging levels that Circle will output.
 */
enum class LogLevel : unsigned {
  None = 0,
  Fatal = 1,
  Error = 2,
  Warning = 3,
  Info = 4,
  Debug = 5
};

class Circle;

/**
 * The type for defining callbacks for create and process.
 */
typedef void (*cb)(circle::Circle *circle);

/**
 * Callbacks for initializing, executing, and obtaining final result
 * of a reduction
 */
typedef void (*cb_reduce_init_fn)(circle::Circle *circle);
typedef void (*cb_reduce_op_fn)(circle::Circle *circle,
		                const void *buf1, size_t size1,
                                const void *buf2, size_t size2);
typedef void (*cb_reduce_fini_fn)(circle::Circle *circle,
		                  const void *buf, size_t size);

namespace internal {

class CircleImpl;

} // namespace internal

class Circle {

public :

  // TODO std::function
  circle::cb create_cb;
  circle::cb process_cb;

  circle::cb_reduce_init_fn reduce_init_cb;
  circle::cb_reduce_op_fn reduce_op_cb;
  circle::cb_reduce_fini_fn reduce_fini_cb;

  internal::CircleImpl* impl;

public :

  /**
   * Initialize a Circle instance for parallel processing.
   */
  Circle(circle::cb createCallback, circle::cb processCallback, circle::RuntimeFlags runtimeFlags);

  /**
   * Initialize a Circle instance for parallel processing and reduction.
   */
  Circle(circle::cb createCallback, circle::cb processCallback,
         circle::cb_reduce_init_fn reduceInitCallback, circle::cb_reduce_op_fn reduceOperationCallback,
	 circle::cb_reduce_fini_fn reduceFinalizeCallback,
         circle::RuntimeFlags runtimeFlags);

  ~Circle();

  template<typename ... Args>
  void log(LogLevel logLevel_, const char* filename, int lineno, Args&& ... args) 
  {
    if (logLevel_ > getLogLevel()) return;

    const char* level = "NONE";
    if (logLevel_ == LogLevel::Fatal)
      level = "FATL";
    else if (logLevel_ == LogLevel::Error)
      level = "ERRO";
    else if (logLevel_ == LogLevel::Warning)
      level = "WARN";
    else if (logLevel_ == LogLevel::Info)
      level = "INFO";
    else if (logLevel_ == LogLevel::Debug)
      level = "DEBG";

    fprintf(getLogStream(), "[%s] %d:%d:%s:%d: ", level, (int)time(NULL),
            getRank(), filename, lineno);
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wformat-security"
    fprintf(getLogStream(), std::forward<Args>(args) ...);
    #pragma GCC diagnostic pop
    fprintf(getLogStream(), "\n");                               
    fflush(getLogStream());                                      
  }

  enum LogLevel getLogLevel() const;

  /**
   * Define the detail of logging that Circle should output.
   */
  void setLogLevel(enum LogLevel level);

  FILE* getLogStream() const;

  enum RuntimeFlags getRuntimeFlags() const;

  /**
   * Change run time flags.
   */
  void setRuntimeFlags(enum RuntimeFlags options);

  /**
   * Change the width of the k-ary communication tree.
   */
  void setTreeWidth(int width);

  /**
   * Change the number of seconds between consecutive reductions.
   */
  void setReducePeriod(int secs);

  /**
   * Get an MPI rank corresponding to the current process.
   */
  int getRank() const;

  void reduce(const void *buf, size_t size);

  /**
   * Once you've defined and told Circle about your callbacks, use this to
   * execute your program.
   */
  void execute();

  /**
   * Call this function to have all ranks dump a checkpoint file and exit.
   */
  void abort(void);

  /**
   * Call this function to read in libcircle restart files.
   */
  int8_t readRestarts();

  /**
   * Call this function to read in libcircle restart files.  Each rank
   * writes a file called circle<rank>.txt
   */
  int8_t checkpoint();

  /**
   * The interface to the work queue. This can be accessed from within the
   * process and create work callbacks.
   */
  int enqueue(const std::vector<uint8_t> &element);
  int enqueue(const std::string &element);

  int dequeue(std::vector<uint8_t> &element);
  int dequeue(std::string &element);

  uint32_t localQueueSize();
};

/**
 *  Produce a stack trace with demangled function and method names.
 */
const char *backtrace(int skip);

/**
 * Initialize internal state needed by Circle. This should be called before
 * any other Circle API call. This returns the MPI rank value.
 */
int init(int *argc, char **argv[]);

/**
 * Processing and creating work is done through callbacks. Here's how we tell
 * Circle about our function which creates work. This call is optional.
 */
void cb_create(circle::cb func);

/**
 * After you give Circle a way to create work, you need to tell it how that
 * work should be processed.
 */
void cb_process(circle::cb func);

/**
 * Specify function that Circle should call to get initial data for
 * a reduction.
 */
void cb_reduce_init(circle::cb_reduce_init_fn);

/**
 * Specify function that Circle should call to execute a reduction
 * operation.
 */
void cb_reduce_op(circle::cb_reduce_op_fn);

/**
 * Specify function that libcicle should invoke at end of reduction.
 * This function is only invoked on rank 0.
 */
void cb_reduce_fini(circle::cb_reduce_fini_fn);

/**
 * Provide Circle with initial reduction data during initial
 * and intermediate reduction callbacks, Circle makes a copy
 * of the data so the user buffer can be immediately released.
 */
void reduce(const void *buf, size_t size);

/**
 * Call this function to checkpoint Circle's distributed queue. Each rank
 * writes a file called circle<rank>.txt
 */
int8_t checkpoint(void);

/**
 * Call this function to initialize Circle queues from restart files
 * created by checkpoint.
 */
int8_t read_restarts(void);

/**
 * Returns an elapsed time on the calling processor for benchmarking purposes.
 */
double wtime(void);

} // namespace circle

#endif /* LANL_CIRCLE_H */
