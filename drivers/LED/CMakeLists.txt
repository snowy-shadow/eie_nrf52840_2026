zephyr_library()

if(CONFIG_GPIO)
  if(CONFIG_BOARD_NATIVE_SIM)
    zephyr_library_sources(led_sim.c)
  else()
    zephyr_library_sources(led.c)
  endif()
endif()
