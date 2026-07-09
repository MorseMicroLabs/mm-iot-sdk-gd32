mm-ekh08-gd32f470 Readme (SUPERSEDED) {#MM_EKH08_GD32F470_README}
====

__Copyright 2026 Morse Micro__

# This platform has been renamed

This directory ("mm-ekh08-gd32f470") is **superseded**. It was originally built targeting the
MM6108/MMECH08 chip, which turned out to be a mistake -- the real hardware pairs the GD32F470VGT6
with an MM8108-based MMECH18 module. Once that was corrected, this platform was renamed to
**`mm-ekh18-gd32f470`** to match.

Please use `../mm-ekh18-gd32f470/` going forward:

- Platform readme: `../mm-ekh18-gd32f470/readme.md`
- Platform makefile: `framework/mk/platform-mm-ekh18-gd32f470.mk`
- Example target: `examples/porting_assistant/targets/mm-ekh18-gd32f470/`

This old directory's source files are left in place, unmodified content-wise from the last MM8108
correction, for reference/history only -- they are **not** kept in sync with the canonical
`mm-ekh18-gd32f470` tree going forward (e.g. the new SPI/GPIO teardown-on-disable enhancement in
`mmhal_wlan_deinit()` was only added to the new tree). Both `framework/mk/platform-mm-ekh08-gd32f470.mk`
and `examples/porting_assistant/targets/mm-ekh08-gd32f470/Makefile` have been updated to forward to
the new platform, so existing build configurations referencing "mm-ekh08-gd32f470" will
transparently build the new, canonical `mm-ekh18-gd32f470` platform instead.

(This directory could not be deleted or renamed -- this workspace only allows content-overwriting
of files, not deletion/renaming -- so it is kept in place purely as a superseded pointer.)
