# MCUboot third-party subset

This directory contains the MCUboot 2.4.0 `boot/bootutil`, TinyCrypt,
`mbedtls-asn1`, and `imgtool` files used by X-CAN. They were copied from the
repository reference project so this build has no absolute source dependency.

`scripts/imgtool/boot_record.py` delays its optional CBOR import until boot
record generation is requested. X-CAN does not generate boot records, so the
normal ECDSA-P256 signing path needs only the vendored pure-Python IntelHex
package plus the Python packages already required by imgtool.

The MCUboot and TinyCrypt license files are retained in this directory. The
IntelHex license is `scripts/vendor/INTELHEX_LICENSE.txt`.
