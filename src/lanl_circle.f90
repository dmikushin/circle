module lanl_circle

!***********************************************************************
!
!     Fortran API for LANL Circle
!
!***********************************************************************

use iso_c_binding
implicit none

!
! Run time flags for the behavior of splitting work.
!
enum, bind(C) ! CircleRuntimeFlags
  enumerator :: CircleSplitRandom = 1, CircleSplitEqual = 2, CircleCreateGlobal = 4, CircleTermTree = 8, CircleDefaultFlags = 2
end enum

!
! The various logging levels that Circle will output.
!
enum, bind(C) ! CircleLogLevel
  enumerator :: CircleNone = 0, CircleFatal = 1, CircleError = 2, CircleWarning = 3, CircleInfo = 4, CircleDebug = 5
end enum

type, public, bind(C) :: circle

  type(c_ptr) :: handle
  
contains

  procedure, pass :: get_log_level => circle_get_log_level
  procedure, pass :: set_log_level => circle_set_log_level
  procedure, pass :: get_runtime_flags => circle_get_runtime_flags
  procedure, pass :: set_runtime_flags => circle_set_runtime_flags
  procedure, pass :: get_tree_width => circle_get_tree_width
  procedure, pass :: set_tree_width => circle_set_tree_width
  procedure, pass :: get_reduce_period => circle_get_reduce_period
  procedure, pass :: set_reduce_period => circle_set_reduce_period
  procedure, pass :: get_rank => circle_get_rank
  procedure, pass :: reduce => circle_reduce
  procedure, pass :: execute => circle_execute
  procedure, pass :: abort => circle_abort
  procedure, pass :: read_restarts => circle_read_restarts
  procedure, pass :: checkpoint => circle_checkpoint
  procedure, pass :: enqueue => circle_enqueue
  procedure, pass :: dequeue => circle_dequeue
  procedure, pass :: get_local_queue_size => circle_get_local_queue_size

end type circle

abstract interface
  subroutine circle_callback(this) bind(C)
  import circle
  type(circle), intent(in) :: this
  end subroutine circle_callback
end interface

contains

!
! Initialize a Circle instance for parallel processing.
!
subroutine circle_create_simple(this, create_callback, circle_process_callback, runtime_flags)
use iso_c_binding
implicit none

interface

function create_simple_c_api(create_callback, circle_process_callback, &
  runtime_flags) bind(C, name = 'circle_create_simple')
use iso_c_binding
implicit none
procedure(circle_callback) :: create_callback, circle_process_callback
integer(c_int), intent(in), value :: runtime_flags
type(c_ptr) :: create_simple_c_api
end function create_simple_c_api

end interface

class(circle), intent(out) :: this
procedure(circle_callback) :: create_callback, circle_process_callback
integer, intent(in) :: runtime_flags

this%handle = create_simple_c_api(create_callback, circle_process_callback, &
  runtime_flags)

end subroutine circle_create_simple

!
! Initialize a Circle instance for parallel processing and reduction.
!
subroutine circle_create(this, create_callback, circle_process_callback, &
  circle_reduce_init_callback, circle_reduce_operation_callback, circle_reduce_finalize_callback, &
  runtime_flags)
use iso_c_binding
implicit none

interface

function create_c_api(create_callback, circle_process_callback, &
  circle_reduce_init_callback, circle_reduce_operation_callback, circle_reduce_finalize_callback, &
  runtime_flags) bind(C, name = 'circle_create')
use iso_c_binding
implicit none
type(c_funptr), intent(in), value :: create_callback, circle_process_callback
type(c_funptr), intent(in), value :: circle_reduce_init_callback, circle_reduce_operation_callback, circle_reduce_finalize_callback
integer(c_int), intent(in), value :: runtime_flags
type(c_ptr) :: create_c_api
end function create_c_api

end interface

class(circle), intent(out) :: this
type(c_funptr), intent(in), value :: create_callback, circle_process_callback
type(c_funptr), intent(in), value :: circle_reduce_init_callback, circle_reduce_operation_callback, circle_reduce_finalize_callback
integer(c_int), intent(in) :: runtime_flags

this%handle = create_c_api(create_callback, circle_process_callback, &
  circle_reduce_init_callback, circle_reduce_operation_callback, circle_reduce_finalize_callback, &
  runtime_flags)

end subroutine circle_create

!
!
!
function circle_get_log_level(this)
use iso_c_binding
implicit none

interface

function get_log_level_c_api(handle) bind(C, name = 'circle_get_log_level')
use iso_c_binding
implicit none
type(c_ptr), value :: handle
integer(c_int) :: get_log_level_c_api
end function get_log_level_c_api

end interface

class(circle), intent(in) :: this
integer(c_int) :: circle_get_log_level

circle_get_log_level = get_log_level_c_api(this%handle)

end function circle_get_log_level

!
! Define the detail of logging that Circle should output.
!
subroutine circle_set_log_level(this, level)
use iso_c_binding
implicit none

