mm-ekh18-gd32f527 Bring-up Quickstart {#MM_EKH18_GD32F527_QUICKSTART}
====

__Copyright 2026 Morse Micro__

Getting a GD32F527ZMT7 board from "out of the box" to running the `ap_mode` example, using the UART
ISP flasher rather than a debug probe.

Read `readme.md` in this directory for *why* things are the way they are. This document is just the
sequence of steps.

> **The single most common failure is the config store.** Flashing firmware **erases** it, so
> firmware and config store must be programmed in that order, every time. If you skip it or do it
> backwards, the radio will not boot and you will get "Unable to find BCF_FILE entry in config
> store". See [Step 6](#step-6-program-the-config-store).

# 0. What you need

**Hardware**

- GD32F527ZMT7 core board with an MM8108 (MMECH18) module wired to it — pinout in `readme.md`.
- The board's own **USB-serial connection** (a CH340X on the boards we have). This is all you need;
  it is both the programmer and the console.
- An antenna on the MM8108. Also put some distance or an attenuator between it and any AP you are
  testing against — a signal around -5 dBm is hot enough to compress the receiver and depress
  throughput measurably.
- A debug probe (DAPLink/CMSIS-DAP) is **optional**. It is useful for halting and inspecting memory,
  but it **cannot program flash on this part** (see `readme.md`), so it is not needed for this guide.

**Host tools**

- `arm-none-eabi-gcc` toolchain, on `PATH`.
- Python 3 with `hjson` and `protobuf`, for the config-store generator. Either use the SDK's pipenv
  (`cd framework && pipenv sync`) or a throwaway venv:

      python3 -m venv /tmp/cs_venv
      /tmp/cs_venv/bin/pip install hjson protobuf

- No OpenOCD required.

**Check your board matches these assumptions.** `gd32_isp.py` assumes the USB-serial bridge drives
the boot pins as follows, which is what our boards do:

    DTR -> resistor -> BOOT0     DTR asserted = BOOT0 low  = boot from flash
    RTS -> diode    -> NRST      RTS asserted = NRST low    = held in reset

If your board wires these differently — or not at all — nothing in this guide will work until the
tool is adjusted. Confirm against your schematic **before** debugging anything else.

# 1. Find the serial port

    ls /dev/cu.usbserial*        # macOS
    ls /dev/ttyUSB*              # Linux

The port name changes regularly (it has been `-1110`, `-310`, `-10` on one machine in a single day),
so don't hardcode it. `gd32_isp.py` auto-detects; pass `-P /dev/...` only if you have several.

If **nothing** appears, the bridge is not enumerating: replug the cable, and check you are using the
board's serial USB connector rather than a debug probe's.

# 2. Prove the board is alive — do this first

    ./framework/tools/platform/gd32_isp.py probe

Expected:

    INFO: port: /dev/cu.usbserial-10
    INFO: MCU alive: ROM bootloader v3.0
    INFO: supported commands: 00 11 21 31 44 63 73 82 92 00 20 03
    INFO: flash vectors: SP=0x2002B120 Reset=0x0807543D NMI=0x08075485 HardFault=0x08075485

This is worth doing before anything else, because one command confirms an enormous amount: the part
has power and a working clock, reset and the boot straps are wired as expected, and the UART path to
your host is intact. If this works, any later silence is a *firmware* problem, not a board problem.

If it fails, stop here and fix it — see [Troubleshooting](#troubleshooting). Do not proceed.

# 3. Build the example

    cd examples/ap_mode/targets/mm-ekh18-gd32f527
    make bin

`make bin` (not just `make`) is required — plain `make` stops at the `.elf` and the ISP flasher needs
the `.bin`. Expect roughly:

               FLASH:      897404 B      2000 KB     43.82%
                 RAM:      176416 B       256 KB     67.30%

# 4. Identify your BCF

The BCF (Board Configuration File) holds the module's RF calibration and regulatory data. **The radio
will not boot without it.**

Find the part number printed on your MM8108 module's label — it looks like `MM8108-MFxxxxx` — and
pick the matching file from `framework/morsefirmware/mm8108/bcfs/`:

    bcf_boardtype_0804.mbin
    bcf_boardtype_0807.mbin
    bcf_boardtype_0a02.mbin
    bcf_mf15457.mbin

For an `MM8108-MF15457` module that is `bcf_mf15457.mbin`. **Do not just copy the value from someone
else's setup** — a mismatched BCF means wrong RF calibration and potentially a non-compliant
transmitter.

# 5. Write a local config overlay

The tracked `examples/*/config.hjson` files deliberately keep `bcf_file` **commented out** and carry
only placeholder Wi-Fi credentials, because BCF paths and credentials are per-board/per-site
configuration and must not be committed. You supply the real values through a small overlay file
that you keep **outside** the repo.

`program-configstore.py`'s `write-json` accepts multiple files and later files win, so the overlay
only needs the keys you are adding.

Create `~/gd32f527-local.hjson`:

    {
        "strings" : {
        },
        "hex_strings" : {
        },
        "files" : {
            "bcf_file": "$MMIOT_ROOT/morsefirmware/mm8108/bcfs/bcf_mf15457.mbin"
        }
    }

For `ap_mode`, a BCF-only overlay is what you want. `ap_mode` sets its own `wlan.ap_ssid` /
`wlan.ap_password` in its tracked config, and an overlay that also sets `wlan.ssid`/`wlan.password`
would override the wrong things. Keep a separate overlay with credentials for the STA examples
(`ping`, `iperf`, `scan`) if you need one.

# 6. Program the config store {#step-6-program-the-config-store}

Three sub-steps. **This is the part people get wrong.**

**6a. Generate the image** (offline — `-o` writes a file instead of pushing through a debugger):

    cd framework
    MMIOT_ROOT=$PWD /tmp/cs_venv/bin/python tools/platform/program-configstore.py \
        -o /tmp/cs_ap.bin -p mm-ekh18-gd32f527 \
        write-json ../examples/ap_mode/config.hjson ~/gd32f527-local.hjson

Sanity-check that the BCF actually got embedded — the image should contain the `MMBC` magic:

    python3 -c "d=open('/tmp/cs_ap.bin','rb').read(); print('BCF embedded:', b'MMBC' in d)"

Expect `BCF embedded: True`. If it prints `False`, your overlay path is wrong or `MMIOT_ROOT` is
unset, and the config store you are about to flash has no BCF in it — fix that before flashing.

(Use the Python check rather than `grep`. Plain `grep -c MMBC` on this binary reports no match and
exits 1 even when the string *is* present; you would need `grep -ac`, and that differs between BSD
and GNU grep.)

**6b. Pad to the full 32 KB config region:**

    python3 -c "d=open('/tmp/cs_ap.bin','rb').read(); \
      open('/tmp/cs_ap_32k.bin','wb').write(d + b'\xff'*(32768-len(d)))"

This matters. The config region is 32 KB spanning **two** flash pages. The generated image is only
~1.7 KB, so without padding the flasher only erases the first page and the second is left full of
zeros from the firmware image — not erased. Padding with `0xFF` makes the tool erase both pages, and
writing `0xFF` to erased flash is a no-op (the flasher skips all-blank chunks, so it costs nothing).

**6c. Flash it — after the firmware, never before.** See the next step.

# 7. Flash, in this order

**Firmware first:**

    cd /path/to/mm-iot-sdk
    ./framework/tools/platform/gd32_isp.py flash \
        examples/ap_mode/targets/mm-ekh18-gd32f527/build/ap_mode.bin

You will see this warning, and it is the whole point of the ordering:

    WARNING: this image spans the mmconfig CONFIG region (0x08004000-0x0800C000);
             the config store has been erased and must be reprogrammed

The application image starts at `0x08000000` and the config region sits between the vector table and
the application, so `objcopy`'s flat binary covers it and fills it with zeros. **Every firmware flash
destroys the config store.**

**Config store second:**

    ./framework/tools/platform/gd32_isp.py flash /tmp/cs_ap_32k.bin --addr 0x08004000

Both commands verify by reading back; look for `verify OK`. Firmware takes ~2.5 minutes at 115200;
the config store takes under a second.

# 8. Run it

    ./framework/tools/platform/gd32_isp.py console -d 86400 -i 86400

Expected:

    AP Mode Example (Built ...)
    -----------------------------------
      HW Version:              MM-EKH18-GD32F527
      BCF API version:         12.1.0
      BCF board description:   {"module": "mm8108-mf15457", "s
      Morse firmware version:  1.17.9
      Morse chip ID:           0x0809
      Morse chip name:         MM8108B2
    -----------------------------------
    Morse LwIP interface initialised.
          1198 Starting AP mode...
          4093 AP started successfully
    Morse AP interface up with MAC address 0e:bf:74:00:1e:43
          4170 NetIf Link status UP
    Link is up. Time: 4170 ms, IP: 192.168.1.2, Netmask: 255.255.255.0, Gateway: 192.168.1.1

The `BCF board description` line is your proof the config store worked — the firmware is reading
that string back out of the store. If the banner shows it, step 6 succeeded.

**Then it goes quiet, and that is normal.** `ap_mode` emits about 796 bytes in its first 4 seconds
and then nothing until something happens. It *does* log station events, so leave the console running
and connect a HaLow client to the `GD32F470`/whatever `wlan.ap_ssid` says:

         89358 AP STA 94:bb:43:dc:fb:53 State: Associated, AID: 1
         89567 AP STA 94:bb:43:dc:fb:53 State: Authorized, AID: 1

`Authorized` (not just `Associated`) means the SAE handshake completed. Note the timestamp — events
can arrive a minute or more after boot, so short capture windows will miss them. That is why
`-d 86400 -i 86400` above: `-d` is the maximum run time and `-i` the "give up after this much
silence" timeout, and both default to short values suited to scripted runs.

Ctrl-C to stop.

# Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| No `/dev/cu.usbserial*` at all | Bridge not enumerating | Replug. Check you're on the board's serial USB, not a probe |
| `probe`: no response to 0x7F | No power / wrong cable / boot strap wiring differs from the assumption in step 0 | Check power and the schematic's DTR/RTS mapping |
| `termios.error: (22, 'Invalid argument')` | Host CH340 driver wedged. Diagnostic: writing back *unmodified* termios attrs also fails | Replug the cable |
| `console` prints the port line then nothing | Usually the firmware is hanging, **or** you piped through `tail`/`grep`, which buffers until exit | Run it bare, no pipe. Then `probe` to confirm the board is alive |
| Runs, but no console output ever, with clocks/UART clearly configured | Old firmware without the SRAM-ECC `.noinit` fix, or `.noinit` not primed | Make sure you are building this branch; see the ECC section in `readme.md` |
| `Unable to find BCF_FILE entry in config store` | Config store empty — you either skipped step 6, or flashed firmware *after* it | Reflash the config store (step 6c). Order matters |
| `Validate BCF` fails in `porting_assistant` | Same as above | Same as above |
| Config store flashed but banner shows no `BCF board description` | Overlay didn't supply the BCF (`MMBC` missing) | Re-check `MMIOT_ROOT` and the overlay path; verify with the Python `MMBC` check in step 6a |
| AP starts, then silence | Normal — nothing happens until a client connects | Leave the console running with a long `-i` |
| `minicom` / `screen` show nothing | They leave RTS asserted, which holds NRST low, so the MCU sits in reset | Use `gd32_isp.py console`, or `picocom -b 115200 --lower-rts /dev/cu.usbserialX` |
| Board seems to run only while a terminal is open | Expected. With the port closed, DTR idles high → BOOT0 high → the board runs the **ROM bootloader**, not your firmware | Not a fault |
| `make reprogram` / `make reconfig` fail | Both go through OpenOCD, which cannot program this part | Use `gd32_isp.py` and the manual config-store steps above |
| Symptom appears/disappears between flashes | Replugging USB power-cycles the board and can mask reset-related bugs | Reproduce without replugging before concluding anything |

# Quick reference

    gd32_isp.py probe                          # liveness + vector table
    gd32_isp.py flash <img.bin>                # erase+write+verify at 0x08000000
    gd32_isp.py flash <cs.bin> --addr 0x08004000
    gd32_isp.py verify <img.bin>               # compare flash against a file
    gd32_isp.py read -a 0x20000000 -n 256      # read memory (RAM survives a warm reset)
    gd32_isp.py console -d 86400 -i 86400      # boot from flash and stream

`gd32_isp.py --help` documents the wiring assumptions, the config-store recipe and the reasons the
usual tooling doesn't apply here.
