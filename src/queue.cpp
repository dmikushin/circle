/**
 * @file
 * This file contains functions related to the local queue structure.
 */

#include <fstream>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <unistd.h>

#include "lanl_circle.hpp"
#include "circle.hpp"
#include "log.hpp"
#include "queue.hpp"

using namespace circle;
using namespace circle::internal;

/**
 * Allocate memory for the basic queue structure used by libcircle.
 *
 * @return a reference to the allocated queue structure.
 *
 * @see Queue::free
 */
Queue::Queue(Circle* parent_) : parent(parent_), count(0), head(0) { }

int Queue::getCount() const {
  return count;
}

/**
 * Dump the raw contents of the queue structure to logging.
 *
 * @param qp the queue structure that should be dumped.
 */
void Queue::dump() {
  uint32_t i = 0;
  char *p = &base[0];

  while (p++ < (&base[0] + base.size())) {
    if (i++ % 120 == 0) {
      LOG(LogLevel::Debug, "%c", *p);
    } else {
      LOG(LogLevel::Debug, "%c", *p);
    }
  }
}

/**
 * Pretty-print the queue data structure.
 *
 * @param qp the queue structure that should be pretty-printed.
 */
void Queue::print() {
  int32_t i = 0;

  for (i = 0; i < count; i++) {
    LOG(LogLevel::Debug, "\t[%p][%d] %s", &base[0] + strings[i], i,
        &base[0] + strings[i]);
  }
}
/**
 * Extend the string array size.
 *
 */
int8_t Queue::extendStr(size_t amount) {
  /* TODO: check for overflow */
  auto newSize = strings.size();
  while (newSize < amount) {
    newSize += 4096 / sizeof(strings[0]);
  }

  auto oldSize = strings.size();
  strings.resize(newSize);

  LOG(LogLevel::Debug,
      "Reallocing string array from"
      " [%zu] to [%zu] [%p] -> [%p].",
      (size_t)oldSize, (size_t)newSize,
      &strings[0], &strings[newSize]);

  return 0;
}

/**
 * Extend the circle queue size
 *
 */
int8_t Queue::extend(size_t amount) {
  /* TODO: check for overflow */
  auto newSize = base.size();
  while (newSize < amount) {
    newSize += ((size_t)sysconf(_SC_PAGESIZE)) * 4096;
  }

  auto oldSize = base.size();
  base.resize(newSize);

  LOG(LogLevel::Debug,
      "Reallocing queue from"
      " [%zu] to [%zu] [%p] -> [%p].",
      (size_t)oldSize, (size_t)newSize,
      &base[0], &base[newSize]);

  return 0;
}

/**
 * Push the specified content onto the queue structure.
 *
 * @param qp the queue structure to push the value onto.
 * @param str the string value to push onto the queue->
 *
 * @return a positive number on success, a negative one on failure.
 */
int8_t Queue::push(const std::vector<uint8_t> &content) {
  /* TODO: check that real_len fits within uint32_t */
  size_t real_len = content.size();
  uint32_t len = (uint32_t)real_len;

  if (len <= 0) {
    LOG(LogLevel::Error, "Attempted to push an empty string onto a queue->");
    return -1;
  }

  if (len > CIRCLE_MAX_STRING_LEN) {
    LOG(LogLevel::Error,
        "Attempted to push a value that was larger than expected.");
    return -1;
  }

  if (count + 1 > strings.size()) {
    LOG(LogLevel::Debug, "Extending string array by 4096.");

    if (extendStr(count + 1) < 0) {
      return -1;
    }
  }

  size_t new_bytes = (size_t)(head + len) * sizeof(char);

  if (new_bytes > base.size()) {
    LOG(LogLevel::Debug, "The queue is not large enough to add another value.");

    if (extend(new_bytes) < 0) {
      return -1;
    }
  }

  /* Set our write location to the end of the current strings array. */
  strings[count] = head;

  /* Copy the string. */
  memcpy(&base[0] + head, reinterpret_cast<const char *>(&content[0]), len);

  /*
   * Make head point to the character after the string (strlen doesn't
   * include a trailing null).
   */
  head += len;

  /* Make the head point to the next available memory */
  count++;

  return 0;
}