interface

subroutine set_log_level_c_api(handle, level) bind(C, name = 'circle_set_log_level')
use iso_c_binding
implicit none
type(c_ptr), value :: handle
integer(c_int), intent(in), value :: level
end subroutine set_log_level_c_api

end interface

class(circle), intent(in) :: this
integer(c_int), intent(in) :: level

call set_log_level_c_api(this%handle, level)

end subroutine circle_set_log_level

!
!
!
function circle_get_runtime_flags(this)
use iso_c_binding
implicit none

interface

function get_runtime_flags_c_api(handle) bind(C, name = 'circle_get_runtime_flags')
use iso_c_binding
implicit none
type(c_ptr), value :: handle
integer(c_int) :: get_runtime_flags_c_api
end function get_runtime_flags_c_api

end interface

class(circle), intent(in) :: this
integer(c_int) :: circle_get_runtime_flags

circle_get_runtime_flags = get_runtime_flags_c_api(this%handle)

end function circle_get_runtime_flags

!
! Change run time flags.
!
subroutine circle_set_runtime_flags(this, options)
use iso_c_binding
implicit none

interface

subroutine set_runtime_flags_c_api(handle, options) bind(C, name = 'circle_set_runtime_flags')
use iso_c_binding
implicit none
type(c_ptr), value :: handle
integer(c_int), intent(in), value :: options
end subroutine set_runtime_flags_c_api

end interface

class(circle), intent(in) :: this
integer(c_int), intent(in) :: options

call set_runtime_flags_c_api(this%handle, options)

end subroutine circle_set_runtime_flags

!
!
!
function circle_get_tree_width(this)
use iso_c_binding
implicit none

interface

function get_tree_width_c_api(handle) bind(C, name = 'circle_get_tree_width')
use iso_c_binding
implicit none
type(c_ptr), value :: handle
integer(c_int) :: get_tree_width_c_api
end function get_tree_width_c_api

end interface

class(circle), intent(in) :: this
integer(c_int) :: circle_get_tree_width

circle_get_tree_width = get_tree_width_c_api(this%handle)

end function circle_get_tree_width

!
! Change the width of the k-ary communication tree.
!
subroutine circle_set_tree_width(this, width)
use iso_c_binding
implicit none

interface

subroutine set_tree_width_c_api(handle, width) bind(C, name = 'circle_set_tree_width')
use iso_c_binding
implicit none
type(c_ptr), value :: handle
integer(c_int), intent(in), value :: width
end subroutine set_tree_width_c_api

end interface

class(circle), intent(in) :: this
integer(c_int), intent(in) :: width

call set_tree_width_c_api(this%handle, width)

end subroutine circle_set_tree_width

!
!
!
function circle_get_reduce_period(this)
use iso_c_binding
implicit none

interface

function get_reduce_period_c_api(handle) bind(C, name = 'circle_get_reduce_period')
use iso_c_binding
implicit none
type(c_ptr), value :: handle
integer(c_int) :: get_reduce_period_c_api
end function get_reduce_period_c_api

end interface

class(circle), intent(in) :: this
integer(c_int) :: circle_get_reduce_period

circle_get_reduce_period = get_reduce_period_c_api(this%handle)

end function circle_get_reduce_period

!
! Change the number of seconds between consecutive reductions.
!
subroutine circle_set_reduce_period(this, secs)
use iso_c_binding
implicit none

interface

subroutine set_reduce_period_c_api(handle, secs) bind(C, name = 'circle_set_reduce_period')
use iso_c_binding
implicit none
type(c_ptr), value :: handle
integer(c_int), intent(in), value :: secs
end subroutine set_reduce_period_c_api

end interface

class(circle), intent(in) :: this
integer(c_int), intent(in) :: secs

call set_reduce_period_c_api(this%handle, secs)

end subroutine circle_set_reduce_period

!
! Get an MPI rank corresponding to the current process.
!
function circle_get_rank(this)
use iso_c_binding
implicit none

interface

function get_rank_c_api(handle) bind(C, name = 'circle_get_rank')
use iso_c_binding
implicit none
type(c_ptr), value :: handle
integer(c_int) :: get_rank_c_api
end function get_rank_c_api

end interface

class(circle), intent(in) :: this
integer(c_int) :: circle_get_rank

circle_get_rank = get_rank_c_api(this%handle)

end function circle_get_rank

!
!
!
subroutine circle_reduce(this, buf, szbuf)
use iso_c_binding
implicit none

interface

subroutine reduce_c_api(handle, buf, szbuf) bind(C, name = 'circle_reduce')
use iso_c_binding
implicit none
type(c_ptr), value :: handle
type(c_ptr), intent(in), value :: buf
integer(c_size_t), intent(in), value :: szbuf
end subroutine reduce_c_api

end interface

class(circle), intent(in) :: this
type(c_ptr), intent(in) :: buf
integer(c_size_t), intent(in) :: szbuf

