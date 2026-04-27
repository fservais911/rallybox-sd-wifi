# Script to flash the dynamically selected firmware for partition OTA
# Reads the firmware path from the selection file and flashes it
#
# Required input variables:
#   - BINARY_COMPONENT_DIR: Path to the component binary directory
#   - SLAVE_FW_OFFSET: Partition offset for flashing
#   - PYTHON: Python executable path

if(NOT DEFINED COMPONENT_NAME)
    set(COMPONENT_NAME "Partition OTA")
endif()

# Read the selected firmware path and name
set(SELECTION_FILE "${BINARY_COMPONENT_DIR}/selected_firmware.txt")
set(NAME_FILE "${BINARY_COMPONENT_DIR}/selected_firmware_name.txt")

if(NOT EXISTS "${SELECTION_FILE}")
    message(FATAL_ERROR "${COMPONENT_NAME}: Selection file not found: ${SELECTION_FILE}")
endif()

file(READ "${SELECTION_FILE}" SELECTED_FIRMWARE_PATH)
file(READ "${NAME_FILE}" SELECTED_FIRMWARE_NAME)

# Remove any trailing newlines
string(STRIP "${SELECTED_FIRMWARE_PATH}" SELECTED_FIRMWARE_PATH)
string(STRIP "${SELECTED_FIRMWARE_NAME}" SELECTED_FIRMWARE_NAME)

message(STATUS "-----------------------------------------------------")
message(STATUS "${COMPONENT_NAME}: Flashing ${SELECTED_FIRMWARE_NAME}")
message(STATUS "Path: ${SELECTED_FIRMWARE_PATH}")
message(STATUS "Offset: ${SLAVE_FW_OFFSET}")
message(STATUS "-----------------------------------------------------")

set(SERIAL_PORT "")
set(SERIAL_BAUD "2000000")

if(DEFINED ENV{ESPPORT} AND NOT "$ENV{ESPPORT}" STREQUAL "")
    set(SERIAL_PORT "$ENV{ESPPORT}")
elseif(DEFINED ENV{IDF_PORT} AND NOT "$ENV{IDF_PORT}" STREQUAL "")
    set(SERIAL_PORT "$ENV{IDF_PORT}")
endif()

if(DEFINED ENV{ESPBAUD} AND NOT "$ENV{ESPBAUD}" STREQUAL "")
    set(SERIAL_BAUD "$ENV{ESPBAUD}")
elseif(DEFINED ENV{IDF_BAUD} AND NOT "$ENV{IDF_BAUD}" STREQUAL "")
    set(SERIAL_BAUD "$ENV{IDF_BAUD}")
endif()

if(NOT "${SERIAL_PORT}" STREQUAL "")
    message(STATUS "Using serial port: ${SERIAL_PORT}")
endif()
message(STATUS "Using serial baud: ${SERIAL_BAUD}")

# Check if file exists
if(NOT EXISTS "${SELECTED_FIRMWARE_PATH}")
    message(FATAL_ERROR "❌ ERROR: Selected firmware file not found: ${SELECTED_FIRMWARE_PATH}")
endif()

# Execute esptool with explicit serial settings when available
if(NOT "${SERIAL_PORT}" STREQUAL "")
    execute_process(
        COMMAND ${PYTHON} -m esptool
            -p ${SERIAL_PORT}
            -b ${SERIAL_BAUD}
            write_flash
            --force
            ${SLAVE_FW_OFFSET} "${SELECTED_FIRMWARE_PATH}"
        RESULT_VARIABLE FLASH_RESULT
        OUTPUT_VARIABLE FLASH_OUTPUT
        ERROR_VARIABLE FLASH_ERROR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )
else()
    execute_process(
        COMMAND ${PYTHON} -m esptool
            -b ${SERIAL_BAUD}
            write_flash
            --force
            ${SLAVE_FW_OFFSET} "${SELECTED_FIRMWARE_PATH}"
        RESULT_VARIABLE FLASH_RESULT
        OUTPUT_VARIABLE FLASH_OUTPUT
        ERROR_VARIABLE FLASH_ERROR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )
endif()

if(FLASH_OUTPUT)
    message(STATUS "${FLASH_OUTPUT}")
endif()

if(NOT FLASH_RESULT EQUAL 0)
    message(FATAL_ERROR "❌ ERROR: Failed to flash firmware:\n${FLASH_ERROR}")
endif()

message(STATUS "✅ Successfully flashed ${SELECTED_FIRMWARE_NAME} to partition!")
