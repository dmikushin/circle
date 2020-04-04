/**
 * @file
 *
 * Handles features of libcircle related to tokens (for self stabilization).
 */

#include <assert.h>
#include <dirent.h>
#include <mpi.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "lanl_circle.hpp"
#include "circle_impl.hpp"
#include "log.hpp"
#include "token.hpp"
#include "worker.hpp"

using namespace circle;
using namespace circle::internal;

/* given the process's rank and the number of ranks, this computes a k-ary
 * tree rooted at rank 0, the structure records the number of children
 * of the local rank and the list of their ranks */
TreeState::TreeState(Circle* parent_, int rank_, int ranks_, int k) : parent(parent_) {
  int i;

  /* initialize fields */
  rank = (int)rank_;
  ranks = (int)ranks_;
  parent_rank = MPI_PROC_NULL;
  children = 0;
  child_ranks = NULL;

  /* compute the maximum number of children this task may have */
  int max_children = k;

  /* allocate memory to hold list of children ranks */
  if (max_children > 0) {
    size_t bytes = (size_t)max_children * sizeof(int);
    child_ranks = (int *)malloc(bytes);

    if (child_ranks == NULL) {
      LOG(LogLevel::Fatal, "Failed to allocate memory for list of children.");
      MPI_Abort(parent->impl->comm, CIRCLE_MPI_ERROR);
    }
  }

  /* initialize all ranks to NULL */
  for (i = 0; i < max_children; i++) {
    child_ranks[i] = MPI_PROC_NULL;
  }

  /* compute rank of our parent if we have one */
  if (rank > 0) {
    parent_rank = (rank - 1) / k;
    printf("Rank %d has parent_rank = %d\n", rank, parent_rank);
  }

  /* identify ranks of what would be leftmost and rightmost children */
  int left = rank * k + 1;
  int right = rank * k + k;

  /* if we have at least one child,
   * compute number of children and list of child ranks */
  if (left < ranks) {
    /* adjust right child in case we don't have a full set of k */
    if (right >= ranks) {
      right = ranks - 1;
    }

    /* compute number of children and list of child ranks */
    children = right - left + 1;

    for (i = 0; i < children; i++) {
      child_ranks[i] = left + i;
    }
  }

  return;
}

TreeState::~TreeState() {
  /* free child rank list */
  free(&child_ranks);

  return;
}

