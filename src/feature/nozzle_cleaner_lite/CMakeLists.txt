target_include_directories(firmware PRIVATE . include)

if(HAS_NOZZLE_CLEANER_LITE)
  target_sources(firmware PRIVATE cleaner_zigzag.cpp nozzle_cleaner_lite.cpp)
  if(HAS_CUSTOM_NOZZLE_WIPER)
    target_sources(firmware PRIVATE custom_wiper_geometry.cpp)
  endif()
endif()
