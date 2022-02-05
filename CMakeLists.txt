# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

# (Not part of the boilerplate)
# This example uses an extra component for common functions such as Wi-Fi and Ethernet connection.
set(EXTRA_COMPONENT_DIRS ./components/esp32-wifi-manager)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hourglassclock)

message("${BUILD_DIR}/${PROJECT_NAME}")

#invoke buildnummer.cmake on each build.
#buildnumber.cmake will increment the build number in version.txt
add_custom_target(buildnumber ALL
     COMMAND ${CMAKE_COMMAND} -D BUILD_DIR="${BUILD_DIR}" -D PROJECT_NAME=${PROJECT_NAME} -D PROJECT_DIR=${CMAKE_SOURCE_DIR} -P "${CMAKE_SOURCE_DIR}/buildnumber.cmake"
)
message("${BUILD_DIR}/${PROJECT_NAME}")

