#!/usr/bin/env python3
#
# Copyright 2026 Morse Micro
#
# SPDX-License-Identifier: Apache-2.0
#

__doc__ = """
UART ISP flasher for GD32F5xx platforms (mm-ekh18-gd32f527), using the MCU's on-chip ROM
bootloader over the board's CH340X USB-serial bridge.

WHY THIS EXISTS
---------------
OpenOCD cannot program flash on the GD32F527. Its stm32f2x driver selects the DBGMCU_IDCODE
address by switching on the CPUID part number (0xC23=M3, 0xC24=M4, 0xC27=M7) and errors out with
"Cannot identify target as a STM32 family" for anything else -- the GD32F527 is a Cortex-M33
(part 0xD21). The stm32l4x/l5x/u5x drivers read the correct ARMv8-M address (0xE0044000, which
does hold 0x23090419) but 0x419 is not in their device table, and stm32h7x reads 0x5C001000 which
does not exist on this part. SWD debug, halt and memory access all work fine via OpenOCD -- only
flash programming is blocked. Fixing that properly means patching stm32f2x.c and rebuilding
OpenOCD.

DAPLink drag-and-drop (cp image.bin /Volumes/DAPLINK/) also works, but has proven unreliable in
practice: the volume disappears for long periods, and on at least one occasion the flash silently
reverted to the previously programmed image.

This script depends on neither. It only needs the CH340X, which is always present.

BOARD WIRING (one-key download circuit)
---------------------------------------
    DTR -> resistor -> BOOT0     DTR asserted = DTR# low = BOOT0 low = boot from flash
    RTS -> diode    -> NRST      RTS asserted = RTS# low = NRST held low

Consequences worth knowing:
  * With the port closed, DTR# idles high, so BOOT0 is high and the board boots the ISP ROM
    rather than your firmware. A serial port must be open holding DTR asserted for the
    application to run.
  * macOS asserts both DTR and RTS when the port is opened.
  * Hardware flow control MUST stay disabled (no CRTSCTS), or the tty layer will drive RTS --
    which on this board is the reset line.
  * The ISP ROM speaks 8E1; the firmware console is 8N1. This script switches between them.

*** WARNING: FLASHING FIRMWARE WIPES THE CONFIG STORE ***
Flash pages 1 and 2 are the mmconfig CONFIG region (0x08004000, 32 KB). A full firmware image
starts at 0x08000000 and therefore spans them, and objcopy fills that gap with zeros. After
flashing firmware you must reprogram the config store or "Validate BCF" (and anything else
reading config) will fail. program-configstore.py can emit an image offline instead of going
through OpenOCD:

    MMIOT_ROOT=$PWD python3 tools/platform/program-configstore.py -o cs.bin \\
        -p mm-ekh18-gd32f527 write-json ../examples/porting_assistant/config.hjson
    python3 -c "d=open('cs.bin','rb').read(); \\
        open('cs32k.bin','wb').write(d + b'\\xff'*(32768-len(d)))"
    ./tools/platform/gd32_isp.py flash cs32k.bin --addr 0x08004000

EXAMPLES
--------
    # Is the MCU alive at all? (sends 0x7F, expects the 0x79 ACK)
    ./gd32_isp.py probe

    # Program and verify an application image
    ./gd32_isp.py flash build/porting_assistant.bin

    # Boot from flash and stream the console
    ./gd32_isp.py console
"""

import argparse
import fcntl
import glob
import logging
import os
import struct
import sys
import termios
import time

# macOS/BSD modem-control ioctls. Note TIOCMBIC is 0x8004746B; 0x8004746D is TIOCMSET, and
# confusing the two silently clears RTS (== NRST here) as a side effect.
TIOCMBIS, TIOCMBIC, TIOCMGET = 0x8004746C, 0x8004746B, 0x4004746A
TIOCM_DTR, TIOCM_RTS = 0x0002, 0x0004

ACK, NACK = 0x79, 0x1F
FLASH_BASE = 0x08000000
CHUNK = 256                      # protocol maximum for one Write Memory command

