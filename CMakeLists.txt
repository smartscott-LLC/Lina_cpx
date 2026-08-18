cmake_minimum_required(VERSION 3.20)

# LINA Core Substrate — single-module C++20 kernel
# Blueprint spec: SECTION 8 (CMakeLists.txt). Staged build per D-021:
#   - LINA_ENABLE_STORAGE  OFF until the storage milestone (PostgreSQL/libpq)
#   - LINA_ENABLE_LLAMA    OFF until the host-adapter milestone (D-007)
#   - LINA_ENABLE_UI       OFF until the UI milestone (D-006)
project(lina_core VERSION 9.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Options ---------------------------------------------------------------
option(LINA_ENABLE_UI "Enable Qt6 UI" OFF)
option(LINA_ENABLE_LLAMA "Enable llama.cpp support" OFF)
option(LINA_ENABLE_STORAGE "Enable PostgreSQL + pgvector storage backend" OFF)

# --- Compiler flags (spec §8.1) --------------------------------------------
add_compile_options(
    -O3
    -march=native
    -Wall
    -Wextra
    -Werror
    -fstack-protector-strong
    -fvisibility=hidden
    -pthread
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# --- Dependencies ----------------------------------------------------------
# GNU MP (exact rational arithmetic) — always required
find_path(GMP_INCLUDE_DIR NAMES gmpxx.h)
find_library(GMP_LIBRARY NAMES gmp)
find_library(GMPXX_LIBRARY NAMES gmpxx)

if (NOT GMP_INCLUDE_DIR OR NOT GMP_LIBRARY OR NOT GMPXX_LIBRARY)
    message(FATAL_ERROR "GNU MP (libgmp / libgmpxx) is required")
endif()

# PostgreSQL / libpq — required only when storage is enabled (D-021)
if (LINA_ENABLE_STORAGE)
    find_package(PkgConfig REQUIRED)
    find_package(PostgreSQL REQUIRED)
endif()

# Qt6 — required only when the UI is enabled (D-006)
if (LINA_ENABLE_UI)
    find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui Network)
endif()

# --- Value Engine (Chamber 1) — GMP-only ------------------------------------
add_library(lina_value_engine STATIC src/value_engine.cpp)
target_link_libraries(lina_value_engine PUBLIC ${GMP_LIBRARY} ${GMPXX_LIBRARY})
target_include_directories(lina_value_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${GMP_INCLUDE_DIR}
)

# --- Tests ------------------------------------------------------------------
enable_testing()
add_executable(value_engine_tests tests/value_engine_tests.cpp)
target_link_libraries(value_engine_tests PRIVATE lina_value_engine)
add_test(NAME value_engine_tests COMMAND value_engine_tests)

# --- Full executable (converges to spec §8.1 at orchestrator milestone) -----
if (LINA_ENABLE_STORAGE)
    add_executable(lina_core
        src/lina_core.cpp
        src/value_engine.cpp
        src/memory_module.cpp
        src/postgres_backend.cpp
        src/main.cpp
    )
    target_link_libraries(lina_core
        ${GMP_LIBRARY}
        ${GMPXX_LIBRARY}
        ${PostgreSQL_LIBRARIES}
        pthread
    )
    target_include_directories(lina_core PRIVATE
        ${GMP_INCLUDE_DIR}
        ${PostgreSQL_INCLUDE_DIRS}
    )
    if (LINA_ENABLE_UI)
        target_link_libraries(lina_core Qt6::Widgets Qt6::Core Qt6::Gui Qt6::Network)
    endif()
    if (LINA_ENABLE_LLAMA)
        target_link_libraries(lina_core llama)
    endif()
    set_target_properties(lina_core PROPERTIES
        WIN32_EXECUTABLE TRUE
        MACOSX_BUNDLE TRUE
    )
    install(TARGETS lina_core DESTINATION bin)
    install(FILES sql/lina_schema.sql DESTINATION share/lina)
endif()
