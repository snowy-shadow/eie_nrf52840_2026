zephyr_library()

if(CONFIG_GPIO)
  if(CONFIG_BOARD_NATIVE_SIM)
    zephyr_library_sources(btn_sim.c)
  else()
    zephyr_library_sources(btn.c)
  endif()
endif()
