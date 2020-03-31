cmake_minimum_required(VERSION 3.9)

project(circle)

enable_language(Fortran)

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

add_library(${PROJECT_NAME} SHARED ${SRCS})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(${PROJECT_NAME} PRIVATE ${MPI_Fortran_INCLUDE_PATH})
target_link_libraries(${PROJECT_NAME} ${MPI_CXX_LIBRARIES} ${MPI_C_LIBRARIES})

add_executable(check_checkpoint "${CMAKE_CURRENT_SOURCE_DIR}/src/tests/check_checkpoint.cpp")
target_include_directories(check_checkpoint PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(check_checkpoint PRIVATE ${CHECK_INCLUDE_DIRS})
target_link_libraries(check_checkpoint ${PROJECT_NAME} ${CHECK_LIBRARIES} Threads::Threads)
target_link_directories(check_checkpoint PRIVATE ${CHECK_LIBRARY_DIRS})

add_executable(check_queue "${CMAKE_CURRENT_SOURCE_DIR}/src/tests/check_queue.cpp")
target_include_directories(check_queue PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(check_queue PRIVATE ${CHECK_INCLUDE_DIRS})
target_link_libraries(check_queue ${PROJECT_NAME} ${CHECK_LIBRARIES} Threads::Threads)
target_link_directories(check_queue PRIVATE ${CHECK_LIBRARY_DIRS})

add_executable(example "${CMAKE_CURRENT_SOURCE_DIR}/src/examples/example.cpp")
target_include_directories(example PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(example PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/filesystem/include)
target_link_libraries(example ${PROJECT_NAME})

add_executable(example_reduction "${CMAKE_CURRENT_SOURCE_DIR}/src/examples/example_reduction.cpp")
target_include_directories(example_reduction PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(example_reduction PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/filesystem/include)
target_link_libraries(example_reduction ${PROJECT_NAME})

