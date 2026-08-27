cmake_minimum_required(VERSION 3.21)

project(DocuSearch
    VERSION 1.5.0
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
    # /EHa (instead of /EHsc) is REQUIRED for _set_se_translator() to
    # actually catch SEH exceptions in C++ try/catch blocks. Without it,
    # access violations inside Poppler/zlib/Windows.Media.Ocr still bypass our
    # catch handlers and terminate the process. See src/core/SehTranslator.h.
    add_compile_options(/permissive- /W3 /utf-8 /EHa /Zc:__cplusplus)
    add_link_options(/LARGEADDRESSAWARE)
endif()

# ============================================================
# Find dependencies
# ============================================================

# Options to disable Poppler entirely (it causes API issues with some
# vcpkg versions). With Poppler OFF, the app still works — just without
# PDF text extraction (PDFs flag as needsOcr and go through Windows.Media.Ocr instead).
# OCR is provided by Windows.Media.Ocr (WinRT) — no build-time deps, ships with the OS.
option(DOCUSEARCH_ENABLE_POPPLER   "Link Poppler for PDF text extraction" ON)

# NOTE: Tesseract is no longer used. The DOCUSEARCH_ENABLE_TESSERACT option
# (present in older versions) is a no-op. OCR is provided by Windows.Media.Ocr
# (the official WinRT OCR API) loaded at runtime via a separate helper
# exe (docusearch_ocr_helper).

# Qt 6
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Concurrent
    Sql
    Network
    Svg
    PrintSupport
)
qt_standard_project_setup()

# SQLite — prefer system, fall back to amalgamation
find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
    message(STATUS "SQLite3 not found via find_package; using bundled amalgamation.")
    set(SQLITE3_BUNDLED TRUE)
endif()

# OCR — Windows.Media.Ocr (WinRT, ships with Windows 10+), called at runtime
# via a separate helper exe (docusearch_ocr_helper). No build-time OCR
# dependency, no redistributed DLLs, no model files. No licensing risk.

# ============================================================
# Semantic Search (BGE Small EN v1.5 via ONNX Runtime)
# ============================================================
# Optional. When enabled, BGE embeddings power semantic search.
# Disabled by default — users must opt-in via -DDOCUSEARCH_HAS_ONNXRUNTIME=ON
# OR by installing onnxruntime via vcpkg manifest mode.
option(DOCUSEARCH_HAS_ONNXRUNTIME "Enable ONNX Runtime for semantic search" OFF)
find_package(onnxruntime CONFIG QUIET)
if(onnxruntime_FOUND)
    set(DOCUSEARCH_HAS_ONNXRUNTIME ON)
endif()

# PkgConfig is needed for Poppler detection.
find_package(PkgConfig QUIET)

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
# OCR Helper — Windows.Media.Ocr (official WinRT OCR API)
# ============================================================
# Standalone console exe that uses C++/WinRT to call
# Windows.Media.Ocr.OcrEngine.RecognizeAsync — the same OCR engine
# that powers Windows Search, Snipping Tool, and the Photos app.
#
# This is the officially-supported, royalty-free OCR API for any
# Windows app (commercial included — see docs/OCR_LICENSING.md).
#
# Architecture notes:
#   • runtimeobject.lib pulls in /INCLUDE:WINRT_CRT_MAIN which conflicts
#     with Qt's WIN32 entry point. We link it ONLY in the helper exe
#     target (a plain console app — no Qt entry point conflict).
#   • C++/WinRT headers ship with the Windows 10 SDK 17763+. No need
#     to invoke cppwinrt.exe — the Windows SDK is sufficient.
#   • windowsapp.lib provides the unified delay-load thunks for all
#     Windows.* APIs and is the recommended way to consume WinRT.
#
# Nothing else needs to ship with this helper — no DLLs, no model
# files, no install scripts. Windows.Media.Ocr is part of the OS.

