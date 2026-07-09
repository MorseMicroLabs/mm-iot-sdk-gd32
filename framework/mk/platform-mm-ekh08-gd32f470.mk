#
# Copyright 2026 Morse Micro
#
# SPDX-License-Identifier: Apache-2.0
#
# SUPERSEDED: this platform was renamed from "mm-ekh08-gd32f470" to "mm-ekh18-gd32f470" once its
# chip target was corrected from MM6108/MMECH08 to MM8108/MMECH18 (the directory name was a
# leftover from before that correction). This file forwards to the new platform makefile so any
# existing build configuration referencing PLATFORM=mm-ekh08-gd32f470 keeps working. Please switch
# to PLATFORM=mm-ekh18-gd32f470 (see examples/porting_assistant/targets/mm-ekh18-gd32f470/) going
# forward -- see framework/src/platforms/mm-ekh18-gd32f470/readme.md for details.
#
# (This file could not be deleted or renamed -- this workspace only allows content-overwriting --
# so it is kept in place purely as a forwarding stub.)

include $(MMIOT_ROOT)/mk/platform-mm-ekh18-gd32f470.mk
