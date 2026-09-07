cmake_minimum_required(VERSION 3.20)
project(nvswitch_fabric VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  add_compile_options(/W4 /WX /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Werror)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  add_compile_options(-Wall -Wextra -Werror)
endif()

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  add_compile_options(/utf-8)
endif()

include(CTest)

option(NVSWITCH_FABRIC_BUILD_CUDA "Build the optional CUDA measurement helper" ON)
option(NVSWITCH_FABRIC_BUILD_NVML "Build the optional NVIDIA/NVML backend" ON)
option(NVSWITCH_FABRIC_BUILD_EXAMPLES "Build examples" ON)
option(NVSWITCH_FABRIC_BUILD_BENCHMARKS "Build benchmarks" ON)
option(NVSWITCH_FABRIC_BUILD_TOOLS "Build inspection CLI + runtime tools" ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(nvswitch_fabric
  src/topology.cpp
  src/path.cpp
  src/route.cpp
  src/fabric_registry.cpp
  src/wire.cpp
  src/persistence.cpp
  src/runtime/transport.cpp
  src/runtime/coordinator.cpp
  src/runtime/worker.cpp
)
target_include_directories(nvswitch_fabric PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_definitions(nvswitch_fabric PUBLIC NVSWITCH_FABRIC_LIBRARY)
if(WIN32)
  target_link_libraries(nvswitch_fabric PUBLIC ws2_32)
endif()

add_library(nvswitch_fabric_synthetic src/backends/synthetic_backend.cpp)
target_link_libraries(nvswitch_fabric_synthetic PUBLIC nvswitch_fabric)

# Resolve the CUDA toolkit root (versioned directory) via a glob.
file(GLOB _nvf_cuda_roots LIST_DIRECTORIES true "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/*")
list(REMOVE_ITEM _nvf_cuda_roots "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/extras" "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/development")
list(SORT _nvf_cuda_roots)
set(_nvf_cuda_root "")
if(_nvf_cuda_roots)
  list(GET _nvf_cuda_roots -1 _nvf_cuda_root)
endif()

if(NVSWITCH_FABRIC_BUILD_NVML)
  add_library(nvswitch_fabric_nvidia src/backends/nvidia_backend.cpp)
  target_link_libraries(nvswitch_fabric_nvidia PUBLIC nvswitch_fabric)
  find_library(NVML_LIB nvml PATHS "${_nvf_cuda_root}/lib/x64" NO_DEFAULT_PATH)
  if(NOT NVML_LIB)
    find_library(NVML_LIB nvml PATHS "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA" PATH_SUFFIXES "lib/x64")
  endif()
  if(NVML_LIB)
    target_link_libraries(nvswitch_fabric_nvidia PRIVATE "${NVML_LIB}")
    target_compile_definitions(nvswitch_fabric_nvidia PRIVATE NVSWITCH_FABRIC_HAVE_NVML=1)
    target_include_directories(nvswitch_fabric_nvidia PRIVATE "${_nvf_cuda_root}/include")
  endif()
endif()

# nvswitch_fabric_cuda always exists: when CUDA is built it additionally links the
# nvcc-compiled kernel object; otherwise runCudaProof reports UNSUPPORTED.
add_library(nvswitch_fabric_cuda STATIC src/backends/cuda_measurement.cpp)
target_link_libraries(nvswitch_fabric_cuda PUBLIC nvswitch_fabric)
if(NVSWITCH_FABRIC_BUILD_CUDA)
  find_program(CUDA_NVCC nvcc PATHS "${_nvf_cuda_root}/bin" NO_DEFAULT_PATH)
  if(NOT CUDA_NVCC)
    find_program(CUDA_NVCC nvcc HINTS ENV PATH)
  endif()
  if(CUDA_NVCC)
    find_library(CUDA_RT_LIB cudart PATHS "${_nvf_cuda_root}/lib/x64" NO_DEFAULT_PATH)
    if(NOT CUDA_RT_LIB)
      find_library(CUDA_RT_LIB cudart PATHS "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA" PATH_SUFFIXES "lib/x64")
    endif()
    set(CUDA_PFX "${CMAKE_CURRENT_BINARY_DIR}/cuda_objects")
    file(MAKE_DIRECTORY "${CUDA_PFX}")
    set(CUDA_OBJ "${CUDA_PFX}/cuda_kernel.obj")
    set(NVF_CUDA_COM "\"${CUDA_NVCC}\" -c -O2 -arch=sm_120 --compiler-options=/MD -I\"${CMAKE_CURRENT_SOURCE_DIR}/include\" -I\"${_nvf_cuda_root}/include\" -o \"${CUDA_OBJ}\" \"${CMAKE_CURRENT_SOURCE_DIR}/src/backends/cuda_kernel.cu\"")
    add_custom_command(
      OUTPUT "${CUDA_OBJ}"
      COMMAND "${CUDA_NVCC}" -c -O2 -arch=sm_120 --compiler-options=/MD -I"${CMAKE_CURRENT_SOURCE_DIR}/include" -I"${_nvf_cuda_root}/include" -o "${CUDA_OBJ}" "${CMAKE_CURRENT_SOURCE_DIR}/src/backends/cuda_kernel.cu"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/backends/cuda_kernel.cu"
      COMMENT "nvcc compiling cuda_kernel.cu")
    target_sources(nvswitch_fabric_cuda PRIVATE "${CUDA_OBJ}")
    if(CUDA_RT_LIB)
      target_link_libraries(nvswitch_fabric_cuda PRIVATE "${CUDA_RT_LIB}")
    endif()
    target_compile_definitions(nvswitch_fabric_cuda PRIVATE NVSWITCH_FABRIC_HAVE_CUDA=1)
  endif()
endif()
enable_testing()
add_subdirectory(tests)

if(NVSWITCH_FABRIC_BUILD_TOOLS)
  add_executable(nvswitch_fabric_inspect tools/nvswitch_fabric_inspect.cpp)
  target_link_libraries(nvswitch_fabric_inspect PRIVATE nvswitch_fabric nvswitch_fabric_synthetic $<$<BOOL:${NVSWITCH_FABRIC_BUILD_NVML}>:nvswitch_fabric_nvidia> nvswitch_fabric_cuda)
  add_executable(nvswitch_fabric_coordinator tools/nvswitch_fabric_coordinator.cpp)
  target_link_libraries(nvswitch_fabric_coordinator PRIVATE nvswitch_fabric nvswitch_fabric_synthetic)
  add_executable(nvswitch_fabric_worker tools/nvswitch_fabric_worker.cpp)
  target_link_libraries(nvswitch_fabric_worker PRIVATE nvswitch_fabric nvswitch_fabric_synthetic)
endif()

if(NVSWITCH_FABRIC_BUILD_EXAMPLES)
  add_executable(example_synthetic_single_switch examples/synthetic_single_switch.cpp)
  target_link_libraries(example_synthetic_single_switch PRIVATE nvswitch_fabric nvswitch_fabric_synthetic)
  add_executable(example_redundant_two_switch examples/redundant_two_switch.cpp)
  target_link_libraries(example_redundant_two_switch PRIVATE nvswitch_fabric nvswitch_fabric_synthetic)
  add_executable(example_route_governance examples/route_governance.cpp)
  target_link_libraries(example_route_governance PRIVATE nvswitch_fabric nvswitch_fabric_synthetic)
  add_executable(example_inspection examples/inspection.cpp)
  target_link_libraries(example_inspection PRIVATE nvswitch_fabric nvswitch_fabric_synthetic)
  add_executable(example_nvidia_discovery examples/nvidia_discovery.cpp)
  target_link_libraries(example_nvidia_discovery PRIVATE nvswitch_fabric nvswitch_fabric_synthetic $<$<BOOL:${NVSWITCH_FABRIC_BUILD_NVML}>:nvswitch_fabric_nvidia> nvswitch_fabric_cuda)
endif()

if(NVSWITCH_FABRIC_BUILD_BENCHMARKS)
  add_executable(nvswitch_fabric_bench benchmarks/fabric_bench.cpp)
  target_link_libraries(nvswitch_fabric_bench PRIVATE nvswitch_fabric nvswitch_fabric_synthetic)
endif()

install(TARGETS nvswitch_fabric nvswitch_fabric_synthetic EXPORT NVSwitchFabricTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
if(TARGET nvswitch_fabric_nvidia)
  install(TARGETS nvswitch_fabric_nvidia EXPORT NVSwitchFabricTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(TARGET nvswitch_fabric_cuda)
  install(TARGETS nvswitch_fabric_cuda EXPORT NVSwitchFabricTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(DIRECTORY include/nvswitch_fabric DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT NVSwitchFabricTargets FILE NVSwitchFabricTargets.cmake NAMESPACE nvswitch_fabric:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NVSwitchFabric)

configure_package_config_file(
  cmake/NVSwitchFabricConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/NVSwitchFabricConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NVSwitchFabric)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/NVSwitchFabricConfigVersion.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/NVSwitchFabricConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/NVSwitchFabricConfigVersion.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NVSwitchFabric)