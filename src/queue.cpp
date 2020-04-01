/**
 * @file
 * This file contains functions related to the local queue structure.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <unistd.h>

#include "lanl_circle.hpp"
#include "log.hpp"
#include "queue.hpp"

/**
 * Allocate memory for the basic queue structure used by libcircle.
 *
 * @return a reference to the allocated queue structure.
 *
 * @see circle::internal_queue_free
 */
circle::internal_queue_t *circle::internal_queue_init(void) {
  circle::internal_queue_t *qp;

  LOG(circle::LogLevel::Debug, "Allocating a queue structure.");

  qp = (circle::internal_queue_t *)malloc(sizeof(circle::internal_queue_t));

  /* Number of string pointers we have allocated */
  size_t str_count = CIRCLE_INITIAL_INTERNAL_QUEUE_SIZE;
  qp->str_count = (int32_t)str_count;

  /* Base address of string pool */
  qp->bytes = sizeof(char) * CIRCLE_MAX_STRING_LEN * str_count;
  qp->base = (char *)malloc(qp->bytes);
  qp->count = 0;
  qp->head = 0;

  /* String pointer array */
  qp->strings = (uintptr_t *)malloc(sizeof(uintptr_t) * str_count);

  if (!qp || !qp->base || !qp->strings) {
    LOG(circle::LogLevel::Error, "Failed to allocate a basic queue structure.");
    circle::internal_queue_free(qp);
    return (circle::internal_queue_t *)NULL;
  }

  return qp;
}

/**
 * Free the memory used by a libcircle basic queue structure.
 *
 * @param qp the reference to the queue that should be freed.
 * @return a negative value on failure, a positive one on success.
 */
int8_t circle::internal_queue_free(circle::internal_queue_t *qp) {
  if (qp) {
    if (qp->strings) {
      LOG(circle::LogLevel::Debug, "Freeing the queue strings array.");
      free(qp->strings);
    }

    if (qp->base) {
      LOG(circle::LogLevel::Debug, "Freeing the queue base.");
      free(qp->base);
    }

    LOG(circle::LogLevel::Debug, "Freeing a queue pointer.");
    free(qp);
  } else {
    LOG(circle::LogLevel::Error, "Attempted to free a null queue structure.");
    return -1;
  }

  return 1;
}

/**
 * Dump the raw contents of the queue structure to logging.
 *
 * @param qp the queue structure that should be dumped.
 */
void circle::internal_queue_dump(circle::internal_queue_t *qp) {
  uint32_t i = 0;
  char *p = qp->base;

  while (p++ < (qp->base + qp->bytes)) {
    if (i++ % 120 == 0) {
      LOG(circle::LogLevel::Debug, "%c", *p);
    } else {
      LOG(circle::LogLevel::Debug, "%c", *p);
    }
  }
}

/**
 * Pretty-print the queue data structure.
 *
 * @param qp the queue structure that should be pretty-printed.
 */
void circle::internal_queue_print(circle::internal_queue_t *qp) {
  int32_t i = 0;

  for (i = 0; i < qp->count; i++) {
    LOG(circle::LogLevel::Debug, "\t[%p][%d] %s", qp->base + qp->strings[i], i,
        qp->base + qp->strings[i]);
  }
}
/**
 * Extend the string array size size
 *
 */
int8_t circle::internal_queue_str_extend(circle::internal_queue_t *qp,
                                         int32_t new_size) {
  int32_t old_count = qp->str_count;

  /* TODO: check for overflow */
  while (qp->str_count < new_size) {
    qp->str_count += 4096;
  }

  size_t size = ((size_t)qp->str_count) * sizeof(uintptr_t);
  qp->strings = (uintptr_t *)realloc(qp->strings, size);

  LOG(circle::LogLevel::Debug,
      "Reallocing string array from"
      " [%d] to [%d] [%p] -> [%p]",
      old_count, qp->str_count, (void *)qp->strings,
      (void *)(qp->strings + size));

  if (!qp->strings) {
    LOG(circle::LogLevel::Error, "Unable to realloc string array.");
    return -1;
  }

  return 0;
}

/**
 * Extend the circle queue size
 *
 */
int8_t circle::internal_queue_extend(circle::internal_queue_t *qp,
                                     size_t new_size) {
  size_t current = qp->bytes;

  /* TODO: check for overflow */
  while (current < new_size) {
    current += ((size_t)sysconf(_SC_PAGESIZE)) * 4096;
  }

  LOG(circle::LogLevel::Debug, "Reallocing queue from [%zd] to [%zd] [%p] -> [%p].",
      qp->bytes, current, qp->base, qp->base + current);

  qp->base = (char *)realloc(qp->base, current);

  if (!qp->base) {
    LOG(circle::LogLevel::Error, "Failed to reallocate a basic queue structure.");
    return -1;
  }

  qp->bytes = current;
  return 0;
}

/**
 * Push the specified content onto the queue structure.
 *
 * @param qp the queue structure to push the value onto.
 * @param str the string value to push onto the queue.
 *
 * @return a positive number on success, a negative one on failure.
 */
