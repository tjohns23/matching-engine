cmake_minimum_required(VERSION 3.14)
project(OrderBook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Project Library (Engine and OrderBook)
add_library(order_book_core
    order-book.cpp
    matching-engine.cpp
)
target_include_directories(order_book_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# --- Fetch GoogleTest ---
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# For Windows: prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# --- Test Executable ---
enable_testing()

add_executable(order_book_tests test.cpp)
target_link_libraries(order_book_tests
    PRIVATE
    order_book_core
    GTest::gtest_main
)

# Statically link MinGW runtime to avoid missing DLL errors (0xc0000139)
if (MINGW)
    target_link_options(order_book_tests PRIVATE -static -static-libgcc -static-libstdc++)
endif()

include(GoogleTest)
gtest_discover_tests(order_book_tests)
