cmake_minimum_required(VERSION 4.1)

project(
	EmuFramework
	VERSION 1.5.85
	DESCRIPTION "Emulator App Framework"
	HOMEPAGE_URL "https://www.explusalpha.com/"
)

set(CMAKE_INSTALL_PREFIX "${IMAGINE_SDK_PLATFORM_PATH}" CACHE PATH "Installation directory" FORCE)

printConfigInfo()
add_library(emuframework STATIC)
set_target_properties(emuframework PROPERTIES CXX_MODULE_STD ON)
set_target_properties(emuframework PROPERTIES OUTPUT_NAME "emuframework${GEN_TARGET_EXT}")
target_include_directories(emuframework PRIVATE include)
target_compile_options(emuframework PRIVATE -Werror)
addPkgConfigDepMultiConfig(emuframework imagine)
addCxxModules(emuframework imagine glm)
install(TARGETS emuframework DESTINATION lib)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/include/emuframework" DESTINATION include)
add_subdirectory(src)
evalPkgConfigCFlags(emuframework)
writePkgConfigFiles(emuframework)

add_library(mednafen_common STATIC)
set_target_properties(mednafen_common PROPERTIES CXX_MODULE_STD ON)
set_target_properties(mednafen_common PROPERTIES OUTPUT_NAME "mednafen_common${GEN_TARGET_EXT}")
target_include_directories(mednafen_common PRIVATE
	include/shared/mednafen
	include/shared/lzma
	include/shared
	include
	src/shared
)
target_compile_options(mednafen_common PRIVATE -Wno-missing-field-initializers -Wno-unused-parameter -Wno-unused-function)
target_compile_definitions(mednafen_common PRIVATE HAVE_CONFIG_H)
addPkgConfigDepMultiConfig(mednafen_common imagine)
addCxxModules(mednafen_common imagine)
evalPkgConfigCFlags(mednafen_common)
install(TARGETS mednafen_common DESTINATION lib)
add_subdirectory(src/shared)
set_source_files_properties(src/shared/mednafen/sound/DSPUtility.cpp PROPERTIES COMPILE_OPTIONS "-fno-fast-math")
set_source_files_properties(src/shared/mednafen/hw_cpu/m68k/m68k.cpp PROPERTIES COMPILE_OPTIONS "-fno-sanitize=address,undefined")