call reduce_c_api(this%handle, buf, szbuf)

end subroutine circle_reduce

!
! Once you've defined and told Circle about your callbacks, use this to
! execute your program.
!
subroutine circle_execute(this)
use iso_c_binding
implicit none

interface

subroutine execute_c_api(handle) bind(C, name = 'circle_execute')
use iso_c_binding
implicit none
type(c_ptr), value :: handle
end subroutine execute_c_api

end interface

class(circle), intent(in) :: this

call execute_c_api(this%handle)

end subroutine circle_execute

!
! Call this function to have all ranks dump a checkpoint file and exit.
!
subroutine circle_abort(this)
use iso_c_binding
implicit none

interface

subroutine abort_c_api(handle) bind(C, name = 'circle_abort')
use iso_c_binding
implicit none
type(c_ptr), value :: handle
end subroutine abort_c_api

end interface

class(circle), intent(in) :: this

call abort_c_api(this%handle) 

end subroutine circle_abort

!
! Call this function to read in libcircle restart files.
!
function circle_read_restarts(this)
use iso_c_binding
implicit none

interface

function read_restarts_c_api(handle) bind(C, name = 'circle_read_restarts')
use iso_c_binding
implicit none
type(c_ptr), value :: handle
integer(c_int) :: read_restarts_c_api
end function read_restarts_c_api

end interface

class(circle), intent(in) :: this
integer(c_int) :: circle_read_restarts

circle_read_restarts = read_restarts_c_api(this%handle)

end function circle_read_restarts

!
! Call this function to read in libcircle restart files.  Each rank
! writes a file called circle<rank>.txt
!
function circle_checkpoint(this)
use iso_c_binding
implicit none

interface

function checkpoint_c_api(handle) bind(C, name = 'circle_checkpoint')
use iso_c_binding
implicit none
type(c_ptr), value :: handle
integer(c_int) :: checkpoint_c_api
end function checkpoint_c_api

end interface

class(circle), intent(in) :: this
integer(c_int) :: circle_checkpoint

circle_checkpoint = checkpoint_c_api(this%handle)

end function circle_checkpoint

!
! The interface to the work queue. This can be accessed from within the
! process and create work callbacks.
!
function circle_enqueue(this, element, szelement)
use iso_c_binding
implicit none

interface

function circle_enqueue_c_api(handle, element, szelement) bind(C, name = 'circle_enqueue')
use iso_c_binding
implicit none
type(c_ptr), value :: handle
type(c_ptr), intent(in), value :: element
integer(c_size_t), intent(in) :: szelement
integer(c_int) :: circle_enqueue_c_api
end function circle_enqueue_c_api

end interface

class(circle), intent(in) :: this
type(c_ptr), intent(in) :: element
integer(c_size_t), intent(in) :: szelement
integer(c_int) :: circle_enqueue

circle_enqueue = circle_enqueue_c_api(this%handle, element, szelement)

end function circle_enqueue

!
!
!
function circle_dequeue(this, element, szelement)
use iso_c_binding
implicit none

interface

function circle_dequeue_c_api(handle, element, szelement) bind(C, name = 'circle_dequeue')
use iso_c_binding
implicit none
type(c_ptr), value :: handle
type(c_ptr), intent(in), value :: element
integer(c_size_t), intent(out) :: szelement
integer(c_int) :: circle_dequeue_c_api
end function circle_dequeue_c_api

end interface

class(circle), intent(in) :: this
type(c_ptr), intent(in) :: element
integer(c_size_t), intent(out) :: szelement
integer(c_int) :: circle_dequeue

circle_dequeue = circle_dequeue_c_api(this%handle, element, szelement)

end function circle_dequeue

!
!
!
function circle_get_local_queue_size(this)
use iso_c_binding
implicit none

interface

function get_local_queue_size_c_api(handle) bind(C, name = 'circle_get_local_queue_size')
use iso_c_binding
implicit none
type(c_ptr), value :: handle
integer(c_int) :: get_local_queue_size_c_api
end function get_local_queue_size_c_api

end interface

class(circle), intent(in) :: this
integer(c_int) :: circle_get_local_queue_size

circle_get_local_queue_size = get_local_queue_size_c_api(this%handle)

end function circle_get_local_queue_size

!
! Initialize internal state needed by Circle. This should be called before
! any other Circle API call. This returns the MPI rank value.
!
function circle_init()
use iso_c_binding
implicit none

integer(c_int) :: circle_init

! TODO

end function circle_init

!
! Returns an elapsed time on the calling processor for benchmarking purposes.
!
function circle_wtime()
use iso_c_binding
implicit none

interface

function wtime_c_api() bind(C, name = 'circle_wtime')
use iso_c_binding
implicit none
real(8) :: wtime_c_api
end function wtime_c_api

end interface

real(8) :: circle_wtime

circle_wtime = wtime_c_api()

end function circle_wtime



end module lanl_circle

