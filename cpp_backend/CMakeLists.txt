cmake_minimum_required(VERSION 3.16)
project(wa_api_server CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Cross-compilation for Android ARM64-v8a is driven by the NDK toolchain file
# (see build_android.sh). For host builds, we use the system toolchain.
# ---------------------------------------------------------------------------

# Header-only libraries (fetch via CMake FetchContent):
#   - CrowCpp  (single-header HTTP framework)
#   - asio      (standalone, header-only networking)
#   - sqlite3   (amalgamation, compiled here for static linking)
#   - nlohmann/json (header-only JSON)
include(FetchContent)

# --- asio (standalone) ---
FetchContent_Declare(asio
  URL https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-28-2.tar.gz
)
FetchContent_GetProperties(asio)
if(NOT asio_POPULATED)
  FetchContent_Populate(asio)
endif()
add_library(asio INTERFACE)
target_include_directories(asio INTERFACE ${asio_SOURCE_DIR}/asio/include)
target_compile_definitions(asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)

# --- CrowCpp ---
FetchContent_Declare(crow
  URL https://github.com/CrowCpp/Crow/archive/refs/tags/v1.2.0.tar.gz
)
FetchContent_GetProperties(crow)
if(NOT crow_POPULATED)
  FetchContent_Populate(crow)
endif()

# --- nlohmann/json ---
FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_GetProperties(json)
if(NOT json_POPULATED)
  FetchContent_Populate(json)
endif()

# --- sqlite3 amalgamation (compiled statically) ---
FetchContent_Declare(sqlite
  URL https://www.sqlite.org/2024/sqlite-amalgamation-3450200.zip
)
FetchContent_GetProperties(sqlite)
if(NOT sqlite_POPULATED)
  FetchContent_Populate(sqlite)
endif()
add_library(sqlite3 STATIC
  ${sqlite_SOURCE_DIR}/sqlite3.c
)
target_include_directories(sqlite3 PUBLIC ${sqlite_SOURCE_DIR})

# ---------------------------------------------------------------------------
# Threads (Crow/asio need pthreads)
# ---------------------------------------------------------------------------
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Our sources
# ---------------------------------------------------------------------------
add_executable(wa_api_server
  src/main.cpp
  src/config.cpp
  src/adb.cpp
  src/blast.cpp
  src/logger.cpp
  src/store.cpp
  src/auth.cpp
  src/util.cpp
)

target_include_directories(wa_api_server PRIVATE
  src
  ${crow_SOURCE_DIR}/include
  ${json_SOURCE_DIR}/single_include
)

target_link_libraries(wa_api_server PRIVATE
  asio
  sqlite3
  Threads::Threads
)

# Produce a statically-linked, self-contained binary.
set_target_properties(wa_api_server PROPERTIES
  OUTPUT_NAME "wa_apid"
  LINK_FLAGS "-static-libstdc++ -static-libgcc"
)
