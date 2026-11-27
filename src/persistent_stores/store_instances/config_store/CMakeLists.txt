target_sources(
  firmware PRIVATE store_c_api.cpp store_definition.cpp store_instance.cpp store_journal.cpp
  )

if(NOT HAS_CONFIG_STORE_WO_BACKEND)
  target_sources(firmware PRIVATE filament_variant_decode.cpp migrations.cpp)

endif()

if(PRINTER STREQUAL "XL")
  target_sources(firmware PRIVATE config_store_export.cpp config_store_export_format.cpp)
  target_sources(firmware PRIVATE config_store_backup_items.cpp config_store_import_format.cpp)
endif()
