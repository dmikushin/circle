cmake_minimum_required(VERSION 3.9)

project(lanl_circle)

enable_language(Fortran)

if (NOT TARGET sighandlers)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/sighandlers)
endif()

if(NOT TARGET pybind11)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/pybind11)
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
set(CMAKE_Fortran_MODULE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/modules)

find_package(MPI REQUIRED)
find_package(Threads REQUIRED)
find_package(Check REQUIRED)

# Enable C++ 11 support
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_EXTENSIONS OFF)

if ("x" STREQUAL "x${CMAKE_BUILD_TYPE}")
set(CMAKE_BUILD_TYPE Release)
endif()
string(TOUPPER ${CMAKE_BUILD_TYPE} CMAKE_BUILD_TYPE_UPPER)

file(GLOB SRCS
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.f90")

add_library(${PROJECT_NAME}_library SHARED ${SRCS})
target_include_directories(${PROJECT_NAME}_library PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(${PROJECT_NAME}_library PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/sighandlers/include)
target_include_directories(${PROJECT_NAME}_library PRIVATE ${MPI_C_INCLUDE_PATH})
target_link_libraries(${PROJECT_NAME}_library ${MPI_CXX_LIBRARIES} ${MPI_C_LIBRARIES} sighandlers)
set_target_properties(${PROJECT_NAME}_library PROPERTIES OUTPUT_NAME ${PROJECT_NAME})

pybind11_add_module(${PROJECT_NAME} SHARED "${CMAKE_CURRENT_SOURCE_DIR}/src/python/${PROJECT_NAME}.cpp")
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/pybind11/include)
target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}_library)

add_executable(check_checkpoint "${CMAKE_CURRENT_SOURCE_DIR}/src/tests/check_checkpoint.cpp")
target_include_directories(check_checkpoint PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(check_checkpoint PRIVATE ${CHECK_INCLUDE_DIRS})
target_link_libraries(check_checkpoint ${PROJECT_NAME}_library ${CHECK_LIBRARIES} Threads::Threads)
target_link_directories(check_checkpoint PRIVATE ${CHECK_LIBRARY_DIRS})

add_executable(check_queue "${CMAKE_CURRENT_SOURCE_DIR}/src/tests/check_queue.cpp")
target_include_directories(check_queue PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(check_queue PRIVATE ${CHECK_INCLUDE_DIRS})
target_link_libraries(check_queue ${PROJECT_NAME}_library ${CHECK_LIBRARIES} Threads::Threads)
target_link_directories(check_queue PRIVATE ${CHECK_LIBRARY_DIRS})

add_executable(example_cxx "${CMAKE_CURRENT_SOURCE_DIR}/src/examples/example.cpp")
target_include_directories(example_cxx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(example_cxx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/filesystem/include)
target_link_libraries(example_cxx ${PROJECT_NAME}_library)

add_executable(example_reduction_cxx "${CMAKE_CURRENT_SOURCE_DIR}/src/examples/example_reduction.cpp")
target_include_directories(example_reduction_cxx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(example_reduction_cxx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/filesystem/include)
target_link_libraries(example_reduction_cxx ${PROJECT_NAME}_library)

add_executable(example_pi_c "${CMAKE_CURRENT_SOURCE_DIR}/src/examples/example_pi.c")
target_include_directories(example_pi_c PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(example_pi_c PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/filesystem/include)
target_link_libraries(example_pi_c ${PROJECT_NAME}_library)

add_executable(example_pi_f90 "${CMAKE_CURRENT_SOURCE_DIR}/src/examples/example_pi.f90")
target_include_directories(example_pi_f90 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(example_pi_f90 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/filesystem/include)
target_link_libraries(example_pi_f90 ${PROJECT_NAME}_library)

