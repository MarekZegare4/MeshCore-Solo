# Stands in for esp32-arduino-lib-builder's arduino_tinyusb during the
# custom_sdkconfig rebuild: its options (Kconfig), and the framework's own
# prebuilt library and headers. No sources, so no archive replaces the
# prebuilt one.
if(CMAKE_BUILD_EARLY_EXPANSION)   # the requirements scan: nothing to resolve yet
  idf_component_register()
  return()
endif()
foreach(dir ${EXTRA_COMPONENT_DIRS})
  if(dir MATCHES "framework-arduinoespressif32$")
    set(libs "${dir}-libs/${IDF_TARGET}")
  endif()
endforeach()
if(NOT libs)
  message(FATAL_ERROR "arduino_tinyusb: the Arduino framework isn't among EXTRA_COMPONENT_DIRS")
endif()
idf_component_register(INCLUDE_DIRS "${libs}/include/arduino_tinyusb/tinyusb/src"
                                    "${libs}/include/arduino_tinyusb/include")
target_link_libraries(${COMPONENT_LIB} INTERFACE "${libs}/lib/libarduino_tinyusb.a")
