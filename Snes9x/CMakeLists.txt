set(CMAKE_CXX_MODULE_STD ON)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "0e5b6991-d74f-4b3d-a41c-cf096e0b2508")

cmake_minimum_required(VERSION 4.1)

project(
	Snes9xEXPlus
	DESCRIPTION "SNES/Super Famicom emulator"
	HOMEPAGE_URL "https://www.explusalpha.com/"
)

printConfigInfo()
configureAppTarget(s9xp)
addPkgConfigDepMultiConfig(s9xp emuframework)
addCxxModules(s9xp emuex imagine)
evalPkgConfigFlags(s9xp all)
target_include_directories(s9xp PRIVATE
	src/snes9x
	src/apu/bapu
)
target_compile_definitions(s9xp PRIVATE HAVE_STRINGS_H HAVE_STDINT_H RIGHTSHIFT_IS_SAR ZLIB)
target_compile_options(s9xp PRIVATE -Wno-register -Wno-implicit-fallthrough)
add_subdirectory(src)