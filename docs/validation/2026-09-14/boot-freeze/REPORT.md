# X-CAN Boot 0.1.0 freeze

The frozen Boot uses MCUboot 2.4.0, ECDSA-P256, primary validation and
swap-using-offset with test/revert. It contains no USB, FreeRTOS, CherryUSB or
FDCAN code.

The signing private key remains outside the repository at
`/home/gtc/Desktop/workspace/MyKey/XCAN/xcan-signing-ecdsa-p256.pem`. The Boot
contains the matching 91-byte DER public key. Its SHA-256 fingerprint is
`9b044372d62bc7bede56344763548b674b776fec6dd0b12e26021f49cf0b14dc`.

`xcan_boot.bin` uses 14,512 bytes of the 24 KiB Boot partition and 3,424 bytes
of RAM. Release and Debug builds, image verification, wrong-key rejection,
payload-corruption rejection, vector checks and partition checks passed.

The files in this directory are the frozen artifact set. The private key is
not copied here. Hardware swap, reset injection and revert testing remain the
release gate before production deployment.
