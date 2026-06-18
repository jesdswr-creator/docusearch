# ============================================================
# tests/CMakeLists.txt — Unit-test wiring
# ============================================================
#
# Each test is a standalone Qt Test executable. They link against the
# SAME source files as the main app, but EXCLUDE main.cpp and the UI
# layer (which would pull in QtWidgets and the MainWindow). This keeps
# the tests fast and headless (QTEST_GUILESS_MAIN).
#
# Run via:
#   cmake -B build -S . -DDOCUSEARCH_BUILD_TESTS=ON
#   cmake --build build --config Release --parallel
#   ctest --test-dir build --output-on-failure
# ============================================================

# ---- Collect the non-UI sources shared with the main app --------------
set(TEST_SOURCES
    ../src/core/Logger.cpp
    ../src/core/Config.cpp
    ../src/core/StringUtils.cpp
    ../src/core/FileUtils.cpp
    ../src/database/Database.cpp
    ../src/database/Schema.cpp
    ../src/database/FileRepository.cpp
    ../src/search/QueryParser.cpp
    ../src/search/SearchEngine.cpp
    ../src/indexer/PriorityScheduler.cpp
)

find_package(Qt6 REQUIRED COMPONENTS Test Sql)
find_package(SQLite3 REQUIRED)

# Helper: declare one test executable
function(docusearch_add_test name)
    add_executable(${name} ${name}.cpp ${TEST_SOURCES})
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/database
        ${CMAKE_SOURCE_DIR}/src/search
        ${CMAKE_SOURCE_DIR}/src/indexer
    )
    target_link_libraries(${name} PRIVATE
        Qt6::Test
        Qt6::Sql
        SQLite::SQLite3
    )
    target_compile_definitions(${name} PRIVATE
        QT_DISABLE_DEPRECATED_BEFORE=0x060000
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
        _CRT_SECURE_NO_WARNINGS
    )
    if(MSVC)
        set_target_properties(${name} PROPERTIES
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_compile_options(${name} PRIVATE /permissive- /W3 /utf-8 /EHsc /Zc:__cplusplus)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# ---- The tests --------------------------------------------------------
docusearch_add_test(tst_StringUtils)
docusearch_add_test(tst_FileUtils)
docusearch_add_test(tst_QueryParser)
docusearch_add_test(tst_PriorityScheduler)
docusearch_add_test(tst_FileRepository)
