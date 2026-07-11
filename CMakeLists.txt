cmake_minimum_required(VERSION 3.24)

project(meta_trader_zeromq VERSION 2.0.0 LANGUAGES C CXX)

# libzmq 4.3.5 predates CMake 4's policy floor.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Policy floor for bundled libzmq" FORCE)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "This project builds MetaTrader DLLs with MSVC on Windows only.")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(MT_ZMQ_PACKAGE_DIR "mt4-win32")
    set(MT_ZMQ_DEF_FILE "${CMAKE_CURRENT_SOURCE_DIR}/wrap/windows/zmq_bind_win32.def")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(MT_ZMQ_PACKAGE_DIR "mt5-x64")
    set(MT_ZMQ_DEF_FILE "${CMAKE_CURRENT_SOURCE_DIR}/wrap/windows/zmq_bind_x64.def")
else()
    message(FATAL_ERROR "Only Win32 and x64 builds are supported.")
endif()

include(FetchContent)

# Keep libzmq small and reproducible. These cache entries are consumed by the
# pinned libzmq 4.3.5 project before FetchContent makes it available.
set(BUILD_SHARED ON CACHE BOOL "Build shared libzmq" FORCE)
set(BUILD_STATIC OFF CACHE BOOL "Do not build static libzmq" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "Do not build libzmq tests" FORCE)
set(ZMQ_BUILD_TESTS OFF CACHE BOOL "Do not build libzmq tests" FORCE)
set(WITH_PERF_TOOL OFF CACHE BOOL "Do not build libzmq performance tools" FORCE)
set(ENABLE_DRAFTS OFF CACHE BOOL "Disable libzmq draft API" FORCE)
set(ENABLE_CPACK OFF CACHE BOOL "Disable libzmq packaging" FORCE)
set(WITH_DOC OFF CACHE BOOL "Disable libzmq documentation" FORCE)
set(WITH_OPENPGM OFF CACHE BOOL "Disable OpenPGM" FORCE)
set(WITH_NORM OFF CACHE BOOL "Disable NORM" FORCE)
set(WITH_VMCI OFF CACHE BOOL "Disable VMCI" FORCE)
set(WITH_LIBSODIUM OFF CACHE BOOL "Disable libsodium" FORCE)
set(ENABLE_CURVE OFF CACHE BOOL "Disable CURVE" FORCE)
set(ENABLE_WS OFF CACHE BOOL "Disable WebSocket transport" FORCE)
set(ZMQ_OUTPUT_BASENAME "zmq" CACHE STRING "libzmq output basename" FORCE)

FetchContent_Declare(
    libzmq
    URL "https://github.com/zeromq/libzmq/archive/refs/tags/v4.3.5.tar.gz"
    URL_HASH "SHA256=6c972d1e6a91a0ecd79c3236f04cf0126f2f4dfbbad407d72b4606a7ba93f9c6"
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
FetchContent_MakeAvailable(libzmq)

# libzmq's MSVC defaults append toolset/configuration/version text to the DLL.
# MetaTrader deployments intentionally use one stable dependency name.
set_target_properties(libzmq PROPERTIES
    OUTPUT_NAME "libzmq"
    DEBUG_POSTFIX ""
    RELEASE_POSTFIX ""
    RELWITHDEBINFO_POSTFIX ""
    MINSIZEREL_POSTFIX ""
)

add_library(zmq_bind SHARED
    wrap/windows/zmq_bind.cpp
    wrap/windows/zmq_bind.h
    "${MT_ZMQ_DEF_FILE}"
)
target_compile_features(zmq_bind PRIVATE cxx_std_17)
target_compile_definitions(zmq_bind PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(zmq_bind PRIVATE ZMQ_BIND_EXPORTS)
target_include_directories(zmq_bind PRIVATE wrap/windows)
target_link_libraries(zmq_bind PRIVATE libzmq)
set_target_properties(zmq_bind PROPERTIES
    OUTPUT_NAME "zmq_bind"
    PREFIX ""
)

if(MSVC)
    target_compile_options(zmq_bind PRIVATE /W4 /permissive- /EHsc)
endif()

set(MT_ZMQ_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist/${MT_ZMQ_PACKAGE_DIR}")
add_custom_command(TARGET zmq_bind POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${MT_ZMQ_DIST_DIR}/include"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:zmq_bind>" "${MT_ZMQ_DIST_DIR}/zmq_bind.dll"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:libzmq>" "${MT_ZMQ_DIST_DIR}/libzmq.dll"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/include/zmq_native.mqh"
            "${MT_ZMQ_DIST_DIR}/include/zmq_native.mqh"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/include/zmq_bind.mqh"
            "${MT_ZMQ_DIST_DIR}/include/zmq_bind.mqh"
    COMMENT "Packaging ${MT_ZMQ_PACKAGE_DIR}"
)

include(CTest)
if(BUILD_TESTING)
    add_library(zmq_bind_c_header_smoke OBJECT tests/c_header_smoke.c)
    target_compile_features(zmq_bind_c_header_smoke PRIVATE c_std_11)
    target_include_directories(zmq_bind_c_header_smoke PRIVATE wrap/windows)

    add_executable(zmq_bind_tests tests/native_smoke.cpp)
    target_compile_features(zmq_bind_tests PRIVATE cxx_std_17)
    target_include_directories(zmq_bind_tests PRIVATE wrap/windows)
    target_link_libraries(zmq_bind_tests PRIVATE zmq_bind libzmq)
    if(MSVC)
        target_compile_options(zmq_bind_tests PRIVATE /W4 /permissive- /EHsc /utf-8)
    endif()
    add_custom_command(TARGET zmq_bind_tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:libzmq>" "$<TARGET_FILE_DIR:zmq_bind_tests>/libzmq.dll"
        COMMENT "Copying libzmq next to native smoke test"
    )
    add_test(NAME zmq_bind_native_smoke COMMAND zmq_bind_tests)
    set_tests_properties(zmq_bind_native_smoke PROPERTIES TIMEOUT 30)

    add_executable(zmq_cross_arch_peer tests/cross_arch_peer.cpp)
    target_compile_features(zmq_cross_arch_peer PRIVATE cxx_std_17)
    target_include_directories(zmq_cross_arch_peer PRIVATE wrap/windows)
    target_link_libraries(zmq_cross_arch_peer PRIVATE zmq_bind libzmq)
    if(MSVC)
        target_compile_options(zmq_cross_arch_peer PRIVATE /W4 /permissive- /EHsc /utf-8)
    endif()
    add_custom_command(TARGET zmq_cross_arch_peer POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:libzmq>" "$<TARGET_FILE_DIR:zmq_cross_arch_peer>/libzmq.dll"
        COMMENT "Copying libzmq next to cross-architecture test peer"
    )
endif()
