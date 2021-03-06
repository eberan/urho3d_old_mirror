#
# Copyright (c) 2008-2013 the Urho3D project.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#

# Set project name
project (Urho3D)

# Set minimum version
cmake_minimum_required (VERSION 2.8.6)

if (COMMAND cmake_policy)
    cmake_policy (SET CMP0003 NEW)
endif ()

# Add cmake modules search path
set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/CMake/Modules/")

# Include Urho3D cmake module
include (Urho3D-CMake-magic)

# Check existence of stdint.h for LibCpuId
include (CheckIncludeFiles)
CHECK_INCLUDE_FILES (stdint.h HAVE_STDINT_H)
if (HAVE_STDINT_H)
    add_definitions (-DHAVE_STDINT_H)
endif ()

# Add definitions for GLEW
if (NOT IOS AND NOT ANDROID AND NOT RASPI AND USE_OPENGL)
    add_definitions (-DGLEW_STATIC)
    add_definitions (-DGLEW_NO_GLU)
endif ()

# Add global include directories for export header and SDL header
include_directories (${CMAKE_BINARY_DIR}/Engine ThirdParty/SDL/include)

# Setup the compiler flags for building shared library
if (URHO3D_BUILD_TYPE STREQUAL SHARED AND NOT IOS AND NOT ANDROID)
    # Hide the symbols that are not explicitly marked for export
    add_compiler_export_flags ()
    # Use PIC on platforms that support it 
    set (CMAKE_POSITION_INDEPENDENT_CODE true)
endif ()

# Add targets
add_subdirectory (Engine/Audio)
add_subdirectory (Engine/Container)
add_subdirectory (Engine/Core)
add_subdirectory (Engine/Engine)
add_subdirectory (Engine/Graphics)
add_subdirectory (Engine/Input)
add_subdirectory (Engine/IO)
add_subdirectory (Engine/Math)
add_subdirectory (Engine/Navigation)
add_subdirectory (Engine/Network)
add_subdirectory (Engine/Physics)
add_subdirectory (Engine/Resource)
add_subdirectory (Engine/Scene)
add_subdirectory (Engine/Script)
add_subdirectory (Engine/UI)
add_subdirectory (ThirdParty/AngelScript)
add_subdirectory (ThirdParty/Bullet)
add_subdirectory (ThirdParty/Detour)
add_subdirectory (ThirdParty/FreeType)
add_subdirectory (ThirdParty/kNet)
add_subdirectory (ThirdParty/PugiXml)
add_subdirectory (ThirdParty/Recast)
add_subdirectory (ThirdParty/SDL)
add_subdirectory (ThirdParty/StanHull)
add_subdirectory (ThirdParty/STB)
add_subdirectory (ThirdParty/JO)

# Alternative Lua bindings target
if (ENABLE_LUA)
    add_definitions (-DENABLE_LUA)
    add_subdirectory (ThirdParty/Lua)
    add_subdirectory (ThirdParty/tolua++/src/lib)
    add_subdirectory (Extras/LuaScript)
endif ()

if (NOT IOS AND NOT ANDROID)
    if (ENABLE_LUA)
        add_subdirectory (ThirdParty/tolua++/src/bin)
    endif ()
    if (NOT RASPI)
        if (USE_OPENGL)
            add_subdirectory (ThirdParty/GLEW)
        endif ()
        add_subdirectory (ThirdParty/LibCpuId)
    endif ()
endif ()    

# Urho3D build type is mutually exclusive due to potential different visibility (export) attribute
if (URHO3D_BUILD_TYPE MATCHES "STATIC|SHARED" AND NOT ANDROID)
    # Urho3D library
    add_subdirectory (Engine)
else ()
    # Urho3D script host application
    add_subdirectory (Tools/Urho3D)

    # Do not build tools for embedded system unless it is explicitly enabled
    if (NOT DEFINED ENABLE_TOOLS)
        if (RASPI)
            set (ENABLE_TOOLS 0)
        else ()
            set (ENABLE_TOOLS 1)
        endif ()
    endif ()
    
    if (NOT IOS AND NOT ANDROID)
        # Urho3D tools
        if (ENABLE_TOOLS)
            add_subdirectory (ThirdParty/Assimp)
            add_subdirectory (Tools/AssetImporter)
            add_subdirectory (Tools/OgreImporter)
            add_subdirectory (Tools/PackageTool)
            add_subdirectory (Tools/RampGenerator)
            add_subdirectory (Tools/ScriptCompiler)
            add_subdirectory (Tools/DocConverter)
        endif ()
    
        # Urho3D samples
        if (ENABLE_SAMPLES)
            add_subdirectory (Samples)
        endif ()
        
        # Urho3D extras
        # Todo: CharacterDemo will be moved to Samples
        if (ENABLE_EXTRAS)
            add_subdirectory (Extras/OgreBatchConverter)
            add_subdirectory (Extras/CharacterDemo)
        endif ()
    endif ()
    
    # Urho3D non-GL tools
    if (NOT USE_OPENGL AND ENABLE_TOOLS)
        add_subdirectory (ThirdParty/MojoShader)
        add_subdirectory (Tools/ShaderCompiler)
    endif ()
endif ()

# Generate platform specific export header file automatically
set_target_properties (Urho3D PROPERTIES DEFINE_SYMBOL URHO3D_EXPORTS)
generate_export_header (Urho3D EXPORT_MACRO_NAME URHO3D_API EXPORT_FILE_NAME Engine/Urho3D.h)

# Append Urho3D license notice to the export header file
file (READ ${CMAKE_SOURCE_DIR}/Engine/Urho3D.h.in LICENSE)
file (READ ${CMAKE_BINARY_DIR}/Engine/Urho3D.h EXPORT_DEFINE)
file (WRITE ${CMAKE_BINARY_DIR}/Engine/Urho3D.h ${LICENSE}${EXPORT_DEFINE})
