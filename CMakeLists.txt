# PowerNotepad --- Power Notepad

# CMake minimum version
cmake_minimum_required(VERSION 3.10)

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
# pnotepad.exe

add_executable(pnotepad WIN32
    dialog.cpp
    main.cpp
    printing.cpp
    settings.cpp
    text.cpp
    rsrc.rc)

target_compile_definitions(pnotepad PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)

if (NOT MSVC)
    target_link_options(pnotepad BEFORE PRIVATE -municode)
endif()

target_link_libraries(pnotepad PRIVATE user32 gdi32 comctl32 comdlg32 advapi32 shell32 kernel32)

if (MSVC)
    # Use wWinMain
    target_link_options(pnotepad PRIVATE "/entry:wWinMainCRTStartup")

    # No default manifest
    target_link_options(pnotepad PRIVATE /MANIFEST:NO)
endif()

##############################################################################
