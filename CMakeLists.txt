cmake_minimum_required(VERSION 3.20)
project(ojay VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)  # __int128
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Using LLVM ${LLVM_PACKAGE_VERSION} from ${LLVM_DIR}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---------------------------------------------------------------- compiler

# The Preload (Context, Allocator, ...) is Jai source compiled into the compiler.
file(READ ${CMAKE_SOURCE_DIR}/src/preload.jai OJAY_PRELOAD)
configure_file(src/preload.cpp.in ${CMAKE_BINARY_DIR}/generated/preload.cpp @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/preload.jai)
# So are the parts of the built-in modules Basic and Math that are written in Jai.
file(READ ${CMAKE_SOURCE_DIR}/src/modules/Basic.jai OJAY_BASIC)
file(READ ${CMAKE_SOURCE_DIR}/src/modules/Math.jai OJAY_MATH)
file(READ ${CMAKE_SOURCE_DIR}/src/core.jai OJAY_CORE)
file(READ ${CMAKE_SOURCE_DIR}/src/modules/Hash_Table.jai OJAY_HASH_TABLE)
file(READ ${CMAKE_SOURCE_DIR}/src/modules/String.jai OJAY_STRING)
file(READ ${CMAKE_SOURCE_DIR}/src/modules/Random.jai OJAY_RANDOM)
file(READ ${CMAKE_SOURCE_DIR}/src/modules/File.jai OJAY_FILE)
configure_file(src/modules.cpp.in ${CMAKE_BINARY_DIR}/generated/modules.cpp @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/src/modules/Basic.jai ${CMAKE_SOURCE_DIR}/src/modules/Math.jai
  ${CMAKE_SOURCE_DIR}/src/modules/Hash_Table.jai ${CMAKE_SOURCE_DIR}/src/modules/String.jai
  ${CMAKE_SOURCE_DIR}/src/modules/Random.jai ${CMAKE_SOURCE_DIR}/src/modules/File.jai
  ${CMAKE_SOURCE_DIR}/src/core.jai)

add_library(ojay_core STATIC
  ${CMAKE_BINARY_DIR}/generated/preload.cpp
  ${CMAKE_BINARY_DIR}/generated/modules.cpp
  src/lexer.cpp
  src/parser.cpp
  src/ast_clone.cpp
  src/types.cpp
  src/sema.cpp
  src/interp.cpp
  src/codegen.cpp
  src/abi.cpp
  src/driver.cpp
)
target_include_directories(ojay_core PUBLIC src)
target_include_directories(ojay_core SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS})
separate_arguments(OJAY_LLVM_DEFS NATIVE_COMMAND ${LLVM_DEFINITIONS})
target_compile_definitions(ojay_core PUBLIC ${OJAY_LLVM_DEFS} OJAY_VERSION="${PROJECT_VERSION}")
target_compile_options(ojay_core PRIVATE -Wall -Wextra -Wno-unused-parameter)

if(LLVM_LINK_LLVM_DYLIB OR TARGET LLVM)
  target_link_libraries(ojay_core PUBLIC LLVM)
else()
  llvm_map_components_to_libnames(OJAY_LLVM_LIBS
    core support passes target targetparser mc irreader
    X86CodeGen X86AsmParser X86Desc X86Info
    ARMCodeGen ARMAsmParser ARMDesc ARMInfo
    RISCVCodeGen RISCVAsmParser RISCVDesc RISCVInfo)
  target_link_libraries(ojay_core PUBLIC ${OJAY_LLVM_LIBS})
endif()

find_package(Threads REQUIRED)
target_link_libraries(ojay_core PUBLIC Threads::Threads)  # #run executes on a thread with a large stack

add_executable(ojay-x86_64 src/main_x86_64.cpp)
target_link_libraries(ojay-x86_64 PRIVATE ojay_core)

add_executable(ojay-arm32 src/main_arm32.cpp)
target_link_libraries(ojay-arm32 PRIVATE ojay_core)

add_executable(ojay-riscv src/main_riscv.cpp)
target_link_libraries(ojay-riscv PRIVATE ojay_core)

