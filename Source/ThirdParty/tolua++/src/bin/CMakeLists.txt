# Define target name
set (TARGET_NAME tolua++)

# Define source files
file (GLOB CPP_FILES *.c)
file (GLOB H_FILES *.h)
set (SOURCE_FILES ${CPP_FILES} ${H_FILES})

# Define dependency libs
set (LIBS ../../../../ThirdParty/Lua/src)
set (LINK_LIBS_ONLY tolua++_static)
set (INCLUDE_DIRS_ONLY ../../include)
if (NOT MSVC)
    set (LINK_LIBS_ONLY ${LINK_LIBS_ONLY} m)
endif ()

# Setup target
setup_executable ()
