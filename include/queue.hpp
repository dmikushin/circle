#ifndef INTERNAL_QUEUE_H
#define INTERNAL_QUEUE_H

#include <stdint.h>

/* The initial queue size for malloc. */
#ifndef CIRCLE_INITIAL_INTERNAL_QUEUE_SIZE
#define CIRCLE_INITIAL_INTERNAL_QUEUE_SIZE 4096
#endif

namespace circle {

class Circle;

namespace internal {

class CircleImpl;

struct Queue {
  std::vector<char> base;         /* Base of the memory pool */
  uintptr_t head;                 /* The location of the next free byte */
  std::vector<uintptr_t> strings; /* The string data */
  int count;                      /* The number of actively queued strings */

  Circle *parent;

public:
  Queue(Circle *parent);

  template <typename... Args>
  void log(LogLevel logLevel_, const char *filename, int lineno,
           Args &&... args) {
    if (parent)
      parent->log(logLevel_, filename, lineno, std::forward<Args>(args)...);
  }

  int getCount() const;

  int8_t push(const std::vector<uint8_t> &content);
  int8_t push(const std::string &content);

  int8_t pop(std::vector<uint8_t> &content);
  int8_t pop(std::string &content);

  /**
   * Get the size of last queue item without removing it.
   */
  size_t lastSize();

  void dump();
  void print();

  int8_t write(int rank);
  int8_t read(int rank);
  int8_t extend(size_t size);
  int8_t extendStr(size_t count);

  friend class Circle;
  friend class CircleImpl;
};

} // namespace internal

} // namespace circle

#endif /* QUEUE_H */
