#!/usr/bin/env python3
"""Run an ojay-arm32 image on an NXP FRDM-MCXN947 board.

RAM images (linked with runtime/boards/mcxn947-ram.ld) are loaded into SRAM through
the on-board MCU-Link (CMSIS-DAP) with OpenOCD; nothing is written to flash.

Flash images (linked with mcxn947-flash.ld) are only run with --flash: the
image is programmed with NXP LinkServer, then started from its reset vector
under OpenOCD so its exit status can be reported.

In both cases the program's semihosting console output goes to stdout.

Exit status: the program's exit status, or
  124  the program did not finish within --timeout seconds
  125  could not talk to the board (probe missing, OpenOCD error, ...)
  126  the core stopped without the program exiting
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RAM_START, RAM_END = 0x20000000, 0x20080000
FLASH_START, FLASH_END = 0x00000000, 0x00200000
DEVICE = "MCXN947:FRDM-MCXN947"


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
    script = (
        "init; mcxn947_start_debug_session; mcxn947.cpu0 arp_examine; halt; "
        f"{load}"
        f"mww 0xE000ED08 {base:#x}; reg msp {sp:#x}; reg pc {reset:#x}; reg xPSR 0x01000000; "
        "arm semihosting enable; resume; "
        # A semihosting exit makes OpenOCD exit with the program's status, so
        # reaching the lines below means something went wrong.
        f"if {{[catch {{wait_halt {ms}}}]}} {{ echo OJAY-RUN-TIMEOUT; halt; shutdown error }}; "
        "echo OJAY-RUN-HALTED; shutdown error"
    )
    proc = subprocess.run([args.openocd, "-f", args.cfg, "-c", script], stderr=subprocess.PIPE,
                          timeout=args.timeout + 30)
    log = proc.stderr.decode(errors="replace")
    if args.verbose:
        sys.stderr.write(log)
    if "OJAY-RUN-TIMEOUT" in log:
        print(f"run_mcxn947: timed out after {args.timeout:g}s", file=sys.stderr)
        return 124
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
