cmake_minimum_required(VERSION 3.5.0)
project(ohospatch)

set(NATIVERENDER_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR})

if(DEFINED PACKAGE_FIND_FILE)
    include(${PACKAGE_FIND_FILE})
endif()

include_directories(${NATIVERENDER_ROOT_PATH})

find_program(NODE_EXECUTABLE node)
if(NOT NODE_EXECUTABLE)
    message(FATAL_ERROR "node is required to minify the OhosPatch runtime")
endif()

set(OHOSPATCH_PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../..")
set(FIXIT_RUNTIME_BUILD_ROOT "${OHOSPATCH_PROJECT_ROOT}/tools/fixit-runtime-build")
set(GULP_CLI "${FIXIT_RUNTIME_BUILD_ROOT}/node_modules/gulp/bin/gulp.js")
if(NOT EXISTS "${GULP_CLI}")
    message(FATAL_ERROR "gulp is required to minify the OhosPatch runtime. Run `npm install --prefix tools/fixit-runtime-build` in the project root.")
endif()

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

set(FIXIT_RUNTIME_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/runtime/fixit.js")
set(FIXIT_RUNTIME_RAWFILE
    "${CMAKE_CURRENT_SOURCE_DIR}/../resources/rawfile/ohospatch/fixit.min.js"
)

set_property(
    DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${FIXIT_RUNTIME_SOURCE}"
    "${FIXIT_RUNTIME_BUILD_ROOT}/gulpfile.mjs"
    "${FIXIT_RUNTIME_BUILD_ROOT}/minifier.mjs"
)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../resources/rawfile/ohospatch")
execute_process(
    COMMAND "${NODE_EXECUTABLE}"
            "${GULP_CLI}"
            "--gulpfile"
            "${FIXIT_RUNTIME_BUILD_ROOT}/gulpfile.mjs"
            "--cwd"
            "${FIXIT_RUNTIME_BUILD_ROOT}"
            "fixitRuntime"
            "--input"
            "ohospatch/src/main/cpp/runtime/fixit.js"
            "--output"
            "ohospatch/src/main/resources/rawfile/ohospatch/fixit.min.js"
    RESULT_VARIABLE FIXIT_RUNTIME_MINIFY_RESULT
)
if(NOT FIXIT_RUNTIME_MINIFY_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to minify OhosPatch runtime JavaScript")
endif()

add_library(ohospatch SHARED ohospatch.cpp)
target_compile_options(
    ohospatch
    PRIVATE
    -fno-exceptions
    -fno-rtti
    -fvisibility=hidden
    -fvisibility-inlines-hidden
    -ffunction-sections
    -fdata-sections
)
target_link_options(ohospatch PRIVATE -Wl,--gc-sections -Wl,--strip-all)
target_link_libraries(
    ohospatch
    PUBLIC
    libace_napi.z.so
    libbundle_ndk.z.so
    libhilog_ndk.z.so
    libjsvm.so
    librawfile.z.so
    libuv.so
)
