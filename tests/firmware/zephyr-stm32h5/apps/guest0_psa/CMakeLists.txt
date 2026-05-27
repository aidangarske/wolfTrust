cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wolftrust_guest0_psa)

if(NOT DEFINED WOLFTRUST_CMSE_IMPLIB)
  message(FATAL_ERROR "WOLFTRUST_CMSE_IMPLIB must point at build/secure_cmse_implib.o")
endif()

zephyr_include_directories(
  ${ZEPHYR_BASE}/modules/cmsis_6
)

target_sources(app PRIVATE src/main.c ${WOLFTRUST_CMSE_IMPLIB})