# ---------------------------------------------------------------- runtime
#
# The runtime is plain C with no libc headers, cross-compiled with clang for
# each (architecture, mode) pair.

find_program(OJAY_CLANG NAMES clang REQUIRED)
find_program(OJAY_AR NAMES llvm-ar ar REQUIRED)

set(OJAY_RUNTIME_SRC ${CMAKE_SOURCE_DIR}/runtime/ojay_runtime.c)
set(OJAY_RUNTIME_DIR ${CMAKE_BINARY_DIR}/lib/ojay)
set(OJAY_RUNTIME_COMMON_FLAGS
  -std=c11 -O2 -ffreestanding -fno-stack-protector -fno-exceptions
  -fno-unwind-tables -fno-asynchronous-unwind-tables -Wall -Wextra)
set(OJAY_RUNTIME_LIBS)

function(ojay_add_runtime arch mode triple)
  set(obj ${CMAKE_BINARY_DIR}/runtime/${arch}_${mode}.o)
  set(lib ${OJAY_RUNTIME_DIR}/libojayrt_${arch}_${mode}.a)
  add_custom_command(
    OUTPUT ${lib}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/runtime ${OJAY_RUNTIME_DIR}
    COMMAND ${OJAY_CLANG} --target=${triple} ${OJAY_RUNTIME_COMMON_FLAGS} ${ARGN} -c ${OJAY_RUNTIME_SRC} -o ${obj}
    COMMAND ${CMAKE_COMMAND} -E rm -f ${lib}
    COMMAND ${OJAY_AR} rcs ${lib} ${obj}
    DEPENDS ${OJAY_RUNTIME_SRC}
    COMMENT "Building runtime ${arch} (${mode})"
    VERBATIM)
  set(OJAY_RUNTIME_LIBS ${OJAY_RUNTIME_LIBS} ${lib} PARENT_SCOPE)
endfunction()

ojay_add_runtime(x86_64 hosted x86_64-pc-linux-gnu -fPIC -DOJAY_PLATFORM_LINUX_HOSTED)
ojay_add_runtime(x86_64 freestanding x86_64-pc-linux-gnu -fno-pic -DOJAY_PLATFORM_LINUX_FREESTANDING)
ojay_add_runtime(arm32 baremetal thumbv8m.main-none-eabihf
  -mcpu=cortex-m33 -mfloat-abi=hard -mfpu=fpv5-sp-d16 -fshort-enums -ffunction-sections -fdata-sections
  -DOJAY_PLATFORM_CORTEXM)
ojay_add_runtime(riscv32 esp32c6 riscv32-unknown-none-elf
  -march=rv32imac_zicsr_zifencei -mabi=ilp32 -ffunction-sections -fdata-sections
  -DOJAY_PLATFORM_ESP32C6)

# Board linker scripts for bare-metal targets.
file(GLOB OJAY_BOARD_SCRIPTS ${CMAKE_SOURCE_DIR}/runtime/boards/*.ld)
set(OJAY_BOARD_OUTPUTS)
foreach(script ${OJAY_BOARD_SCRIPTS})
  get_filename_component(name ${script} NAME)
  set(out ${OJAY_RUNTIME_DIR}/boards/${name})
  add_custom_command(OUTPUT ${out}
    COMMAND ${CMAKE_COMMAND} -E copy ${script} ${out}
    DEPENDS ${script} VERBATIM)
  list(APPEND OJAY_BOARD_OUTPUTS ${out})
endforeach()

add_custom_target(ojay_runtime ALL DEPENDS ${OJAY_RUNTIME_LIBS} ${OJAY_BOARD_OUTPUTS})

# ---------------------------------------------------------------- tests

enable_testing()
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME ojay_tests
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/run_tests.py --build-dir ${CMAKE_BINARY_DIR})
endif()

# ---------------------------------------------------------------- install

install(TARGETS ojay-x86_64 ojay-arm32 ojay-riscv RUNTIME DESTINATION bin)
install(FILES ${OJAY_RUNTIME_LIBS} DESTINATION lib/ojay)
install(FILES ${OJAY_BOARD_SCRIPTS} DESTINATION lib/ojay/boards)
