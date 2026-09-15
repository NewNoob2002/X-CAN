set(FREERTOS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/FreeRTOS-Kernel)
add_library(freertos_kernel STATIC
    ${FREERTOS_ROOT}/tasks.c
    ${FREERTOS_ROOT}/queue.c
    ${FREERTOS_ROOT}/list.c
    ${FREERTOS_ROOT}/portable/GCC/ARM_CM4F/port.c
)
target_include_directories(freertos_kernel
    PUBLIC
        ${FREERTOS_ROOT}/include
        ${FREERTOS_ROOT}/portable/GCC/ARM_CM4F
    PRIVATE
        ${PROJECT_SOURCE_DIR}/Core/Inc
)
target_link_libraries(freertos_kernel PUBLIC stm32cubemx)

set(CHERRYUSB_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/CherryUSB)
add_library(cherryusb_device STATIC
    ${CHERRYUSB_ROOT}/core/usbd_core.c
    ${CHERRYUSB_ROOT}/port/fsdev/usb_dc_fsdev.c
)
target_include_directories(cherryusb_device
    PUBLIC
        ${CHERRYUSB_ROOT}/common
        ${CHERRYUSB_ROOT}/core
    PRIVATE
        ${CHERRYUSB_ROOT}/port/fsdev
        ${PROJECT_SOURCE_DIR}/firmware/src
)
target_compile_options(cherryusb_device PRIVATE -Wno-unused-parameter)
target_link_libraries(cherryusb_device PUBLIC stm32cubemx)

set(MCUBOOT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/MCUboot)
set(BOOTUTIL_ROOT ${MCUBOOT_ROOT}/boot/bootutil)
set(TINYCRYPT_ROOT ${MCUBOOT_ROOT}/ext/tinycrypt/lib)
set(MCUBOOT_ASN1_ROOT ${MCUBOOT_ROOT}/ext/mbedtls-asn1)

add_library(tinycrypt STATIC
    ${TINYCRYPT_ROOT}/source/sha256.c
    ${TINYCRYPT_ROOT}/source/utils.c
    ${TINYCRYPT_ROOT}/source/ecc.c
    ${TINYCRYPT_ROOT}/source/ecc_dsa.c
    ${TINYCRYPT_ROOT}/source/ecc_platform_specific.c
)
target_include_directories(tinycrypt PUBLIC ${TINYCRYPT_ROOT}/include)
target_link_libraries(tinycrypt PRIVATE stm32cubemx)

add_library(mcuboot_asn1 STATIC
    ${MCUBOOT_ASN1_ROOT}/src/asn1parse.c
    ${MCUBOOT_ASN1_ROOT}/src/platform_util.c
)
target_include_directories(mcuboot_asn1 PUBLIC ${MCUBOOT_ASN1_ROOT}/include)
target_link_libraries(mcuboot_asn1 PRIVATE stm32cubemx)

add_library(mcuboot_bootutil STATIC
    ${BOOTUTIL_ROOT}/src/bootutil_area.c
    ${BOOTUTIL_ROOT}/src/bootutil_find_key.c
    ${BOOTUTIL_ROOT}/src/bootutil_img_hash.c
    ${BOOTUTIL_ROOT}/src/bootutil_img_security_cnt.c
    ${BOOTUTIL_ROOT}/src/bootutil_loader.c
    ${BOOTUTIL_ROOT}/src/bootutil_misc.c
    ${BOOTUTIL_ROOT}/src/bootutil_public.c
    ${BOOTUTIL_ROOT}/src/caps.c
    ${BOOTUTIL_ROOT}/src/fault_injection_hardening.c
    ${BOOTUTIL_ROOT}/src/image_ecdsa.c
    ${BOOTUTIL_ROOT}/src/image_validate.c
    ${BOOTUTIL_ROOT}/src/loader.c
    ${BOOTUTIL_ROOT}/src/swap_misc.c
    ${BOOTUTIL_ROOT}/src/swap_offset.c
    ${BOOTUTIL_ROOT}/src/tlv.c
)
target_include_directories(mcuboot_bootutil
    PUBLIC ${BOOTUTIL_ROOT}/include
    PRIVATE ${BOOTUTIL_ROOT}/src ${PROJECT_SOURCE_DIR}/firmware/boot/include
)
target_link_libraries(mcuboot_bootutil PRIVATE tinycrypt mcuboot_asn1 stm32cubemx)
target_compile_options(mcuboot_bootutil PRIVATE $<$<CONFIG:Release>:-UNDEBUG>)