int8_t Queue::push(const std::string &element)
{ 
  std::vector<uint8_t> content(element.begin(), element.end());
  return push(content);
}

/**
 * Removes an item from the queue and returns a copy.
 *
 * @param qp the queue structure to remove the item from.
 * @param str a reference to the value removed.
 *
 * @return a positive value on success, a negative one otherwise.
 */
int8_t Queue::pop(std::vector<uint8_t> &content) {
  if (count < 1) {
    LOG(LogLevel::Debug, "Attempted to pop from an empty queue->");
    return -1;
  }

  /* Copy last element into str */
  uintptr_t current = strings[count - 1];
  size_t len = head - current;
  content.resize(len);
  memcpy(reinterpret_cast<uint8_t *>(&content[0]), &base[0] + current, len);
  head = current;
  count--;

  return 0;
}

int8_t Queue::pop(std::string &element)
{
  std::vector<uint8_t> content;
  int result = pop(content);
  element.resize(content.size());
  std::copy(content.begin(), content.end(), element.begin());
  return result;
}

/**
 * Read a queue checkpoint file into working memory.
 *
 * @param qp the queue structure to read the checkpoint file into.
 * @param rank the node which holds the checkpoint file.
 *
 * @return a positive value on success, a negative one otherwise.
 */
int8_t Queue::read(int rank) {
  LOG(LogLevel::Debug, "Reading from checkpoint file %d.", rank);

  if (count != 0) {
    LOG(LogLevel::Warning,
        "Reading items from checkpoint file into non-empty work queue->");
  }

  char filename[256];
  sprintf(filename, "circle%d.txt", rank);

  LOG(LogLevel::Debug, "Attempting to open %s.", filename);

  std::ifstream checkpoint_file(filename);

  if (!checkpoint_file.is_open()) {
    LOG(LogLevel::Error, "Unable to open checkpoint file %s", filename);
    return -1;
  }

  LOG(LogLevel::Debug, "Checkpoint file opened.");

  std::string str;
  while (std::getline(checkpoint_file, str))
  {
    std::vector<uint8_t> content(str.begin(), str.end());
    if (push(content) < 0) {
      LOG(LogLevel::Error, "Failed to push element on queue \"%s\"", str);
    }

    LOG(LogLevel::Debug, "Pushed %s onto queue->", str);
  }

  checkpoint_file.close();
  return 0;
}

/**
 * Write out the queue structure to a checkpoint file.
 *
 * @param qp the queue structure to be written to the checkpoint file.
 * @param rank the node which is writing out the checkpoint file.
 *
 * @return a positive value on success, negative otherwise.
 */
int8_t Queue::write(int rank) {
  LOG(LogLevel::Info, "Writing checkpoint file with %d elements.", count);

  if (count == 0) {
    return 0;
  }

  char filename[256];
  sprintf(filename, "circle%d.txt", rank);
  FILE *checkpoint_file = fopen(filename, "w");

  if (checkpoint_file == NULL) {
    LOG(LogLevel::Error, "Unable to open checkpoint file %s", filename);
    return -1;
  }

  while (count > 0) {
    std::vector<uint8_t> content;
    if (pop(content) < 0) {
      LOG(LogLevel::Error, "Failed to pop item off queue->");
      return -1;
    }

    std::string str(content.begin(), content.end());
    if (fprintf(checkpoint_file, "%s\n", str.c_str()) < 0) {
      LOG(LogLevel::Error, "Failed to write \"%s\" to file.", str.c_str());
      return -1;
    }
  }

  int fclose_rc = fclose(checkpoint_file);
  return (int8_t)fclose_rc;
}
