Urho3D - cross-platform rendering and game engine
-------------------------------------------------

http://urho3d.googlecode.com

Licensed under the MIT license, see License.txt for details.


Credits
-------

Urho3D engine & example code by Lasse ��rni (loorni@gmail.com)

Urho3D is greatly inspired by OGRE (http://www.ogre3d.org) and Horde3D
(http://www.horde3d.org). Additional inspiration & research used:
- Rectangle packing by Jukka Jyl�nki (clb)
  http://clb.demon.fi/projects/rectangle-bin-packing
- Tangent generation from Terathon
  http://www.terathon.com/code/tangent.html
- Fast, Minimum Storage Ray/Triangle Intersection by M�ller & Trumbore
  http://www.graphics.cornell.edu/pubs/1997/MT97.pdf
- Linear-Speed Vertex Cache Optimisation by Tom Forsyth
  http://home.comcast.net/~tom_forsyth/papers/fast_vert_cache_opt.html
- Software rasterization of triangles based on Chris Hecker's
  Perspective Texture Mapping series in the Game Developer magazine
  http://chrishecker.com/Miscellaneous_Technical_Articles
- Networked Physics by Glenn Fiedler
  http://gafferongames.com/game-physics/networked-physics/
- Euler Angle Formulas by David Eberly
  http://www.geometrictools.com/Documentation/EulerAngles.pdf
- Red Black Trees by Julienne Walker 
  http://eternallyconfuzzled.com/tuts/datastructures/jsw_tut_rbtree.aspx
- Comparison of several sorting algorithms by Juha Nieminen 
  http://warp.povusers.org/SortComparison/

Urho3D uses the following third-party libraries:
- AngelScript 2.21.1 (http://www.angelcode.com/angelscript/)
- FreeType 2.3.12 (http://www.freetype.org/)
- GLee 5.4 (http://elf-stone.com/)
- GLFW 3.0 WIP (http://www.glfw.org/)
- kNet (https://bitbucket.org/clb/knet)
- Open Asset Import Library, svn rev 1062 (http://assimp.sourceforge.net/)
- Open Dynamics Engine, svn rev 1806 (http://www.ode.org/)
- PortAudio V19 (http://www.portaudio.com/)
- StanHull (http://codesuppository.blogspot.com/2006/03/
  john-ratcliffs-code-suppository-blog.html)
- stb_image 1.29 (http://nothings.org/)
- stb_vorbis 0.99996 (http://nothings.org/)
- TinyXML 2.6.1 (http://www.grinninglizard.com/tinyxml/)

Jack and mushroom models from the realXtend project. (http://www.realxtend.org)

Ninja model and smoke/flare/status bar textures from OGRE.

BlueHighway font from Larabie Fonts.

Anonymous Pro font by Mark Simonson.

NinjaSnowWar sounds by Veli-Pekka T�til�.


Documentation
-------------

Urho3D classes have been sparsely documented using Doxygen notation. To
generate documentation into the "Docs" subdirectory, open the Doxyfile in the
root directory with doxywizard and click "Run doxygen" from the "Run" tab.
Get Doxygen from http://www.doxygen.org


Building prerequisites
----------------------

Although all required third-party libraries are included as source code, there
are system-level dependencies that must be satisfied before Urho3D can be built
successfully:

- For Windows, the DirectX SDK needs to be installed and its include and library
  directories set as Visual Studio global directories (Tools -> Options ->
  Projects and Solutions -> VC++ Directories in VS2008.)

- For Linux, the following development packages need to be installed:
  libx11-dev, libxrandr-dev, libgl1-mesa-dev, libasound2-dev.

- For Mac OS X, the Xcode developer tools package should include everything
  necessary.


Build process
-------------

Urho3D uses CMake (http://www.cmake.org) to build. The process has two steps:

1) Run CMake in the root directory with your preferred toolchain specified to
generate the build files. You can use the batch files or shell scripts provided:
cmake_vs2008.bat or cmake_vs2010.bat on Windows, and cmake_gcc.sh on Linux and
Mac OS X.

2) For Visual Studio, open Urho3D.sln and build the configuration(s) you like.
For gcc, execute make (by default, cmake_gcc.sh specifies to make a Release
build.)

The build process will also compile models and shaders from the Source_Asset
directory into Bin/Data/Models & Bin/Data/Shaders. On Windows & Direct3D9,
shader compilation requires the D3DX library from the DirectX runtime or SDK to
be available.

After the build is complete, the programs can be run from the Bin directory.

To run Urho3D from the Visual Studio debugger, set it as a startup project and
enter its relative path and filename into Properties -> Debugging -> Command:
..\Bin\Urho3D.exe or ..\Bin\Urho3D_d.exe. Additionally, entering -w into
Debugging -> Command Arguments is highly recommended. This enables startup in
windowed mode: without it running into an exception or breakpoint will be
obnoxious as the mouse cursor will most probably be hidden.

To actually make Urho3D.exe do something useful, it must be supplied with the
name of the script file it should load and run. You can try for example the
following arguments: Scripts/TestScene.as -w


History
-------

V1.0    - Original release.

V1.1    - Object and scene model refactoring.
        - Automatic serialization of scene objects via attributes.
        - Added OpenGL and cross-platform support.
        - Switched to kNet library for networking.

V1.11   - Bugfixes and performance optimizations.
        - Added GraphicsTest example from V1.0 (now called TestSceneOld.)
        - Added fallback mode, which is used if multiple render targets or
          hardware shadow maps are not available.

V1.12   - Manipulator gizmo and multi-editing in the editor.
        - Switched to forward rendering exclusively, which is optimized to do
          more work in the vertex shader.
        - Zone system refactoring. Objects check the zone they belong to for
          per-zone light masking, ambient light and fog settings.
        - Scripting API fixes and improvements.

