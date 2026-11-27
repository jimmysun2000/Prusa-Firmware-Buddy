target_sources(firmware PRIVATE M141_M191.cpp)

if(XL_ENCLOSURE_SUPPORT)
  target_sources(firmware PRIVATE chamber_soak.cpp)
endif()
