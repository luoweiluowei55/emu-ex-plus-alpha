cmake_minimum_required(VERSION 4.1)

project(
	Imagine
	VERSION 1.5.85
	DESCRIPTION "Game/Multimedia Engine"
	HOMEPAGE_URL "https://www.explusalpha.com/"
)

set(CMAKE_INSTALL_PREFIX "${IMAGINE_SDK_PLATFORM_PATH}" CACHE PATH "Installation directory" FORCE)

if(APPLE_PLATFORM)
	enable_language(OBJCXX)
endif()

printConfigInfo()
add_library(imagine STATIC)
set_target_properties(imagine PROPERTIES CXX_MODULE_STD ON)
set_target_properties(imagine PROPERTIES OUTPUT_NAME "imagine${GEN_TARGET_EXT}")
set_property(TARGET imagine PROPERTY C_STANDARD 23)
target_include_directories(imagine PRIVATE include)
target_compile_options(imagine PRIVATE -Werror)
install(TARGETS imagine DESTINATION lib)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/include/imagine" DESTINATION include)
add_subdirectory(src)

if(SUBENV STREQUAL pandora)
	addConfigEnable(imagine CONFIG_MACHINE_PANDORA)
endif()

generateConfigHeader(imagine)
evalPkgConfigCFlags(imagine)
writePkgConfigFiles(imagine)
