subroutine foo
end subroutine foo

program main
use iso_c_binding
use lanl_circle
implicit none

integer, parameter :: npoints = 10000, njobs = 10
real(8), target :: pi_partial
integer :: err
type(c_ptr) :: example
type(c_funptr) :: create_some_work, process_some_work
type(c_funptr) :: reduce_init, reduce_op, reduce_fini

  create_some_work = c_funloc(my_create_some_work)
  process_some_work = c_funloc(my_process_some_work)
  reduce_init = c_funloc(my_reduce_init)
  reduce_op = c_funloc(my_reduce_op)
  reduce_fini = c_funloc(my_reduce_fini)

  !
  ! Do partial computations with libcircle.
  !
  err = circle_init()
  call circle_create(example, create_some_work, process_some_work, &
                     reduce_init, reduce_op, reduce_fini, &
                     CircleDefaultFlags)
  call circle_set_log_level(example, CircleInfo)

  pi_partial = 0

  !
  ! Specify time period between consecutive reductions.
  ! Here we set a time period of 10 seconds.
  !
  call circle_set_reduce_period(example, 10)

  call circle_execute(example)

contains

!
! The reduce_init callback provides the memory address and size of the
! variable(s) to use as input on each process to the reduction
! operation.  One can specify an arbitrary block of data as input.
! When a new reduction is started, libcircle invokes this callback on
! each process to snapshot the memory block specified in the call to
! circle::reduce.  The library makes a memcpy of the memory block, so
! its contents can be safely changed or go out of scope after the call
! to circle::reduce returns.
!
subroutine my_reduce_init(example)
use iso_c_binding
use lanl_circle
implicit none

type(c_ptr), intent(in), value :: example
  !
  ! We give the starting memory address and size of a memory
  ! block that we want libcircle to capture on this process when
  ! it starts a new reduction operation.
  !
  ! In this example, we capture a single uint64_t value,
  ! which is the global reduce_count variable.
  !
  call circle_reduce(example, c_loc(pi_partial), c_sizeof(pi_partial))
end subroutine

!
! On intermediate nodes of the reduction tree, libcircle invokes the
! reduce_op callback to reduce two data buffers.  The starting
! address and size of each data buffer are provided as input
! parameters to the callback function.  An arbitrary reduction
! operation can be executed.  Then libcircle snapshots the memory
! block specified in the call to circle::reduce to capture the partial
! result.  The library makes a memcpy of the memory block, so its
! contents can be safely changed or go out of scope after the call to
! circle::reduce returns.
!
! Note that the sizes of the input buffers do not have to be the same,
! and the output buffer does not need to be the same size as either
! input buffer.  For example, one could concatentate buffers so that
! the reduction actually performs a gather operation.
!
subroutine my_reduce_op(example, a, a_size, b, b_size)
implicit none

  type(c_ptr), intent(in), value :: example
  real(8), intent(in) :: a, b
  integer(c_size_t), intent(in) :: a_size, b_size
  real(8), target :: res
  !
  ! Here we are given the starting address and size of two input
  ! buffers.  These could be the initial memory blocks copied during
  ! reduce_init, or they could be intermediate results copied from a
  ! reduce_op call.  We can execute an arbitrary operation on these
  ! input buffers and then we save the partial result to a call
  ! to circle::reduce.
  !
  ! In this example, we sum two input uint64_t values and
  ! libcircle makes a copy of the result when we call circle::reduce.
  !
  res = a + b
  call circle_reduce(example, c_loc(res), c_sizeof(res))
end subroutine

!
! The reduce_fini callback is only invoked on the root process.  It
! provides a buffer holding the final reduction result as in input
! parameter. Typically, one might print the result in this callback.
!
subroutine my_reduce_fini(example, pi_total, size)
use iso_c_binding
implicit none

  type(c_ptr), intent(in), value :: example
  real(8), intent(in) :: pi_total
  integer(c_size_t), intent(in) :: size
  !
  ! In this example, we get the reduced sum from the input buffer,
  ! and we compute the average processing rate.  We then print
  ! the count, time, and rate of items processed.
  !

  ! get result of reduction
  print *, "result = ", pi_total / njobs
end subroutine

! An example of a create callback defined by your program
subroutine my_create_some_work(example)
use iso_c_binding
implicit none

  type(c_ptr), intent(in), value :: example
  integer :: n, i, j, err
  integer, allocatable, target :: seed(:)
  !
  ! This is where you should generate work that needs to be processed.
  ! For example, we can generate RNG seeds to be used by worker processes.
  !
  ! By default, the create callback is only executed on the root
  ! process, i.e., the process whose call to circle::init returns 0.
  ! If the circle::CREATE_GLOBAL option flag is specified, the create
  ! callback is invoked on all processes.
  !

  call random_seed(size = n)
  allocate(seed(n))
  do i = 1, njobs
    do j = 1, n
      seed(j) = j + i
    enddo
    err = circle_enqueue(example, c_loc(seed), sizeof(seed(1)) * n)
  enddo    
    
  deallocate(seed)

end subroutine

! An example of a process callback defined by your program.
subroutine my_process_some_work(example)
use iso_c_binding
implicit none

  type(c_ptr), intent(in), value :: example
  integer, allocatable, target :: seed(:)
  integer(c_size_t) :: szseed
  integer :: ncircle, i, err
  real :: rval(2)

  !
  ! Master process sends us the random seed that he generated,
  ! as an example of data sharing. We use this seed in our processing.
  !
  err = circle_dequeue(example, c_null_ptr, szseed)
  allocate(seed(szseed / c_sizeof(seed(1))))
  err = circle_dequeue(example, c_loc(seed), szseed)
  call random_seed(put=seed)
  deallocate(seed)

  !
  ! This is where work should be processed. For example, this is where you
  ! should size one of the files which was placed on the queue by your
  ! create_some_work callback. You should try to keep this short and block
  ! as little as possible.
  !
  ncircle = 0
  do i = 1, npoints
    call random_number(rval)
    if (rval(1)**2 + rval(2)**2 .le. 1) then
      ncircle = ncircle + 1
    endif
  enddo

  pi_partial = dble(npoints) / ncircle
end subroutine

end program