# GD32F527xM bank0/bank1 erase page geometry, per GD32MCUFlashXML/GD32F527.xml
# (GD32F527XMXX: 4096 KB, 40 pages, dual bank).
PAGES = (
    [(n, 0x08000000 + n * 16 * 1024, 16 * 1024) for n in range(4)] +
    [(4, 0x08010000, 64 * 1024)] +
    [(5 + i, 0x08020000 + i * 128 * 1024, 128 * 1024) for i in range(15)] +
    [(20 + n, 0x08200000 + n * 16 * 1024, 16 * 1024) for n in range(4)] +
    [(24, 0x08210000, 64 * 1024)] +
    [(25 + i, 0x08220000 + i * 128 * 1024, 128 * 1024) for i in range(15)]
)

CONFIGSTORE_RANGE = (0x08004000, 0x0800C000)   # pages 1-2, see warning above


class IspError(Exception):
    pass


class Gd32Isp:
    """Minimal GigaDevice/ST ROM bootloader client (protocol per ST AN3155)."""

    def __init__(self, port=None):
        if port is None:
            found = sorted(glob.glob('/dev/cu.usbserial*')) + \
                    sorted(glob.glob('/dev/cu.wchusbserial*')) + \
                    sorted(glob.glob('/dev/ttyUSB*'))
            if not found:
                raise IspError("no CH340X serial port found (looked for cu.usbserial*, "
                               "cu.wchusbserial*, ttyUSB*)")
            port = found[0]
        self.port = port
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)

    def close(self):
        os.close(self.fd)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    # -- low level ---------------------------------------------------------

    def configure(self, even_parity):
        """8E1 for the ISP ROM, 8N1 for the firmware console. Never enables CRTSCTS."""
        attrs = termios.tcgetattr(self.fd)
        attrs[0] = attrs[1] = attrs[3] = 0
        cflag = termios.CS8 | termios.CREAD | termios.CLOCAL
        if even_parity:
            cflag |= termios.PARENB
        attrs[2] = cflag
        attrs[4] = attrs[5] = termios.B115200
        cc = list(attrs[6])
        cc[termios.VMIN] = 0
        cc[termios.VTIME] = 0
        attrs[6] = cc
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)

    def reset(self, boot_from_flash):
        """Pulse NRST via RTS, with BOOT0 (DTR) selecting flash or the ISP ROM."""
        fcntl.ioctl(self.fd, TIOCMBIS if boot_from_flash else TIOCMBIC,
                    struct.pack('I', TIOCM_DTR))
        time.sleep(0.05)
        termios.tcflush(self.fd, termios.TCIOFLUSH)
        fcntl.ioctl(self.fd, TIOCMBIS, struct.pack('I', TIOCM_RTS))     # NRST low
        time.sleep(0.15)
        fcntl.ioctl(self.fd, TIOCMBIC, struct.pack('I', TIOCM_RTS))     # release
        time.sleep(0.35)

    def _write(self, data):
        while data:
            try:
                data = data[os.write(self.fd, data):]
            except BlockingIOError:
                time.sleep(0.001)

    def _read(self, count, timeout=2.0):
        buf = b''
        deadline = time.monotonic() + timeout
        while len(buf) < count and time.monotonic() < deadline:
            try:
                chunk = os.read(self.fd, count - len(buf))
            except BlockingIOError:
                chunk = b''
            if chunk:
                buf += chunk
            else:
                time.sleep(0.002)
        return buf

    def _ack(self, what, timeout=2.0):
        reply = self._read(1, timeout)
        if reply == bytes([ACK]):
            return
        detail = 'NACK' if reply == bytes([NACK]) else 'timeout' if not reply else reply.hex()
        raise IspError(f"{what}: {detail}")

    def _command(self, code, timeout=2.0):
        self._write(bytes([code, code ^ 0xFF]))
        self._ack(f"command 0x{code:02X}", timeout)

    @staticmethod
    def _xor(data):
        acc = 0
        for byte in data:
            acc ^= byte
        return acc

    # -- protocol ----------------------------------------------------------

    def sync(self):
        """Reset into the ROM bootloader and autobaud it. Also a liveness test."""
        self.configure(even_parity=True)
        self.reset(boot_from_flash=False)
        for _ in range(8):
            self._write(b'\x7f')
            reply = self._read(1, 0.5)
            if reply in (bytes([ACK]), bytes([NACK])):
                return
            time.sleep(0.15)
        raise IspError("no response to 0x7F sync -- MCU not running "
                       "(check power, NRST, and that BOOT0/DTR is wired as documented)")

    def get(self):
        """Get command: returns (bootloader_version, supported_command_bytes)."""
        self._command(0x00)
        count = self._read(1)[0]
        body = self._read(count + 1)
        self._read(1)
        return body[0], body[1:]

    def read_memory(self, addr, count):
        self._command(0x11)
        packed = struct.pack('>I', addr)
        self._write(packed + bytes([self._xor(packed)]))
        self._ack("read address")
        self._write(bytes([count - 1, (count - 1) ^ 0xFF]))
        self._ack("read length")
        data = self._read(count, 3.0)
        if len(data) != count:
            raise IspError(f"short read at 0x{addr:08X}: {len(data)}/{count}")
        return data

    def write_memory(self, addr, data):
        self._command(0x31)
        packed = struct.pack('>I', addr)
        self._write(packed + bytes([self._xor(packed)]))
        self._ack("write address")
        body = bytes([len(data) - 1]) + data
        self._write(body + bytes([self._xor(body)]))
        self._ack("write data", timeout=5.0)

    def erase_pages(self, pages):
        """Extended Erase (0x44) of an explicit page list."""
        self._command(0x44, timeout=5.0)
        payload = struct.pack('>H', len(pages) - 1)
        for page in pages:
            payload += struct.pack('>H', page)
        self._write(payload + bytes([self._xor(payload)]))
        self._ack("erase", timeout=120.0)

    def go(self, addr):
        self._command(0x21)
        packed = struct.pack('>I', addr)
        self._write(packed + bytes([self._xor(packed)]))
        self._ack("go")

    # -- console -----------------------------------------------------------

    def console(self, duration=60.0, idle=10.0):
        """Boot from flash and stream USART0 to stdout until idle. Returns bytes seen."""
        self.configure(even_parity=False)
        self.reset(boot_from_flash=True)
        start = last = time.monotonic()
        total = 0
        while time.monotonic() - start < duration:
            try:
                chunk = os.read(self.fd, 512)
            except BlockingIOError:
                chunk = b''
            if chunk:
                total += len(chunk)
                last = time.monotonic()
                os.write(1, chunk)
            else:
                time.sleep(0.01)
                if total and time.monotonic() - last > idle:
                    break
        return total