add_executable(docusearch_ocr_helper src/ocr/ocr_helper_main.cpp)
if(MSVC)
    set_property(TARGET docusearch_ocr_helper PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    # windowsapp.lib = unified WinRT delay-load thunks (recommended).
    # runtimeobject.lib = WinRT apartment init + RoGetActivationFactory.
    target_link_libraries(docusearch_ocr_helper PRIVATE
        windowsapp runtimeobject)
    target_compile_options(docusearch_ocr_helper PRIVATE
        /std:c++20 /W3 /permissive- /utf-8 /EHa /Zc:__cplusplus /await
    )
    target_compile_definitions(docusearch_ocr_helper PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS
    )
endif()

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

# Force dynamic CRT (/MD) to match Qt6EntryPoint.lib which was compiled
# with /MD. Without this, the linker uses static CRT (/MT) from vcpkg's
# Poppler, causing:
#   LNK4098: defaultlib 'msvcrt.lib' conflicts with use of other libs
#   LNK2019: unresolved external symbol main (Qt6EntryPoint can't find
#            main because it expects dynamic CRT linkage)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# ============================================================
# Application sources
# ============================================================
set(APP_SOURCES
    # core
    src/core/Logger.cpp
    src/core/Config.cpp
    src/core/StringUtils.cpp
    src/core/FileUtils.cpp
    src/core/SehTranslator.cpp
    src/core/CrashHandler.cpp
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
    src/preview/TextPreview.cpp
    src/preview/ImagePreview.cpp
    src/preview/PdfPreview.cpp
    src/preview/OfficePreview.cpp
    src/preview/FilePreviewPane.cpp
    # embeddings (semantic search)
    src/embeddings/BgeTokenizer.cpp
    src/embeddings/BgeEmbeddingEngine.cpp
    src/embeddings/BgeEmbeddingDb.cpp
    src/embeddings/BgeService.cpp
    # search (hybrid)
    src/search/HybridSearchEngine.cpp
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
    src/ui/ResultItemDelegate.cpp
    src/ui/PreviewPane.cpp
    src/ui/MetadataPane.cpp
    src/ui/TagsNotesPane.cpp
    src/ui/IndexingProgress.cpp
    src/ui/SettingsDialog.cpp
    src/ui/SwitchControl.cpp
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
    src/core/SehTranslator.h
    src/core/CrashHandler.h
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
    src/ocr/WindowsOcrEngine.h
    src/ocr/OcrWorkerPool.h
    src/indexer/MetadataIndexer.h
    src/indexer/ContentIndexer.h
    src/indexer/PriorityScheduler.h
    src/indexer/Indexer.h
    src/search/QueryParser.h
    src/search/SearchEngine.h
    src/monitoring/FileWatcher.h
    src/preview/IFilePreview.h
    src/preview/TextPreview.h
    src/preview/ImagePreview.h
    src/preview/PdfPreview.h
    src/preview/OfficePreview.h
    src/preview/FilePreviewPane.h
    src/embeddings/BgeTokenizer.h
    src/embeddings/BgeEmbeddingEngine.h
    src/embeddings/BgeEmbeddingDb.h
    src/embeddings/BgeService.h
    src/search/HybridSearchEngine.h
    src/settings/SettingsManager.h
    src/backup/BackupManager.h
    src/win/JumpList.h
    src/ui/Theme.h
    src/ui/IconUtils.h
    src/ui/SearchBar.h
    src/ui/ResultsPane.h
    src/ui/ResultItemDelegate.h
    src/ui/PreviewPane.h
    src/ui/MetadataPane.h
    src/ui/TagsNotesPane.h
    src/ui/IndexingProgress.h
    src/ui/SettingsDialog.h
    src/ui/SwitchControl.h
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
    Qt6::PrintSupport
    SQLite::SQLite3
    ZLIB::ZLIB
    dbghelp        # MiniDumpWriteDump for crash handler
)

# OCR: No link needed — Windows.Media.Ocr is consumed via the separate
# docusearch_ocr_helper.exe target (see add_executable above). The helper
# links runtimeobject.lib + windowsapp.lib; the main Qt app does NOT.

if(POPPLER_FOUND)
    target_link_libraries(DocuSearch PRIVATE Poppler::Lib)
    target_compile_definitions(DocuSearch PRIVATE DOCUSEARCH_HAS_POPPLER=1)
    message(STATUS "Poppler: enabled")
else()
    message(WARNING "Poppler not found — PDF text extraction will be stubbed.")
endif()

# ── ONNX Runtime (optional, for BGE semantic search) ────────
if(DOCUSEARCH_HAS_ONNXRUNTIME)
    if(TARGET onnxruntime::onnxruntime)
        target_link_libraries(DocuSearch PRIVATE onnxruntime::onnxruntime)
        target_compile_definitions(DocuSearch PRIVATE DOCUSEARCH_HAS_ONNXRUNTIME=1)
        message(STATUS "ONNX Runtime: enabled via vcpkg target (semantic search available)")
    elseif(TARGET onnxruntime)
        target_link_libraries(DocuSearch PRIVATE onnxruntime)
        target_compile_definitions(DocuSearch PRIVATE DOCUSEARCH_HAS_ONNXRUNTIME=1)
        message(STATUS "ONNX Runtime: enabled via onnxruntime target (semantic search available)")
    elseif(DEFINED ONNXRUNTIME_ROOT)
        # Fallback: use pre-built ONNX Runtime binaries at ONNXRUNTIME_ROOT.
        # This is the typical layout from Microsoft's GitHub releases:
        #   <root>/include/  — headers
        #   <root>/lib/      — onnxruntime.lib + onnxruntime.dll (both!)
        # Some older releases put the DLL in <root>/bin/ — check both.
        target_include_directories(DocuSearch PRIVATE "${ONNXRUNTIME_ROOT}/include")
        target_link_libraries(DocuSearch PRIVATE
            "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
        # Find the DLL — it could be in lib/ (newer releases) or bin/ (older).
        set(ONNXRUNTIME_DLL_PATH "")
        if(EXISTS "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll")
            set(ONNXRUNTIME_DLL_PATH "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll")
        elseif(EXISTS "${ONNXRUNTIME_ROOT}/bin/onnxruntime.dll")
            set(ONNXRUNTIME_DLL_PATH "${ONNXRUNTIME_ROOT}/bin/onnxruntime.dll")
        endif()
        if(ONNXRUNTIME_DLL_PATH)
            add_custom_command(TARGET DocuSearch POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_DLL_PATH}"
                "$<TARGET_FILE_DIR:DocuSearch>")
            target_compile_definitions(DocuSearch PRIVATE DOCUSEARCH_HAS_ONNXRUNTIME=1)
            message(STATUS "ONNX Runtime: enabled via ONNXRUNTIME_ROOT=${ONNXRUNTIME_ROOT} "
                           "(DLL: ${ONNXRUNTIME_DLL_PATH})")
        else()
            message(WARNING "DOCUSEARCH_HAS_ONNXRUNTIME is ON and ONNXRUNTIME_ROOT is set, "
                            "but onnxruntime.dll not found in lib/ or bin/. "
                            "Semantic search will be compiled in but may fail to load.")
            target_compile_definitions(DocuSearch PRIVATE DOCUSEARCH_HAS_ONNXRUNTIME=1)
        endif()
    else()
        message(WARNING "DOCUSEARCH_HAS_ONNXRUNTIME is ON but onnxruntime target not found "
                        "and ONNXRUNTIME_ROOT is not set. Disabling ONNX Runtime.")
        set(DOCUSEARCH_HAS_ONNXRUNTIME OFF)
    endif()
else()
    message(STATUS "ONNX Runtime: disabled (semantic search unavailable; "
                    "keyword-only search will work)")
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
        # NOTE: runtimeobject.lib is NOT linked to the main Qt app.
        # It pulls in /INCLUDE:WINRT_CRT_MAIN which conflicts with Qt's
        # WIN32 entry point (LNK2019: unresolved external symbol main).
        # All WinRT calls live in the separate docusearch_ocr_helper.exe
        # target — a plain console exe where WINRT_CRT_MAIN fits cleanly.
    )
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
