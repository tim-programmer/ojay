#!/usr/bin/env python3
"""Run an ojay-arm32 image on an NXP FRDM-MCXN947 board.

RAM images (linked with runtime/boards/mcxn947-ram.ld) are loaded into SRAM through
the on-board MCU-Link (CMSIS-DAP) with OpenOCD; nothing is written to flash.

Flash images (linked with mcxn947-flash.ld) are only run with --flash: the
image is programmed with NXP LinkServer, then started from its reset vector
under OpenOCD so its exit status can be reported.

In both cases the program's semihosting console output goes to stdout. The
program is started with r0/r1 set to the runtime's "debugger present" values,
which enable semihosting (without them the runtime never makes semihosting
calls on this board, so a leftover DHCSR.C_DEBUGEN cannot halt it).

With --uart the program is started without them, as if no debugger were
attached: its output is read from the board's UART (LPUART4, the MCU-Link's
virtual COM port) instead, and the exit status from r0/r1 once the program
waits in its final wfi loop.

Exit status: the program's exit status, or
  124  the program did not finish within --timeout seconds
  125  could not talk to the board (probe missing, OpenOCD error, ...)
  126  the core stopped without the program exiting
"""
import argparse
import glob
import os
import re
import select
import termios
import threading
import time
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RAM_START, RAM_END = 0x20000000, 0x20080000
FLASH_START, FLASH_END = 0x00000000, 0x00200000
DEVICE = "MCXN947:FRDM-MCXN947"
DEBUGGER_MAGIC = 0x5E41D0B6   # runtime/ojay_runtime.c: OJAY_DEBUGGER_MAGIC
EXITED_MAGIC = 0xE417C0DE     # OJAY_EXITED_MAGIC


def find_vcom():
    found = sorted(glob.glob("/dev/serial/by-id/*MCU-LINK*FRDM-MCXN947*-if0[0-9]"))
    return found[0] if found else None


class SerialReader:
    """Reads a tty at 115200 8N1 in the background, forwarding to stdout (CRLF -> LF)."""

    def __init__(self, path):
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attrs = termios.tcgetattr(self.fd)
        attrs[0] = 0                                        # iflag: raw
        attrs[1] = 0                                        # oflag
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        attrs[3] = 0                                        # lflag: no echo, non-canonical
        attrs[4] = attrs[5] = termios.B115200
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        termios.tcflush(self.fd, termios.TCIFLUSH)
        self.stop = False
        self.data = bytearray()
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        while not self.stop:
            r, _, _ = select.select([self.fd], [], [], 0.05)
            if r:
                try:
                    chunk = os.read(self.fd, 4096)
                except BlockingIOError:
                    continue
                self.data += chunk

    def finish(self):
        time.sleep(0.2)  # the last bytes in flight
        self.stop = True
        self.thread.join()
        os.close(self.fd)
        out = bytes(self.data).replace(b"\r\n", b"\n")
        sys.stdout.buffer.write(out)
        sys.stdout.flush()


def find_linkserver():
    for cand in (os.environ.get("OJAY_LINKSERVER"), shutil.which("LinkServer"), "/opt/linkserver/LinkServer"):
        if cand and os.path.isfile(cand) and os.access(cand, os.X_OK):
            return cand
    return None