int8_t circle::internal_queue_push(circle::internal_queue_t *qp,
                                   const std::vector<uint8_t> &content) {
  /* TODO: check that real_len fits within uint32_t */
  size_t real_len = content.size();
  uint32_t len = (uint32_t)real_len;

  if (len <= 0) {
    LOG(circle::LogLevel::Error, "Attempted to push an empty string onto a queue.");
    return -1;
  }

  if (len > CIRCLE_MAX_STRING_LEN) {
    LOG(circle::LogLevel::Error,
        "Attempted to push a value that was larger than expected.");
    return -1;
  }

  if (qp->count + 1 > qp->str_count) {
    LOG(circle::LogLevel::Debug, "Extending string array by 4096.");

    if (circle::internal_queue_str_extend(qp, qp->count + 1) < 0) {
      return -1;
    }
  }

  size_t new_bytes = (size_t)(qp->head + len) * sizeof(char);

  if (new_bytes > qp->bytes) {
    LOG(circle::LogLevel::Debug, "The queue is not large enough to add another value.");

    if (circle::internal_queue_extend(qp, new_bytes) < 0) {
      return -1;
    }
  }

  /* Set our write location to the end of the current strings array. */
  qp->strings[qp->count] = qp->head;

  /* Copy the string. */
  memcpy(qp->base + qp->head, reinterpret_cast<const char *>(&content[0]), len);

  /*
   * Make head point to the character after the string (strlen doesn't
   * include a trailing null).
   */
  qp->head += len;

  /* Make the head point to the next available memory */
  qp->count++;

  return 0;
}

/**
 * Removes an item from the queue and returns a copy.
 *
 * @param qp the queue structure to remove the item from.
 * @param str a reference to the value removed.
 *
 * @return a positive value on success, a negative one otherwise.
 */
int8_t circle::internal_queue_pop(circle::internal_queue_t *qp,
                                  std::vector<uint8_t> &content) {
  if (!qp) {
    LOG(circle::LogLevel::Error, "Attempted to pop from an invalid queue.");
    return -1;
  }

  if (qp->count < 1) {
    LOG(circle::LogLevel::Debug, "Attempted to pop from an empty queue.");
    return -1;
  }

  /* Copy last element into str */
  uintptr_t current = qp->strings[qp->count - 1];
  size_t len = qp->head - current;
  content.resize(len);
  memcpy(reinterpret_cast<uint8_t *>(&content[0]), qp->base + current, len);
  qp->head = current;
  qp->count--;

  return 0;
}

/**
 * Read a queue checkpoint file into working memory.
 *
 * @param qp the queue structure to read the checkpoint file into.
 * @param rank the node which holds the checkpoint file.
 *
 * @return a positive value on success, a negative one otherwise.
 */
int8_t circle::internal_queue_read(circle::internal_queue_t *qp, int rank) {
  if (!qp) {
    LOG(circle::LogLevel::Error, "Libcircle queue not initialized.");
    return -1;
  }

  LOG(circle::LogLevel::Debug, "Reading from checkpoint file %d.", rank);

  if (qp->count != 0) {
    LOG(circle::LogLevel::Warning,
        "Reading items from checkpoint file into non-empty work queue.");
  }

  char filename[256];
  sprintf(filename, "circle%d.txt", rank);

  LOG(circle::LogLevel::Debug, "Attempting to open %s.", filename);

  FILE *checkpoint_file = fopen(filename, "r");

  if (checkpoint_file == NULL) {
    LOG(circle::LogLevel::Error, "Unable to open checkpoint file %s", filename);
    return -1;
  }

  LOG(circle::LogLevel::Debug, "Checkpoint file opened.");

  uint32_t len = 0;
  char str[CIRCLE_MAX_STRING_LEN];

  while (fgets(str, CIRCLE_MAX_STRING_LEN, checkpoint_file) != NULL) {
    /* TODO: check that real_len fits within uint32_t */
    size_t real_len = strlen(str);
    len = (uint32_t)real_len;

    if (len > 0) {
      str[len - 1] = '\0';
    } else {
      continue;
    }

    std::vector<uint8_t> content(str, str + len);
    if (circle::internal_queue_push(qp, content) < 0) {
      LOG(circle::LogLevel::Error, "Failed to push element on queue \"%s\"", str);
    }

    LOG(circle::LogLevel::Debug, "Pushed %s onto queue.", str);
  }

  int fclose_rc = fclose(checkpoint_file);
  return (int8_t)fclose_rc;
}

/**
 * Write out the queue structure to a checkpoint file.
 *
 * @param qp the queue structure to be written to the checkpoint file.
 * @param rank the node which is writing out the checkpoint file.
 *
 * @return a positive value on success, negative otherwise.
 */
int8_t circle::internal_queue_write(circle::internal_queue_t *qp, int rank) {
  LOG(circle::LogLevel::Info, "Writing checkpoint file with %d elements.", qp->count);

  if (qp->count == 0) {
    return 0;
  }

  char filename[256];
  sprintf(filename, "circle%d.txt", rank);
  FILE *checkpoint_file = fopen(filename, "w");

  if (checkpoint_file == NULL) {
    LOG(circle::LogLevel::Error, "Unable to open checkpoint file %s", filename);
    return -1;
  }

  while (qp->count > 0) {
    std::vector<uint8_t> content;
    if (circle::internal_queue_pop(qp, content) < 0) {
      LOG(circle::LogLevel::Error, "Failed to pop item off queue.");
      return -1;
    }

    std::string str(content.begin(), content.end());
    if (fprintf(checkpoint_file, "%s\n", str.c_str()) < 0) {
      LOG(circle::LogLevel::Error, "Failed to write \"%s\" to file.", str.c_str());
      return -1;
    }
  }

  int fclose_rc = fclose(checkpoint_file);
  return (int8_t)fclose_rc;
}

/* EOF */
