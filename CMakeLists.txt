# PowerNotepad --- Power Notepad

# CMake minimum version
cmake_minimum_required(VERSION 3.11)

# project name and languages
project(RNotepad CXX RC)

##############################################################################
# Debugging

if (DBG)
    add_definitions(-D_DEBUG=1)
endif()

##############################################################################
# Architecture

if (MSVC)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_definitions(-D_AMD64_ -D_M_AMD64)
    else()
        add_definitions(-D_X86_ -D_M_IX86)
    endif()
endif()

##############################################################################
# PCRE2 — fetched and built as a static 16-bit library

include(FetchContent)

FetchContent_Declare(
    pcre2
    URL      https://github.com/PCRE2Project/pcre2/releases/download/pcre2-10.47/pcre2-10.47.tar.bz2
    URL_HASH SHA256=47fe8c99461250d42f89e6e8fdaeba9da057855d06eb7fc08d9ca03fd08d7bc7
)

# Build only the 16-bit library; skip tools and tests to keep the build fast.
set(PCRE2_BUILD_PCRE2_8     OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2_16    ON  CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2_32    OFF CACHE BOOL "" FORCE)
set(PCRE2_SUPPORT_UNICODE   ON  CACHE BOOL "" FORCE)
set(PCRE2_BUILD_TESTS       OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2GREP   OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2TEST   OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS       OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(pcre2)

##############################################################################
# PowerNote.exe

add_executable(PowerNote WIN32
    dialog.cpp
    main.cpp
    printing.cpp
    regex_engine.cpp
    settings.cpp
    text.cpp
    rsrc.rc)

target_compile_definitions(PowerNote PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)

if (NOT MSVC)
    target_link_options(PowerNote BEFORE PRIVATE -municode)
endif()

target_link_libraries(PowerNote PRIVATE user32 gdi32 comctl32 comdlg32 advapi32 shell32 kernel32 pcre2-16-static)

if (MSVC)
    # Use wWinMain
    target_link_options(PowerNote PRIVATE "/entry:wWinMainCRTStartup")

    # No default manifest
    target_link_options(PowerNote PRIVATE /MANIFEST:NO)
endif()

##############################################################################
