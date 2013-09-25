::
:: Copyright (c) 2008-2013 the Urho3D project.
::
:: Permission is hereby granted, free of charge, to any person obtaining a copy
:: of this software and associated documentation files (the "Software"), to deal
:: in the Software without restriction, including without limitation the rights
:: to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
:: copies of the Software, and to permit persons to whom the Software is
:: furnished to do so, subject to the following conditions:
::
:: The above copyright notice and this permission notice shall be included in
:: all copies or substantial portions of the Software.
::
:: THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
:: IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
:: FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
:: AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
:: LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
:: OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
:: THE SOFTWARE.
::

@echo off
:: Define USE_MKLINK to 1 to enable out-of-source build and symbolic linking of resources from Bin directory
if exist CMakeCache.txt. del /F CMakeCache.txt
if exist Source\Android\CMakeCache.txt. del /F Source\Android\CMakeCache.txt
if exist CMakeFiles. rd /S /Q CMakeFiles
if exist Source\Android\CMakeFiles. rd /S /Q Source\Android\CMakeFiles
set "build=Source\Android"
set "source=.."
set "use_mklink="
:loop
if not "%1" == "" (
    if "%1" == "-DUSE_MKLINK" set "use_mklink=%~2"
    shift
    shift
    goto loop
)
if "%use_mklink%" == "1" (
    cmake -E make_directory android-Build
    if exist android-Build\CMakeCache.txt. del /F android-Build\CMakeCache.txt
    if exist android-Build\CMakeFiles. rd /S /Q android-Build\CMakeFiles
    set "build=android-Build"
    set "source=..\Source"
    for %%d in (CoreData Data) do mklink /D "Source\Android\assets\%%d" "..\..\..\Bin\%%d"
    for %%d in (src res assets) do mklink /D "android-Build\%%d" "..\Source\Android\%%d"
    for %%f in (AndroidManifest.xml build.xml project.properties) do mklink "android-Build\%%f" "..\Source\Android\%%f"
)
cmake -E copy_if_different Docs\Doxyfile.in Doxyfile
echo on
cmake -E chdir %build% cmake -G "Unix Makefiles" -DANDROID=1 -DCMAKE_TOOLCHAIN_FILE=%source%\CMake\Toolchains\android.toolchain.cmake -DLIBRARY_OUTPUT_PATH_ROOT=.  %* %source%