def image_vectors(path):
    """Returns (load address, initial SP, reset vector) of an ELF image."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        raise ValueError("not a 32-bit little-endian ELF file")
    e_phoff, = struct.unpack_from("<I", data, 28)
    e_phentsize, e_phnum = struct.unpack_from("<HH", data, 42)
    loads = []
    for i in range(e_phnum):
        p_type, p_offset, p_vaddr, p_paddr, p_filesz = struct.unpack_from("<IIIII", data, e_phoff + i * e_phentsize)
        if p_type == 1 and p_filesz >= 8:
            loads.append((p_paddr, p_offset))
    if not loads:
        raise ValueError("no loadable segments")
    base, off = min(loads)
    sp, reset = struct.unpack_from("<II", data, off)
    return base, sp, reset


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf", nargs="?")
    ap.add_argument("--probe", action="store_true", help="only check that the board responds (exit 0 if so)")
    ap.add_argument("--timeout", type=float, default=30.0, help="seconds (default 30)")
    ap.add_argument("--openocd", default=shutil.which("openocd") or "openocd")
    ap.add_argument("--cfg", default=os.path.join(HERE, "mcxn947.cfg"))
    ap.add_argument("--flash", action="store_true", help="program a flash image (with LinkServer) and run it")
    ap.add_argument("--uart", action="store_true",
                    help="run without semihosting; read the output from the board's UART (virtual COM port)")
    ap.add_argument("--vcom", default=os.environ.get("OJAY_MCXN947_VCOM"),
                    help="serial device of the MCU-Link virtual COM port (default: found in /dev/serial/by-id)")
    ap.add_argument("-v", "--verbose", action="store_true", help="show OpenOCD's log")
    args = ap.parse_args()

    if args.probe:
        r = subprocess.run([args.openocd, "-f", args.cfg, "-c",
                            "init; mcxn947_start_debug_session; mcxn947.cpu0 arp_examine; shutdown"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=30)
        return 0 if "processor detected" in r.stderr.decode(errors="replace") else 125
    if not args.elf:
        ap.error("an ELF file is required")
    try:
        base, sp, reset = image_vectors(args.elf)
    except (OSError, ValueError) as e:
        print(f"run_mcxn947: {args.elf}: {e}", file=sys.stderr)
        return 125
    in_flash = FLASH_START <= base < FLASH_END
    if in_flash and not args.flash:
        print(f"run_mcxn947: {args.elf} is a flash image (linked at {base:#x}); pass --flash to program "
              "and run it, or link a RAM image with mcxn947-ram.ld", file=sys.stderr)
        return 125
    if not in_flash and not (RAM_START <= base < RAM_END):
        print(f"run_mcxn947: {args.elf} is linked at {base:#x}, which is neither flash nor SRAM", file=sys.stderr)
        return 125

    elf = args.elf.replace("\\", "/")
    load = f"load_image {{{elf}}}; "
    if in_flash:
        linkserver = find_linkserver()
        if not linkserver:
            print("run_mcxn947: LinkServer not found (set OJAY_LINKSERVER)", file=sys.stderr)
            return 125
        r = subprocess.run([linkserver, "flash", DEVICE, "load", "--no-reset", args.elf],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
        if r.returncode != 0:
            sys.stderr.write(r.stdout.decode(errors="replace"))
            print("run_mcxn947: programming flash failed", file=sys.stderr)
            return 125
        load = ""

    ms = int(args.timeout * 1000)
    start = (
        "init; mcxn947_start_debug_session; mcxn947.cpu0 arp_examine; halt; "
        f"{load}"
        f"mww 0xE000ED08 {base:#x}; reg msp {sp:#x}; reg pc {reset:#x}; reg xPSR 0x01000000; "
    )
    reader = None
    if args.uart:
        vcom = args.vcom or find_vcom()
        if not vcom:
            print("run_mcxn947: the MCU-Link virtual COM port was not found (use --vcom)", file=sys.stderr)
            return 125
        reader = SerialReader(vcom)
        # No semihosting: poll (halting briefly) until the program waits in
        # its exit loop with r0 = EXITED_MAGIC, then report r1.
        script = start + (
            "reg r0 0; resume; "
            f"set deadline [expr {{[clock milliseconds] + {ms}}}]; "
            "while {1} { "
            "  sleep 100; halt; "
            "  regexp {0x([0-9a-fA-F]+)} [reg r0] -> r0; "
            f"  if {{[expr 0x$r0] == {EXITED_MAGIC}}} {{ "
            "    regexp {0x([0-9a-fA-F]+)} [reg r1] -> r1; "
            "    echo \"OJAY-EXIT [expr 0x$r1]\"; shutdown }; "
            "  if {[clock milliseconds] > $deadline} { echo OJAY-RUN-TIMEOUT; shutdown error }; "
            "  resume "
            "}"
        )
    else:
        script = start + (
            f"reg r0 {DEBUGGER_MAGIC:#x}; reg r1 {DEBUGGER_MAGIC ^ 0xFFFFFFFF:#x}; "
            "arm semihosting enable; resume; "
            # A semihosting exit makes OpenOCD exit with the program's status, so
            # reaching the lines below means something went wrong.
            f"if {{[catch {{wait_halt {ms}}}]}} {{ echo OJAY-RUN-TIMEOUT; halt; shutdown error }}; "
            "echo OJAY-RUN-HALTED; shutdown error"
        )
    try:
        proc = subprocess.run([args.openocd, "-f", args.cfg, "-c", script], stderr=subprocess.PIPE,
                              timeout=args.timeout + 30)
    finally:
        if reader:
            reader.finish()
    log = proc.stderr.decode(errors="replace")
    if args.verbose:
        sys.stderr.write(log)
    if "OJAY-RUN-TIMEOUT" in log:
        print(f"run_mcxn947: timed out after {args.timeout:g}s", file=sys.stderr)
        return 124
    if args.uart:
        m = re.search(r"OJAY-EXIT (\d+)", log)
        if m:
            return int(m.group(1)) & 0xFF  # as a process exit status
    if "OJAY-RUN-HALTED" in log:
        print("run_mcxn947: the core halted without the program exiting", file=sys.stderr)
        return 126
    if "processor detected" not in log or "Error:" in log.split("processor detected")[0]:
        if not args.verbose:
            sys.stderr.write(log)
        print("run_mcxn947: could not start the program on the board", file=sys.stderr)
        return 125
    return proc.returncode


if __name__ == "__main__":
    sys.exit(main())
