set(_key_root "${CMAKE_CURRENT_LIST_DIR}/..")

# Object files keep the WPAD bridge in the existing graphics runtime link unit.
# The bridge calls graphics event polling; graphics invokes the settings overlay.
add_library(key_controller OBJECT input.cpp controls.cpp)
target_include_directories(key_controller PUBLIC "${_key_root}" "${_key_root}/runtime/include"
  PRIVATE "${_key_root}/renderer" "${_key_root}/references/WiiCompiled/runtime/include")
target_link_libraries(key_controller PUBLIC key_memory aurora::core aurora::wpad)

add_executable(input_smoke "${_key_root}/tests/input_smoke.cpp")
target_include_directories(input_smoke PRIVATE "${_key_root}" "${_key_root}/references/WiiCompiled/runtime/include")
target_link_libraries(input_smoke PRIVATE key_graphics)

add_executable(motion_tests "${_key_root}/tests/motion_tests.cpp")
target_include_directories(motion_tests PRIVATE "${_key_root}")
if(MINGW)
  target_link_options(motion_tests PRIVATE -static)
endif()
add_test(NAME controller_motion COMMAND motion_tests)
