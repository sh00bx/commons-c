find_path(LINUX_INPUT_HEADER_DIR linux/input.h)

if (NOT LINUX_INPUT_HEADER_DIR)
    return()
endif ()

pkg_check_modules(SDL2 REQUIRED sdl2)

add_library(commons-gamepad-touchpad gamepad_touchpad.c)
target_include_directories(commons-gamepad-touchpad PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(commons-gamepad-touchpad SYSTEM PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(commons-gamepad-touchpad PUBLIC commons-logging ${SDL2_LIBRARIES})

if (NOT CMAKE_TESTING_ENABLED)
    return()
endif ()

# The test #includes gamepad_touchpad.c to reach the static sysfs helpers, which
# is where the platform assumptions live. Linking against commons-gamepad-touchpad
# would duplicate the symbols, so it only links the logging / SDL deps.
add_executable(tests_gamepad_touchpad gamepad_touchpad_tests.c)
target_include_directories(tests_gamepad_touchpad SYSTEM PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(tests_gamepad_touchpad PRIVATE commons-logging ${SDL2_LIBRARIES})
add_test(tests_gamepad_touchpad tests_gamepad_touchpad)
# 77 is the Automake skip convention, used by tests that need a real device.
# Not 127, which a failed exec also returns - a broken test binary must not
# read as "skipped".
set_tests_properties(tests_gamepad_touchpad PROPERTIES SKIP_RETURN_CODE 77)
