target_sources(firmware PRIVATE chamber.cpp)

if(XL_ENCLOSURE_SUPPORT)
  target_sources(firmware PRIVATE chamber_requirements.cpp)
endif()
