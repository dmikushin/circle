#ifndef LANL_CIRCLE_H
#define LANL_CIRCLE_H

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

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
typedef void (*CallbackFunc)(circle::Circle *circle);

/**
 * Callbacks for initializing, executing, and obtaining final result
 * of a reduction
 */
typedef void (*reduceInitCallbackFunc)(circle::Circle *circle);
typedef void (*reduceOperationCallbackFunc)(circle::Circle *circle,
                                            const void *buf1, size_t size1,
                                            const void *buf2, size_t size2);
typedef void (*reduceFinalizeCallbackFunc)(circle::Circle *circle,
                                           const void *buf, size_t size);

namespace internal {

class CircleImpl;

} // namespace internal

class Circle {
  internal::CircleImpl *impl;

public:
  /**
   * Initialize a Circle instance for parallel processing.
   */
  Circle(circle::CallbackFunc createCallback,
         circle::CallbackFunc processCallback,
         circle::RuntimeFlags runtimeFlags);

  /**
   * Initialize a Circle instance for parallel processing and reduction.
   */
  Circle(circle::CallbackFunc createCallback,
         circle::CallbackFunc processCallback,
         circle::reduceInitCallbackFunc reduceInitCallback,
         circle::reduceOperationCallbackFunc reduceOperationCallback,
         circle::reduceFinalizeCallbackFunc reduceFinalizeCallback,
         circle::RuntimeFlags runtimeFlags);

  ~Circle();

  template <typename... Args>
  void log(LogLevel logLevel_, const char *filename, int lineno,
           Args &&... args) {
    if (logLevel_ > getLogLevel())
      return;

    const char *level = "NONE";
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
    fprintf(getLogStream(), std::forward<Args>(args)...);
#pragma GCC diagnostic pop
    fprintf(getLogStream(), "\n");
    fflush(getLogStream());
  }

  enum LogLevel getLogLevel() const;

  /**
   * Define the detail of logging that Circle should output.
   */
  void setLogLevel(enum LogLevel level);

  FILE *getLogStream() const;

  enum RuntimeFlags getRuntimeFlags() const;

  /**
   * Change run time flags.
   */
  void setRuntimeFlags(enum RuntimeFlags options);

  int getTreeWidth() const;

  /**
   * Change the width of the k-ary communication tree.
   */
  void setTreeWidth(int width);

  int getReducePeriod() const;

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

  friend class circle::internal::CircleImpl;
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
 * Returns an elapsed time on the calling processor for benchmarking purposes.
 */
double wtime(void);

} // namespace circle

#endif /* LANL_CIRCLE_H */
