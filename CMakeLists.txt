#Standard stuff
cmake_minimum_required(VERSION 2.6)
project(bw_generator)
project(bw_recv)

#Check threads
set(CMAKE_THREAD_PREFER_PTHREAD 1)
include(FindThreads)

if(NOT CMAKE_USE_PTHREADS_INIT)
    message(FATAL_ERROR "Could not find pthreads")
endif(NOT CMAKE_USE_PTHREADS_INIT)

add_executable(bw_generator bw_estimation_generator.c)
add_executable(bw_recv bw_estimation_recv.c)
target_link_libraries(bw_generator ${CMAKE_THREAD_LIBS_INIT})
