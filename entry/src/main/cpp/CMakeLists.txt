cmake_minimum_required(VERSION 3.5.0)
project(ohospatch)

set(NATIVERENDER_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR})

if(DEFINED PACKAGE_FIND_FILE)
    include(${PACKAGE_FIND_FILE})
endif()

include_directories(${NATIVERENDER_ROOT_PATH})

add_library(ohospatch SHARED ohospatch.cpp)
target_link_libraries(ohospatch PUBLIC libace_napi.z.so libjsvm.so)
