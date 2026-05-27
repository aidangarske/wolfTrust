if(CONFIG_WOLFTRUST_ZEPHYR)
  zephyr_library()
  zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR}/include)
  zephyr_library_sources(src/wolftrust_client.c)
  zephyr_library_sources_ifdef(CONFIG_WOLFTRUST_TEE_DRIVER
    src/wolftrust_tee_driver.c)
endif()
