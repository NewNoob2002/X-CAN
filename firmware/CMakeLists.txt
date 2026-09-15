add_library(xcan_mcuboot_port STATIC boot/flash_map_backend.c)
target_include_directories(xcan_mcuboot_port PUBLIC boot/include)
target_link_libraries(xcan_mcuboot_port PUBLIC stm32cubemx)

add_library(xcan_app STATIC
    src/app.c
    src/fw_update.c
    src/protocol.c
    src/protocol_v2.c
    src/usb.c
    ${PROJECT_SOURCE_DIR}/Core/Src/early_reset.S
    ${PROJECT_SOURCE_DIR}/Core/Src/xcan_board.c
)
target_include_directories(xcan_app PUBLIC src PRIVATE boot/include)
set(XCAN_CONFIRM_DELAY_MS 1000 CACHE STRING "MCUboot test-image health confirmation delay")
target_compile_definitions(xcan_app PRIVATE XCAN_CONFIRM_DELAY_MS=${XCAN_CONFIRM_DELAY_MS})
target_link_libraries(xcan_app
    PUBLIC stm32cubemx
    PRIVATE cherryusb_device freertos_kernel mcuboot_bootutil tinycrypt xcan_mcuboot_port
)

set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME xcan)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    USER_VECT_TAB_ADDRESS
    VECT_TAB_OFFSET=0x00006200U
)
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    -T${PROJECT_SOURCE_DIR}/STM32G431XX_FLASH.ld
    -Wl,--defsym=__flash_origin=0x08006200
    -Wl,--defsym=__flash_length=0x0000BE00
    -Wl,-Map=${CMAKE_BINARY_DIR}/xcan.map
)
set_property(TARGET ${CMAKE_PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS
    ${PROJECT_SOURCE_DIR}/STM32G431XX_FLASH.ld
)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(XCAN_IMAGE_VERSION "0.1.0+0" CACHE STRING "MCUboot image version")
set(XCAN_SIGNING_KEY "$ENV{XCAN_SIGNING_KEY}" CACHE FILEPATH
    "ECDSA-P256 image signing key")
set(XCAN_SIGNING_KEY_USAGE "development" CACHE STRING
    "Signing key usage recorded in firmware metadata")
if(NOT XCAN_SIGNING_KEY)
    message(FATAL_ERROR "Set XCAN_SIGNING_KEY to an ECDSA-P256 private key outside the repository")
endif()
if(NOT EXISTS "${XCAN_SIGNING_KEY}")
    message(FATAL_ERROR "XCAN_SIGNING_KEY does not exist: ${XCAN_SIGNING_KEY}")
endif()

set(MCUBOOT_SCRIPTS ${PROJECT_SOURCE_DIR}/components/MCUboot/scripts)
set(MCUBOOT_PYTHONPATH ${MCUBOOT_SCRIPTS}:${MCUBOOT_SCRIPTS}/vendor)
set(MCUBOOT_PUBLIC_KEY_C ${CMAKE_BINARY_DIR}/generated/mcuboot_public_key.c)
add_custom_command(
    OUTPUT ${MCUBOOT_PUBLIC_KEY_C}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/generated
    COMMAND ${CMAKE_COMMAND} -E env PYTHONPATH=${MCUBOOT_PYTHONPATH}
        ${Python3_EXECUTABLE} ${MCUBOOT_SCRIPTS}/imgtool.py getpub
        --key ${XCAN_SIGNING_KEY} --encoding lang-c --output ${MCUBOOT_PUBLIC_KEY_C}
    DEPENDS ${XCAN_SIGNING_KEY} ${MCUBOOT_SCRIPTS}/imgtool.py
    VERBATIM
)

add_executable(xcan_boot
    boot/main.c
    boot/keys.c
    ${MCUBOOT_PUBLIC_KEY_C}
    ${PROJECT_SOURCE_DIR}/startup_stm32g431xx.s
    ${PROJECT_SOURCE_DIR}/Core/Src/early_reset.S
    ${PROJECT_SOURCE_DIR}/Core/Src/system_stm32g4xx.c
    ${PROJECT_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash.c
    ${PROJECT_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash_ex.c
)
set_target_properties(xcan_boot PROPERTIES
    SUFFIX ".elf"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
target_include_directories(xcan_boot PRIVATE boot/include)
target_link_libraries(xcan_boot PRIVATE mcuboot_bootutil xcan_mcuboot_port
    stm32cubemx m)
target_link_options(xcan_boot PRIVATE
    -T${PROJECT_SOURCE_DIR}/STM32G431XX_FLASH.ld
    -Wl,--defsym=__flash_origin=0x08000000
    -Wl,--defsym=__flash_length=0x00006000
    -Wl,-Map=${CMAKE_BINARY_DIR}/xcan_boot.map
)
set_property(TARGET xcan_boot APPEND PROPERTY LINK_DEPENDS
    ${PROJECT_SOURCE_DIR}/STM32G431XX_FLASH.ld
)

add_custom_target(xcan_artifacts ALL
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_BINARY_DIR}/xcan.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_BINARY_DIR}/xcan.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:xcan_boot> ${CMAKE_BINARY_DIR}/xcan_boot.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:xcan_boot> ${CMAKE_BINARY_DIR}/xcan_boot.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:xcan_boot>
    COMMAND ${CMAKE_COMMAND} -E env PYTHONPATH=${MCUBOOT_PYTHONPATH}
        ${Python3_EXECUTABLE} ${MCUBOOT_SCRIPTS}/imgtool.py sign
        --key ${XCAN_SIGNING_KEY} --align 8 --max-align 8
        --header-size 0x200 --slot-size 0xD000 --max-sectors 26
        --version ${XCAN_IMAGE_VERSION} --pad-header
        ${CMAKE_BINARY_DIR}/xcan.bin ${CMAKE_BINARY_DIR}/xcan.signed.bin
    COMMAND ${CMAKE_COMMAND} -E env PYTHONPATH=${MCUBOOT_PYTHONPATH}
        ${Python3_EXECUTABLE} ${MCUBOOT_SCRIPTS}/imgtool.py verify
        --key ${XCAN_SIGNING_KEY} ${CMAKE_BINARY_DIR}/xcan.signed.bin
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/package_firmware.py
        --boot ${CMAKE_BINARY_DIR}/xcan_boot.bin
        --app ${CMAKE_BINARY_DIR}/xcan.bin
        --signed ${CMAKE_BINARY_DIR}/xcan.signed.bin
        --factory ${CMAKE_BINARY_DIR}/xcan_factory.bin
        --metadata ${CMAKE_BINARY_DIR}/firmware-metadata.json
        --version ${XCAN_IMAGE_VERSION}
        --key-usage ${XCAN_SIGNING_KEY_USAGE}
    DEPENDS ${CMAKE_PROJECT_NAME} xcan_boot
    BYPRODUCTS
        ${CMAKE_BINARY_DIR}/xcan.bin
        ${CMAKE_BINARY_DIR}/xcan.hex
        ${CMAKE_BINARY_DIR}/xcan_boot.bin
        ${CMAKE_BINARY_DIR}/xcan_boot.hex
        ${CMAKE_BINARY_DIR}/xcan.signed.bin
        ${CMAKE_BINARY_DIR}/xcan_factory.bin
        ${CMAKE_BINARY_DIR}/firmware-metadata.json
    VERBATIM
)
