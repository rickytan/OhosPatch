cmake_minimum_required(VERSION 3.5.0)
project(ohospatch)

set(NATIVERENDER_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR})

if(DEFINED PACKAGE_FIND_FILE)
    include(${PACKAGE_FIND_FILE})
endif()

include_directories(${NATIVERENDER_ROOT_PATH})

set_property(
    DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/fixit.js"
)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/runtime/fixit.js" FIXIT_RUNTIME_SOURCE)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/fixit_runtime.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/fixit_runtime.h"
    @ONLY
)

add_library(ohospatch SHARED ohospatch.cpp)
target_include_directories(ohospatch PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_options(ohospatch PRIVATE -fno-exceptions)
target_link_libraries(ohospatch PUBLIC libace_napi.z.so libhilog_ndk.z.so libjsvm.so libuv.so)
