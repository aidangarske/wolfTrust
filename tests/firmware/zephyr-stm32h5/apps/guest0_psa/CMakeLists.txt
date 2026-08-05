cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wolftrust_guest0_psa)

if(NOT DEFINED WOLFTRUST_CMSE_IMPLIB)
  message(FATAL_ERROR "WOLFTRUST_CMSE_IMPLIB must point at build/secure_cmse_implib.o")
endif()

zephyr_include_directories(
  ${ZEPHYR_BASE}/modules/cmsis_6
)

set(WT_REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../../..)
set(WT_WOLFCOSE_DIR ${WT_REPO_ROOT}/lib/wolfCOSE)

target_sources(app PRIVATE
  src/main.c
  src/attestation_verify.c
  ${WOLFTRUST_CMSE_IMPLIB}
  ${WT_WOLFCOSE_DIR}/src/wolfcose.c
  ${WT_WOLFCOSE_DIR}/src/wolfcose_cbor.c
)
target_include_directories(app PRIVATE ${WT_WOLFCOSE_DIR}/include)
target_compile_definitions(app PRIVATE
  WOLFCOSE_LEAN
  WOLFCOSE_NO_SIGN1_SIGN
  WOLFCOSE_NO_ENCRYPT0
  WOLFCOSE_NO_MAC0
  WOLFCOSE_NO_KEY_ENCODE
  WOLFCOSE_NO_KEY_DECODE
)
if(DEFINED WT_EXPECTED_MEASUREMENT_HEX)
    target_compile_definitions(app PRIVATE
        WT_EXPECTED_MEASUREMENT_HEX="${WT_EXPECTED_MEASUREMENT_HEX}")
endif()

if(NOT DEFINED WT_EXPECTED_LIFECYCLE)
    set(WT_EXPECTED_LIFECYCLE 0x3000u)
endif()
target_compile_definitions(app PRIVATE
    WT_EXPECTED_LIFECYCLE=${WT_EXPECTED_LIFECYCLE})

if(WT_ATTESTATION_DEVELOPMENT_PROFILE)
    target_compile_definitions(app PRIVATE
        WT_ATTESTATION_DEVELOPMENT_PROFILE=1)
endif()

if(WT_M33MU_EXPECT_BKPT)
    target_compile_definitions(app PRIVATE WT_M33MU_EXPECT_BKPT=1)
endif()

set_source_files_properties(
  ${WT_WOLFCOSE_DIR}/src/wolfcose.c
  ${WT_WOLFCOSE_DIR}/src/wolfcose_cbor.c
  PROPERTIES COMPILE_OPTIONS
    "-Wno-unused-function;-Wno-unused-variable;-Wno-unused-parameter;-Wno-type-limits"
)
