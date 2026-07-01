cmake_minimum_required(VERSION 3.21)

project(DocuSearch
    VERSION 1.0.0
    DESCRIPTION "Offline Intelligent Document Search & OCR System"
    LANGUAGES CXX
)

# ============================================================
# Global settings
# ============================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Use Qt6 autogen
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Output to one bin dir for easy runtime
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Windows: statically link runtime so the app runs without VC++ redistributable
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/permissive- /W3 /utf-8 /EHsc /Zc:__cplusplus)
    add_link_options(/LARGEADDRESSAWARE)
endif()

# ============================================================
# Find dependencies
# ============================================================

# Options to disable Tesseract/Poppler entirely (they cause API issues
# with some vcpkg versions). With both OFF, the app still works — just
# without OCR and without PDF text extraction (PDFs flag as needsOcr).
option(DOCUSEARCH_ENABLE_TESSERACT "Link Tesseract OCR engine" ON)
option(DOCUSEARCH_ENABLE_POPPLER   "Link Poppler for PDF text extraction" ON)

# Qt 6
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Concurrent
    Sql
    Network
    Svg
)
qt_standard_project_setup()

# SQLite — prefer system, fall back to amalgamation
find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
    message(STATUS "SQLite3 not found via find_package; using bundled amalgamation.")
    set(SQLITE3_BUNDLED TRUE)
endif()

# Tesseract + Leptonica — REMOVED.
# OCR is now handled by Windows OCR (Windows.Media.Ocr via WinRT),
# which is built into Windows 10/11 and requires no third-party
# dependencies. This removes ~40 MB from the build and eliminates
# the crash-prone Tesseract/Leptonica DLLs.

# Poppler (Qt binding preferred)
if(DOCUSEARCH_ENABLE_POPPLER)
if(PkgConfig_FOUND)
    pkg_check_modules(POPPLER IMPORTED_TARGET poppler-cpp)
