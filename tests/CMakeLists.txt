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
    ../src/core/TextQuality.cpp
    ../src/core/FileUtils.cpp
    ../src/database/Database.cpp
    ../src/database/Schema.cpp
    ../src/database/FileRepository.cpp
    ../src/search/QueryParser.cpp
    ../src/search/SearchEngine.cpp
    ../src/indexer/PriorityScheduler.cpp
    ../src/embeddings/BgeTokenizer.cpp
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
        target_compile_options(${name} PRIVATE /permissive- /W3 /utf-8 /EHa /Zc:__cplusplus)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# ---- The tests --------------------------------------------------------
docusearch_add_test(tst_StringUtils)
docusearch_add_test(tst_TextQuality)
docusearch_add_test(tst_FileUtils)
docusearch_add_test(tst_QueryParser)
docusearch_add_test(tst_PriorityScheduler)
docusearch_add_test(tst_FileRepository)
docusearch_add_test(tst_FtsTokenizer)
docusearch_add_test(tst_BgeTokenizer)

# ---- OCR helper test (separate function — no SQLite deps) -------------
# Verifies the Windows.Media.Ocr helper exe crash safety. Tests that the
# helper exits cleanly on missing files / corrupt images without
# crashing (which would otherwise kill the main DocuSearch app via
# QProcess::state detection).
if(MSVC)
    add_executable(tst_OcrHelper tst_OcrHelper.cpp)
    target_include_directories(tst_OcrHelper PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/core
    )
    target_link_libraries(tst_OcrHelper PRIVATE
        Qt6::Test
        Qt6::Core
    )
    target_compile_definitions(tst_OcrHelper PRIVATE
        QT_DISABLE_DEPRECATED_BEFORE=0x060000
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
        _CRT_SECURE_NO_WARNINGS
    )
    set_target_properties(tst_OcrHelper PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(tst_OcrHelper PRIVATE /permissive- /W3 /utf-8 /EHa /Zc:__cplusplus)
    add_test(NAME tst_OcrHelper COMMAND tst_OcrHelper)
endif()

# ---- Extractor fuzz test (verifies SEH protection) --------------------
# Generates random/malformed PDFs/DOCXs and verifies the extractor
# catches every failure rather than crashing.
if(MSVC AND DOCUSEARCH_ENABLE_POPPLER)
    add_executable(tst_ExtractorFuzz tst_ExtractorFuzz.cpp
        ${CMAKE_SOURCE_DIR}/src/core/SehTranslator.cpp
        ${CMAKE_SOURCE_DIR}/src/core/Logger.cpp
        ${CMAKE_SOURCE_DIR}/src/core/Config.cpp
        ${CMAKE_SOURCE_DIR}/src/core/StringUtils.cpp
        ${CMAKE_SOURCE_DIR}/src/core/FileUtils.cpp
        ${CMAKE_SOURCE_DIR}/src/documents/TextExtractor.cpp
        ${CMAKE_SOURCE_DIR}/src/documents/PdfExtractor.cpp
        ${CMAKE_SOURCE_DIR}/src/documents/DocxExtractor.cpp
        ${CMAKE_SOURCE_DIR}/src/documents/XlsxExtractor.cpp
        ${CMAKE_SOURCE_DIR}/src/documents/PptxExtractor.cpp
        ${CMAKE_SOURCE_DIR}/src/documents/DocumentExtractorRegistry.cpp
    )
    target_include_directories(tst_ExtractorFuzz PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/documents
    )
    target_link_libraries(tst_ExtractorFuzz PRIVATE
        Qt6::Test
        Qt6::Core
        Poppler::Lib
        ZLIB::ZLIB
    )
    target_compile_definitions(tst_ExtractorFuzz PRIVATE
        QT_DISABLE_DEPRECATED_BEFORE=0x060000
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
        _CRT_SECURE_NO_WARNINGS
        DOCUSEARCH_HAS_POPPLER=1
    )
    set_target_properties(tst_ExtractorFuzz PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(tst_ExtractorFuzz PRIVATE /permissive- /W3 /utf-8 /EHa /Zc:__cplusplus)
    add_test(NAME tst_ExtractorFuzz COMMAND tst_ExtractorFuzz)
endif()