def pages_for(addr, size):
    end = addr + size
    covered = [n for n, base, page_size in PAGES if base < end and base + page_size > addr]
    if not covered:
        raise IspError(f"0x{addr:08X}+{size} is outside the known flash page map")
    return covered


def overlaps_configstore(addr, size):
    lo, hi = CONFIGSTORE_RANGE
    return addr < hi and addr + size > lo


def load_image(path):
    with open(path, 'rb') as handle:
        image = handle.read()
    if len(image) % 4:
        image += b'\xff' * (4 - len(image) % 4)     # flash writes must be word aligned
    return image


def do_flash(isp, args):
    image = load_image(args.image)
    logging.info("image %s: %d bytes -> 0x%08X", args.image, len(image), args.addr)

    isp.sync()
    version, commands = isp.get()
    logging.info("ISP ready: bootloader v%d.%d (commands %s)",
                 version >> 4, version & 0xF, commands.hex(' '))

    pages = pages_for(args.addr, len(image))
    logging.info("erasing pages %s", pages)
    isp.erase_pages(pages)

    start = time.monotonic()
    written = skipped = 0
    for offset in range(0, len(image), CHUNK):
        chunk = image[offset:offset + CHUNK]
        # The pages were just erased, so an all-0xFF chunk is already correct.
        if args.skip_blank and chunk.count(0xFF) == len(chunk):
            skipped += len(chunk)
            continue
        isp.write_memory(args.addr + offset, chunk)
        written += len(chunk)
        if (offset // CHUNK) % 40 == 0:
            pct = 100.0 * (offset + len(chunk)) / len(image)
            print(f"\r  writing {pct:5.1f}%", end='', flush=True)
    print("\r" + " " * 24 + "\r", end='')
    logging.info("wrote %d bytes in %.1fs (%d skipped as blank)",
                 written, time.monotonic() - start, skipped)

    if overlaps_configstore(args.addr, len(image)):
        logging.warning("this image spans the mmconfig CONFIG region (0x%08X-0x%08X); "
                        "the config store has been erased and must be reprogrammed "
                        "(see this script's --help)", *CONFIGSTORE_RANGE)
    return do_verify(isp, args, already_synced=True)


def do_verify(isp, args, already_synced=False):
    image = load_image(args.image)
    if not already_synced:
        isp.sync()
        isp.get()
    bad = 0
    for offset in range(0, len(image), CHUNK):
        expected = image[offset:offset + CHUNK]
        actual = isp.read_memory(args.addr + offset, len(expected))
        if actual != expected:
            bad += 1
            if bad <= 3:
                logging.error("mismatch at 0x%08X", args.addr + offset)
        if (offset // CHUNK) % 60 == 0:
            print(f"\r  verifying {100.0 * offset / len(image):5.1f}%", end='', flush=True)
    print("\r" + " " * 24 + "\r", end='')
    if bad:
        logging.error("verify FAILED: %d bad blocks", bad)
        return 1
    logging.info("verify OK: %d bytes match", len(image))
    return 0


def do_probe(isp, _args):
    isp.sync()
    version, commands = isp.get()
    logging.info("MCU alive: ROM bootloader v%d.%d", version >> 4, version & 0xF)
    logging.info("supported commands: %s", commands.hex(' '))
    vectors = isp.read_memory(FLASH_BASE, 16)
    sp, reset, nmi, hardfault = struct.unpack('<4I', vectors)
    logging.info("flash vectors: SP=0x%08X Reset=0x%08X NMI=0x%08X HardFault=0x%08X",
                 sp, reset, nmi, hardfault)
    return 0


def do_read(isp, args):
    isp.sync()
    isp.get()
    data = b''
    while len(data) < args.length:
        want = min(CHUNK, args.length - len(data))
        data += isp.read_memory(args.addr + len(data), want)
    if args.output:
        with open(args.output, 'wb') as handle:
            handle.write(data)
        logging.info("wrote %d bytes to %s", len(data), args.output)
    else:
        for i in range(0, len(data), 16):
            row = data[i:i + 16]
            logging.info("0x%08X  %-47s  %s", args.addr + i, row.hex(' '),
                         ''.join(chr(b) if 32 <= b < 127 else '.' for b in row))
    return 0


def do_console(isp, args):
    total = isp.console(duration=args.duration, idle=args.idle)
    logging.info("%d bytes of console output", total)
    return 0


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-v", "--verbose", action="count", default=0,
                        help="Increase verbosity of log messages")
    parser.add_argument("-P", "--port",
                        help="Serial port of the CH340X (default: first cu.usbserial*/ttyUSB*)")
    sub = parser.add_subparsers(dest="command", required=True)

    def add_addr(p, default=FLASH_BASE):
        p.add_argument("-a", "--addr", type=lambda s: int(s, 0), default=default,
                       help=f"Target address (default 0x{default:08X})")

    p_flash = sub.add_parser("flash", help="Erase, program and verify an image")
    p_flash.add_argument("image")
    add_addr(p_flash)
    p_flash.add_argument("--no-skip-blank", dest="skip_blank", action="store_false",
                         help="Write all-0xFF chunks instead of skipping them")
    p_flash.set_defaults(func=do_flash, skip_blank=True)

    p_verify = sub.add_parser("verify", help="Compare flash against an image")
    p_verify.add_argument("image")
    add_addr(p_verify)
    p_verify.set_defaults(func=do_verify)

    p_probe = sub.add_parser(
        "probe", help="Liveness check: sync with the ROM bootloader and dump the vector table")
    p_probe.set_defaults(func=do_probe)

    p_read = sub.add_parser("read", help="Read flash/memory to stdout or a file")
    add_addr(p_read)
    p_read.add_argument("-n", "--length", type=lambda s: int(s, 0), default=256)
    p_read.add_argument("-o", "--output")
    p_read.set_defaults(func=do_read)

    p_console = sub.add_parser("console", help="Boot from flash and stream the console")
    p_console.add_argument("-d", "--duration", type=float, default=60.0,
                           help="Maximum seconds to stream for (default 60)")
    p_console.add_argument("-i", "--idle", type=float, default=10.0,
                           help="Stop early after this many seconds with no output, once "
                                "something has been received (default 10). Raise it for tests "
                                "that go quiet for a long time, e.g. a 100s iperf transfer that "
                                "only reports at the end.")
    p_console.set_defaults(func=do_console)

    args = parser.parse_args()
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO,
                        format="%(levelname)s: %(message)s")
    try:
        with Gd32Isp(args.port) as isp:
            logging.info("port: %s", isp.port)
            return args.func(isp, args)
    except IspError as exc:
        logging.error("%s", exc)
        return 1


if __name__ == '__main__':
    sys.exit(main())
