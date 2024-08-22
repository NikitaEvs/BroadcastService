cmake_minimum_required(VERSION 3.9)
project(BroadcastService)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT DEFINED ASAN) 
    set(ASAN OFF)
endif()
if (NOT DEFINED TSAN)
    set(TSAN OFF)
endif()
if (NOT DEFINED UNIT_TESTS)
    set(UNIT_TESTS OFF)
endif()

string(CONCAT CMAKE_CXX_FLAGS_COMMON_STR ""
                        "-Werror -Wall -Wconversion -Wfloat-equal "
                        "-Wpedantic -Wpointer-arith -Wswitch-default "
                        "-Wpacked -Wextra -Winvalid-pch "
                        "-Wmissing-field-initializers "
                        "-Wunreachable-code -Wcast-align -Wcast-qual "
                        "-Wdisabled-optimization -Wformat=2 "
                        "-Wformat-nonliteral -Wuninitialized "
                        "-Wformat-security -Wformat-y2k -Winit-self "
                        "-Wmissing-declarations -Wmissing-include-dirs "
                        "-Wredundant-decls -Wstrict-overflow=5 -Wundef "
                        "-Wno-unused -Wctor-dtor-privacy -Wsign-promo "
                        "-Woverloaded-virtual -Wold-style-cast")
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    string(CONCAT CMAKE_CXX_FLAGS_STR "${CMAKE_CXX_FLAGS_COMMON_STR} "
                        "-Wlogical-op -Wstrict-null-sentinel -Wnoexcept")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_STR}")
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if (ASAN)
        string(CONCAT CMAKE_CXX_FLAGS_STR "${CMAKE_CXX_FLAGS_COMMON_STR} " "-fsanitize=address,undefined -fno-sanitize-recover=all")
    elseif (TSAN)
        string(CONCAT CMAKE_CXX_FLAGS_STR "${CMAKE_CXX_FLAGS_COMMON_STR} " "-fsanitize=thread -fno-sanitize-recover=all")
    else()
        string(CONCAT CMAKE_CXX_FLAGS_STR "${CMAKE_CXX_FLAGS_COMMON_STR} " "-gdwarf-4")
    endif()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_STR}")
endif()

set(CMAKE_CXX_FLAGS_MINSIZEREL "-Os -DNDEBUG")

set(CMAKE_CXX_FLAGS_DEBUG "-Winline -g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")
set(CMAKE_CXX_FLAGS_MINSIZEREL "-Os -DNDEBUG")

include_directories(include)

set(FUTEX_SOURCES
    src/concurrency/futex.cpp)

set(CONTEXT_SOURCES
    src/concurrency/context.cpp
    src/concurrency/context.S)

set(RUNTIME_SOURCES
    ${FUTEX_SOURCES}
    src/runtime/api.cpp
    src/runtime/thread_pool.cpp
    src/runtime/just_execute.cpp
    src/runtime/packet_batcher.cpp
    src/runtime/event_loop.cpp
    src/runtime/async_mutex.cpp)

set(FIBER_SOURCES
    ${CONTEXT_SOURCES}
    ${RUNTIME_SOURCES}
    src/concurrency/fiber.cpp)

set(IO_SOURCES
    ${CONTEXT_SOURCES}
    ${RUNTIME_SOURCES}
    src/io/timer_service.cpp)

set(UTIL_SOURCES
    src/util/tests/transport_util.cpp)

set(CONCURRENCY_SOURCES ${FUTEX_SOURCES} ${CONTEXT_SOURCES} ${RUNTIME_SOURCES} ${FIBER_SOURCES} ${IO_SOURCES} ${UTIL_SOURCES})

add_library(runtime ${CONCURRENCY_SOURCES})

set(SOURCES src/main.cpp ${CONCURRENCY_SOURCES})

enable_language(ASM)

