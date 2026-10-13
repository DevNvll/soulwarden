# Standalone local backend. It builds as a native Linux program or as a MinGW Windows program.
# It has no link to the game hook and no access to game memory.
#
# Targets:
#   soulwarden_backend_core    static library with all backend sources but main.cpp
#   soulwarden_backend         the backend program (main.cpp and the library)
#   soulwarden_backend_tests   test program with no game process (native target only,
#                         tools/test-soulwarden-backend.sh builds and runs it)

file(GLOB_RECURSE SOULWARDEN_BACKEND_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(FILTER SOULWARDEN_BACKEND_SOURCES EXCLUDE REGEX "/src/main\\.cpp$")

add_library(soulwarden_sqlite STATIC vendor/sqlite/sqlite3.c)
target_compile_definitions(soulwarden_sqlite PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_DQS=0
    SQLITE_OMIT_LOAD_EXTENSION)
target_include_directories(soulwarden_sqlite PUBLIC vendor/sqlite)

# Mbed TLS provides DTLS 1.2 for the game server transport. Socket and timer code stays in
# the backend, so the library platform modules for those are not built.
file(GLOB SOULWARDEN_MBEDTLS_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/mbedtls/library/*.c")
list(FILTER SOULWARDEN_MBEDTLS_SOURCES EXCLUDE REGEX "/(net_sockets|timing)\\.c$")
add_library(soulwarden_mbedtls STATIC ${SOULWARDEN_MBEDTLS_SOURCES})
target_include_directories(soulwarden_mbedtls
    PUBLIC vendor/mbedtls/include
    PRIVATE vendor/mbedtls/library)
target_compile_options(soulwarden_mbedtls PRIVATE -O2 -w)

add_library(soulwarden_backend_core STATIC ${SOULWARDEN_BACKEND_SOURCES})
target_include_directories(soulwarden_backend_core PUBLIC src vendor)
target_link_libraries(soulwarden_backend_core PUBLIC soulwarden_sqlite soulwarden_mbedtls)
target_compile_options(soulwarden_backend_core PRIVATE -O2 -Wall -Wextra -Wno-unused-parameter)
if(MINGW)
    target_compile_definitions(soulwarden_backend_core PUBLIC
        WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
    target_link_libraries(soulwarden_backend_core PUBLIC ws2_32 bcrypt)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(soulwarden_backend_core PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
endif()

add_executable(soulwarden_backend src/main.cpp)
target_link_libraries(soulwarden_backend PRIVATE soulwarden_backend_core)
target_compile_options(soulwarden_backend PRIVATE -O2 -Wall -Wextra -Wno-unused-parameter)
if(MINGW)
    target_link_options(soulwarden_backend PRIVATE -static)
endif()
set_target_properties(soulwarden_backend PROPERTIES OUTPUT_NAME "soulwarden-backend")

# The test program runs the game server with made-up clients: no socket, no game process, and
# a database in memory. It is not a product: it stays in the build directory.
if(NOT MINGW)
    file(GLOB_RECURSE SOULWARDEN_BACKEND_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    add_executable(soulwarden_backend_tests EXCLUDE_FROM_ALL ${SOULWARDEN_BACKEND_TEST_SOURCES})
    target_include_directories(soulwarden_backend_tests PRIVATE tests)
    target_link_libraries(soulwarden_backend_tests PRIVATE soulwarden_backend_core)
    target_compile_options(soulwarden_backend_tests PRIVATE -O1 -Wall -Wextra -Wno-unused-parameter)
    set_target_properties(soulwarden_backend_tests PROPERTIES OUTPUT_NAME "soulwarden-backend-tests")
endif()
