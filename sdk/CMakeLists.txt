# TrinityCore module SDK headers and CMake package.

set(TRINITY_SDK_VERSION_MAJOR 1)
set(TRINITY_SDK_VERSION_MINOR 0)

file(GLOB_RECURSE TRINITY_SDK_PUBLIC_HEADERS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/include/trinity/*.h")
set(TRINITY_SDK_HEADER_DIGEST_INPUT "")
foreach(header IN LISTS TRINITY_SDK_PUBLIC_HEADERS)
  file(SHA256 "${header}" header_hash)
  string(APPEND TRINITY_SDK_HEADER_DIGEST_INPUT "${header_hash};")
endforeach()
string(SHA256 TRINITY_SDK_HEADER_HASH "${TRINITY_SDK_HEADER_DIGEST_INPUT}")

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  set(TRINITY_SDK_COMPILER "MSVC-${CMAKE_CXX_COMPILER_VERSION}")
  if(CMAKE_MSVC_RUNTIME_LIBRARY)
    set(TRINITY_SDK_CRT "${CMAKE_MSVC_RUNTIME_LIBRARY}")
  elseif(WITH_COREDEBUG)
    set(TRINITY_SDK_CRT "Debug")
  else()
    set(TRINITY_SDK_CRT "Release")
  endif()
elseif(CMAKE_CXX_COMPILER_ID)
  set(TRINITY_SDK_COMPILER "${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}")
  set(TRINITY_SDK_CRT "default")
else()
  set(TRINITY_SDK_COMPILER "unknown")
  set(TRINITY_SDK_CRT "unknown")
endif()
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(TRINITY_SDK_ARCH "x64")
else()
  set(TRINITY_SDK_ARCH "x86")
endif()

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/TrinitySDKBuild.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/trinity/TrinitySDKBuild.h"
  @ONLY)

set(TRINITY_SDK_SHARED_CORE ${BUILD_SHARED_LIBS})

add_library(TrinitySDK INTERFACE)
add_library(Trinity::SDK ALIAS TrinitySDK)
set_target_properties(TrinitySDK PROPERTIES EXPORT_NAME SDK)
target_include_directories(TrinitySDK INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/trinity>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated/trinity>"
  "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>"
  "$<INSTALL_INTERFACE:include>"
  "$<INSTALL_INTERFACE:include/trinity>"
  "$<INSTALL_INTERFACE:include/trinity/core>")

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/" DESTINATION include)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/trinity/TrinitySDKBuild.h" DESTINATION include/trinity)
install(FILES "${CMAKE_BINARY_DIR}/revision_data.h" DESTINATION include/trinity/core)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/src/server/game/Modules/" DESTINATION include/trinity/core/Modules FILES_MATCHING PATTERN "*.h")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/src/server/game/Scripting/" DESTINATION include/trinity/core/Scripting FILES_MATCHING PATTERN "*.h")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/src/common/" DESTINATION include/trinity/core/common FILES_MATCHING PATTERN "*.h")

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/TrinitySDKConfigVersion.cmake"
  VERSION "${TRINITY_SDK_VERSION_MAJOR}.${TRINITY_SDK_VERSION_MINOR}.0"
  COMPATIBILITY SameMajorVersion)
configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/TrinitySDKConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/TrinitySDKConfig.cmake"
  INSTALL_DESTINATION lib/cmake/TrinitySDK)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/TrinitySDKConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/TrinitySDKConfigVersion.cmake"
  DESTINATION lib/cmake/TrinitySDK)
install(FILES "${CMAKE_SOURCE_DIR}/cmake/macros/ConfigureModules.cmake"
  DESTINATION lib/cmake/TrinitySDK)
install(TARGETS TrinitySDK EXPORT TrinitySDKTargets)
install(EXPORT TrinitySDKTargets
  FILE TrinitySDKTargets.cmake
  NAMESPACE Trinity::
  DESTINATION lib/cmake/TrinitySDK)