# Tests 
if (UNIT_TESTS) 
    get_directory_property(old_dir_compile_options COMPILE_OPTIONS)
    # Supress all warnings in gtest sources
    add_compile_options(-w)

    set(googletest_SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/googletest-src)
    set(googletest_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/googletest-build)

    include(FetchContent)
    FetchContent_Declare(
            googletest
            GIT_REPOSITORY    https://github.com/google/googletest.git
            GIT_TAG           main 
            SOURCE_DIR        "${googletest_SOURCE_DIR}"
            BINARY_DIR        "${googletest_SOURCE_DIR}"
    )

    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(BUILD_GTEST OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(googletest)
    include_directories(SYSTEM ${GTEST_INCLUDE_DIR})

    # Restore previous compilation flags
    set_directory_properties(PROPERTIES COMPILE_OPTIONS "${old_dir_compile_options}")

    # Concurrency tests 

    add_executable(concurrency_context_test src/tests/concurrency/context.cpp)
    target_link_libraries(concurrency_context_test ${CMAKE_THREAD_LIBS_INIT} gtest gtest_main runtime)

    add_executable(concurrency_fiber_sync_test src/tests/concurrency/fiber_sync.cpp)
    target_link_libraries(concurrency_fiber_sync_test ${CMAKE_THREAD_LIBS_INIT} gtest gtest_main runtime)

    add_executable(concurrency_fiber_test src/tests/concurrency/fiber.cpp)
    target_link_libraries(concurrency_fiber_test ${CMAKE_THREAD_LIBS_INIT} gtest gtest_main runtime)

    add_executable(concurrency_future_test src/tests/concurrency/future.cpp)
    target_link_libraries(concurrency_future_test ${CMAKE_THREAD_LIBS_INIT} gtest gtest_main runtime)

    add_executable(concurrency_mrmw_map_test src/tests/concurrency/mrmw_map.cpp)
    target_link_libraries(concurrency_mrmw_map_test ${CMAKE_THREAD_LIBS_INIT} gtest gtest_main runtime)

    add_executable(concurrency_spinlock_test src/tests/concurrency/spinlock.cpp)
    target_link_libraries(concurrency_spinlock_test ${CMAKE_THREAD_LIBS_INIT} gtest gtest_main runtime)

    add_executable(concurrency_task_queue_test src/tests/concurrency/task_queue.cpp)
    target_link_libraries(concurrency_task_queue_test ${CMAKE_THREAD_LIBS_INIT} gtest gtest_main runtime)

    add_executable(concurrency_wait_group_test src/tests/concurrency/wait_group.cpp)
    target_link_libraries(concurrency_wait_group_test ${CMAKE_THREAD_LIBS_INIT} gtest gtest_main runtime)

    add_executable(concurrency_shared_budget_test src/tests/concurrency/budget.cpp)
    target_link_libraries(concurrency_shared_budget_test ${CMAKE_THREAD_LIBS_INIT} gtest gtest_main runtime)

    # IO tests

    add_executable(io_timer_service_test src/tests/io/timer_service.cpp)
    target_link_libraries(io_timer_service_test ${CMAKE_THREAD_LIBS_INIT} gtest gtest_main runtime)

    # IO/Transport tests

    add_executable(io_transport_fairloss_channel_app src/tests/io/transport/fairloss_channel.cpp)
    target_link_libraries(io_transport_fairloss_channel_app ${CMAKE_THREAD_LIBS_INIT} gtest gtest_main runtime)

    add_executable(io_transport_payload_test src/tests/io/transport/payload.cpp)
    target_link_libraries(io_transport_payload_test ${CMAKE_THREAD_LIBS_INIT} gtest gtest_main runtime)

    add_executable(io_transport_reliable_channel_app src/tests/io/transport/reliable_channel.cpp)
    target_link_libraries(io_transport_reliable_channel_app ${CMAKE_THREAD_LIBS_INIT} gtest gtest_main runtime)

    add_executable(io_transport_udp_socket_app src/tests/io/transport/udp_socket.cpp)
    target_link_libraries(io_transport_udp_socket_app ${CMAKE_THREAD_LIBS_INIT} gtest gtest_main runtime)

    # Primitives tests

    add_executable(primitives_best_effort_broadcast_app src/tests/primitives/best_effort_broadcast.cpp)
    target_link_libraries(primitives_best_effort_broadcast_app ${CMAKE_THREAD_LIBS_INIT} gtest gtest_main runtime)

    add_executable(primitives_uniform_reliable_broadcast_app src/tests/primitives/uniform_reliable_broadcast.cpp)
    target_link_libraries(primitives_uniform_reliable_broadcast_app ${CMAKE_THREAD_LIBS_INIT} gtest gtest_main runtime)

    add_executable(primitives_fifo_broadcast_app src/tests/primitives/fifo_broadcast.cpp)
    target_link_libraries(primitives_fifo_broadcast_app ${CMAKE_THREAD_LIBS_INIT} gtest gtest_main runtime)

    add_executable(primitives_lattice_agreement_app src/tests/primitives/lattice_agreement.cpp)
    target_link_libraries(primitives_lattice_agreement_app ${CMAKE_THREAD_LIBS_INIT} gtest gtest_main runtime)

    # Runtime tests

    add_executable(runtime_async_mutex_test src/tests/runtime/async_mutex.cpp)
    target_link_libraries(runtime_async_mutex_test ${CMAKE_THREAD_LIBS_INIT} gtest gtest_main runtime)

    add_executable(runtime_event_loop_test src/tests/runtime/event_loop.cpp)
    target_link_libraries(runtime_event_loop_test ${CMAKE_THREAD_LIBS_INIT} gtest gtest_main runtime)

    add_executable(runtime_thread_pool_test src/tests/runtime/thread_pool.cpp)
    target_link_libraries(runtime_thread_pool_test ${CMAKE_THREAD_LIBS_INIT} gtest gtest_main runtime)

    # Util tests

    add_executable(util_intrusive_list_test src/tests/util/intrusive_list.cpp)
    target_link_libraries(util_intrusive_list_test ${CMAKE_THREAD_LIBS_INIT} gtest gtest_main runtime)

    add_executable(util_result_test src/tests/util/result.cpp)
    target_link_libraries(util_result_test ${CMAKE_THREAD_LIBS_INIT} gtest gtest_main runtime)
endif (UNIT_TESTS)

find_package(Threads)
add_executable(da_proc ${SOURCES})
target_link_libraries(da_proc ${CMAKE_THREAD_LIBS_INIT})