endif()
if(NOT POPPLER_FOUND)
    find_path(POPPLER_INCLUDE_DIR poppler-document.h PATH_SUFFIXES poppler/cpp)
    find_library(POPPLER_LIBRARY NAMES poppler-cpp)
    if(POPPLER_INCLUDE_DIR AND POPPLER_LIBRARY)
        set(POPPLER_FOUND TRUE)
        add_library(Poppler::Lib INTERFACE IMPORTED)
        set_target_properties(Poppler::Lib PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${POPPLER_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES      "${POPPLER_LIBRARY}"
        )
    endif()
endif()
endif()

# Minizip (for OOXML .zip reading) — usually bundled with zlib
find_package(ZLIB REQUIRED)

# ============================================================
# Bundled SQLite3 (fallback)
# ============================================================
if(SQLITE3_BUNDLED)
    set(SQLITE3_AMALGAMATION ${CMAKE_SOURCE_DIR}/third_party/sqlite3/sqlite3.c)
    if(EXISTS ${SQLITE3_AMALGAMATION})
        add_library(sqlite3_bundled STATIC ${SQLITE3_AMALGAMATION})
        target_include_directories(sqlite3_bundled PUBLIC ${CMAKE_SOURCE_DIR}/third_party/sqlite3)
        target_compile_definitions(sqlite3_bundled PRIVATE
            SQLITE_ENABLE_FTS5
            SQLITE_ENABLE_JSON1
            SQLITE_ENABLE_RTREE
            SQLITE_THREADSAFE=1
            SQLITE_HAS_CODEC=0
            SQLITE_OMIT_LOAD_EXTENSION=1
            SQLITE_DEFAULT_MEMSTATUS=0
            SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
            SQLITE_LIKE_DOESNT_MATCH_BLOBS
            SQLITE_DQS=0
            SQLITE_MAX_EXPR_DEPTH=0
            SQLITE_OMIT_DEPRECATED
        )
        add_library(SQLite::SQLite3 ALIAS sqlite3_bundled)
    else()
        message(FATAL_ERROR "SQLite3 amalgamation not found at ${SQLITE3_AMALGAMATION}. "
            "Install SQLite3 dev package or download amalgamation into third_party/sqlite3/.")
    endif()
endif()

# ============================================================
# Application sources
# ============================================================
set(APP_SOURCES
    # core
    src/core/Logger.cpp
    src/core/Config.cpp
    src/core/StringUtils.cpp
    src/core/FileUtils.cpp
    # database
    src/database/Database.cpp
    src/database/Schema.cpp
    src/database/FileRepository.cpp
    # documents
    src/documents/TextExtractor.cpp
    src/documents/PdfExtractor.cpp
    src/documents/DocxExtractor.cpp
    src/documents/XlsxExtractor.cpp
    src/documents/PptxExtractor.cpp
    src/documents/DocumentExtractorRegistry.cpp
    # ocr
    src/ocr/WindowsOcrEngine.cpp
    src/ocr/OcrWorkerPool.cpp
    # indexer
    src/indexer/MetadataIndexer.cpp
    src/indexer/ContentIndexer.cpp
    src/indexer/PriorityScheduler.cpp
    src/indexer/Indexer.cpp
    # search
    src/search/QueryParser.cpp
    src/search/SearchEngine.cpp
    # monitoring
    src/monitoring/FileWatcher.cpp
    # preview
    src/preview/ThumbnailGenerator.cpp
    # settings
    src/settings/SettingsManager.cpp
    # backup
    src/backup/BackupManager.cpp
    # windows native (Win11 backdrop + jump list) — only compiled on Windows
    $<$<PLATFORM_ID:Windows>:src/win/JumpList.cpp>
    # ui
    src/ui/Theme.cpp
    src/ui/SearchBar.cpp
    src/ui/ResultsPane.cpp
    src/ui/PreviewPane.cpp
    src/ui/MetadataPane.cpp
    src/ui/TagsNotesPane.cpp
    src/ui/IndexingProgress.cpp
    src/ui/SettingsDialog.cpp
    src/ui/MainWindow.cpp
    # entry
    src/main.cpp
)

set(APP_HEADERS
    src/core/Logger.h
    src/core/Config.h
    src/core/Constants.h
    src/core/Types.h
    src/core/StringUtils.h
    src/core/FileUtils.h
    src/database/Database.h
    src/database/Schema.h
    src/database/FileRepository.h
    src/documents/IDocumentExtractor.h
    src/documents/TextExtractor.h
    src/documents/PdfExtractor.h
    src/documents/DocxExtractor.h
    src/documents/XlsxExtractor.h
    src/documents/PptxExtractor.h
    src/documents/DocumentExtractorRegistry.h
    src/ocr/OcrEngine.h
    src/ocr/OcrWorkerPool.h
    src/indexer/MetadataIndexer.h
    src/indexer/ContentIndexer.h
    src/indexer/PriorityScheduler.h
    src/indexer/Indexer.h
    src/search/QueryParser.h
    src/search/SearchEngine.h
    src/monitoring/FileWatcher.h
    src/preview/ThumbnailGenerator.h
    src/settings/SettingsManager.h
    src/backup/BackupManager.h
    src/win/JumpList.h
    src/ui/Theme.h
    src/ui/SearchBar.h
    src/ui/ResultsPane.h
    src/ui/PreviewPane.h
    src/ui/MetadataPane.h
    src/ui/TagsNotesPane.h
    src/ui/IndexingProgress.h
    src/ui/SettingsDialog.h
    src/ui/MainWindow.h
)

add_executable(DocuSearch WIN32 ${APP_SOURCES} ${APP_HEADERS} resources/app.qrc)

# ---- Windows 11 resources: manifest + icon + version info ----------------
# rc.exe compiles app.rc → app.res, which the linker embeds into DocuSearch.exe.
# The .rc references DocuSearch.exe.manifest and icons/DocuSearch.ico, so both
# must exist at configure time. On non-Windows hosts (e.g., CI sanity builds)
# we skip the RC step so the project still configures cleanly.
if(WIN32)
    set(APP_RC ${CMAKE_SOURCE_DIR}/resources/app.rc)
    target_sources(DocuSearch PRIVATE ${APP_RC})
    source_group("Resources" FILES
        ${APP_RC}
        ${CMAKE_SOURCE_DIR}/resources/DocuSearch.exe.manifest
        ${CMAKE_SOURCE_DIR}/resources/icons/DocuSearch.ico
    )
    # Tell MSVC to compile .rc as UTF-8 with BOM-aware parsing
    if(MSVC)
        set_source_files_properties(${APP_RC} PROPERTIES
            COMPILE_FLAGS "/utf-8 /nologo")
        # CRITICAL: app.rc already embeds the manifest via RT_MANIFEST.
        # Tell the MSVC linker to NOT also generate and embed a default
        # manifest (otherwise we get CVTRES CVT1100: duplicate resource).
        set_target_properties(DocuSearch PROPERTIES
            LINK_FLAGS "/MANIFEST:NO")
    endif()
endif()

target_include_directories(DocuSearch PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/database
    ${CMAKE_SOURCE_DIR}/src/documents
)

target_link_libraries(DocuSearch PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Sql
    Qt6::Network
    Qt6::Svg
    SQLite::SQLite3
    ZLIB::ZLIB
)

if(POPPLER_FOUND)
    target_link_libraries(DocuSearch PRIVATE Poppler::Lib)
    target_compile_definitions(DocuSearch PRIVATE DOCUSEARCH_HAS_POPPLER=1)
    message(STATUS "Poppler: enabled")
else()
    message(WARNING "Poppler not found — PDF text extraction will be stubbed.")
endif()

target_compile_definitions(DocuSearch PRIVATE
    QT_DISABLE_DEPRECATED_BEFORE=0x060000
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
    _CRT_SECURE_NO_WARNINGS
)

# Windows-specific link libs
if(WIN32)
    target_link_libraries(DocuSearch PRIVATE
        psapi      # process info for CPU throttling
        shlwapi    # path utilities
        shell32
        ole32
        oleaut32
        uuid
        winmm
        dwmapi     # DwmSetWindowAttribute (Win11 Mica backdrop)
        propsys    # IPropertyStore for jump-list tasks
        comctl32   # Common Controls v6 (manifest-driven)
        # NOTE: runtimeobject.lib is NOT linked here because it contains
        # /INCLUDE:WINRT_CRT_MAIN which conflicts with Qt's WIN32 entry
        # point. WindowsOcrEngine.cpp loads the WinRT functions dynamically
        # via LoadLibrary("api-ms-win-core-winrt-l1-1-0.dll").
    )
    # Enable Windows OCR via raw COM ABI + dynamic loading.
    if(MSVC)
        target_compile_definitions(DocuSearch PRIVATE DOCUSEARCH_HAS_WINDOWS_OCR=1)
        # WINRT_NO_MAKE_LINK (defined in WindowsOcrEngine.cpp) suppresses
        # the #pragma comment(lib, "runtimeobject.lib") from WinRT ABI
        # headers. No /NODEFAULTLIB needed.
    endif()
endif()

# Install targets (for packaging)
install(TARGETS DocuSearch RUNTIME DESTINATION bin)
install(DIRECTORY resources/themes DESTINATION share/DocuSearch)

# ============================================================
# Tests (optional)
# ============================================================
option(DOCUSEARCH_BUILD_TESTS "Build unit tests" OFF)
if(DOCUSEARCH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
