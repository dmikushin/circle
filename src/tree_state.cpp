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

using namespace circle;
using namespace circle::internal;

/* given the process's rank and the number of ranks, this computes a k-ary
 * tree rooted at rank 0, the structure records the number of children
 * of the local rank and the list of their ranks */
TreeState::TreeState(Circle* parent_, int rank_, int nranks_, int maxChildren_) :
  parent(parent_), rank(rank_), nranks(nranks_),
  parentRank(MPI_PROC_NULL), nchildren(0), childrenRanks(nullptr),
  maxChildren(maxChildren_) {
  /* allocate memory to hold list of children ranks */
  if (maxChildren > 0) {
    size_t bytes = (size_t)maxChildren * sizeof(int);
    childrenRanks = (int *)malloc(bytes);

    if (childrenRanks == NULL) {
      LOG(LogLevel::Fatal, "Failed to allocate memory for list of children.");
      MPI_Abort(parent->impl->comm, CIRCLE_MPI_ERROR);
    }
  }

  /* initialize all ranks to NULL */
  for (int i = 0; i < maxChildren; i++) {
    childrenRanks[i] = MPI_PROC_NULL;
  }

  /* compute rank of our parent if we have one */
  if (rank > 0) {
    parentRank = (rank - 1) / maxChildren;
  }

  /* identify ranks of what would be leftmost and rightmost children */
  int left = rank * maxChildren + 1;
  int right = rank * maxChildren + maxChildren;

  /* if we have at least one child,
   * compute number of children and list of child ranks */
  if (left < nranks) {
    /* adjust right child in case we don't have a full set of k */
    if (right >= nranks) {
      right = nranks - 1;
    }

    /* compute number of children and list of child ranks */
    nchildren = right - left + 1;

    for (int i = 0; i < nchildren; i++) {
      childrenRanks[i] = left + i;
    }
  }
}

TreeState::~TreeState() {
  /* free child rank list */
  free(&childrenRanks);
}

int TreeState::getChildrenCount() const { return nchildren; }

int TreeState::getParentRank() const { return parentRank; }

const int *TreeState::getChildrenRanks() const { return childrenRanks; }

