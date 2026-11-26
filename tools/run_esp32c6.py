#!/usr/bin/env python3
"""Run an ojay-riscv image on an ESP32-C6 through its built-in USB-JTAG.

The image (linked with runtime/boards/esp32c6.ld) is loaded into SRAM with
Espressif's OpenOCD (openocd-esp32), started at its entry point, and its
semihosting console output is forwarded to stdout. Nothing is written to
flash; the board's own firmware runs again after the next reset.

OpenOCD is taken from --openocd, $OJAY_ESP_OPENOCD, or the newest
~/.espressif/tools/openocd-esp32 installation.

Exit status: the program's exit status, or
  124  the program did not finish within --timeout seconds
  125  could not talk to the board (probe missing, OpenOCD error, ...)
  126  the core stopped without the program exiting
"""
import argparse
import glob
import os
import struct
import subprocess
import sys
import tempfile

SRAM_START, SRAM_END = 0x40800000, 0x40880000


def find_openocd():
    cand = os.environ.get("OJAY_ESP_OPENOCD")
    if cand:
        return cand
    found = sorted(glob.glob(os.path.expanduser("~/.espressif/tools/openocd-esp32/*/openocd-esp32/bin/openocd")))
    return found[-1] if found else None


def elf_entry_and_range(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        raise ValueError("not a 32-bit little-endian ELF file")
    entry, = struct.unpack_from("<I", data, 24)
    e_phoff, = struct.unpack_from("<I", data, 28)
    e_phentsize, e_phnum = struct.unpack_from("<HH", data, 42)
    addrs = []
    for i in range(e_phnum):
        p_type, _, _, p_paddr, p_filesz = struct.unpack_from("<IIIII", data, e_phoff + i * e_phentsize)
        if p_type == 1 and p_filesz:
            addrs.append((p_paddr, p_paddr + p_filesz))
    if not addrs:
        raise ValueError("no loadable segments")
    return entry, min(a for a, _ in addrs), max(b for _, b in addrs)



def semihosting_cmdline(elf, args):
    """The Tcl words of `arm semihosting_cmdline` for the program's arguments:
    OpenOCD joins them with spaces, and the runtime splits the line at spaces
    outside "double quotes", so arguments with spaces (or empty ones) are quoted."""
    words = [os.path.basename(elf)] + list(args)
    out = []
    for w in words:
        if w == "" or any(c in w for c in " \t"):
            w = '"' + w + '"'
        out.append("{" + w + "}")
    return " ".join(out)

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf", nargs="?")
    ap.add_argument("--probe", action="store_true", help="only check that the board responds (exit 0 if so)")
    ap.add_argument("--timeout", type=float, default=30.0, help="seconds (default 30)")
    ap.add_argument("--openocd", default=None)
    ap.add_argument("--usb-location", default=os.environ.get("OJAY_ESP_USB_LOCATION"),
                    help="USB location (bus-port[.port...], e.g. 5-4) of the board to use when several "
                         "are connected; default $OJAY_ESP_USB_LOCATION or the first one found")
    ap.add_argument("-v", "--verbose", action="store_true", help="show OpenOCD's log")
    ap.add_argument("--arg", action="append", default=[],
                    help="an argument for the program (repeatable; semihosting SYS_GET_CMDLINE)")
    args = ap.parse_args()

    openocd = args.openocd or find_openocd()
    if not openocd:
        print("run_esp32c6: openocd-esp32 not found (set OJAY_ESP_OPENOCD)", file=sys.stderr)
        return 125
    scripts = os.path.join(os.path.dirname(os.path.dirname(openocd)), "share", "openocd", "scripts")
    select = ["-c", f"adapter usb location {args.usb_location}"] if args.usb_location else []
    if args.probe:
        with tempfile.TemporaryDirectory(prefix="ojay-esp-") as cwd:
            r = subprocess.run([openocd, "-s", scripts] + select +
                               ["-f", "board/esp32c6-builtin.cfg",
                                "-c", "gdb port disabled; telnet port disabled; tcl port disabled",
                                "-c", "init; shutdown"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, cwd=cwd, timeout=30)
        ok = "Examination succeed" in r.stderr.decode(errors="replace")
        return 0 if ok else 125
    if not args.elf:
        ap.error("an ELF file is required")
    try:
        entry, lo, hi = elf_entry_and_range(args.elf)
    except (OSError, ValueError) as e:
        print(f"run_esp32c6: {args.elf}: {e}", file=sys.stderr)
        return 125
    if not (SRAM_START <= lo and hi <= SRAM_END):
        print(f"run_esp32c6: {args.elf} is not an SRAM image ({lo:#x}-{hi:#x}); link it with esp32c6.ld",
              file=sys.stderr)
        return 125

    ms = int(args.timeout * 1000)
    elf = os.path.abspath(args.elf).replace("\\", "/")
    script = (
        "init; reset halt; "
        f"load_image {{{elf}}}; reg pc {entry:#x}; "
        "arm semihosting enable; "
        f"arm semihosting_cmdline {semihosting_cmdline(args.elf, args.arg)}; resume; "
        # A semihosting exit makes OpenOCD exit with the program's status, so
        # reaching the lines below means something went wrong.
        f"if {{[catch {{wait_halt {ms}}}]}} {{ echo OJAY-RUN-TIMEOUT; halt; shutdown error }}; "
        "echo OJAY-RUN-HALTED; shutdown error"
    )
    # Run in an empty directory: semihosting file operations are relative to it.
    with tempfile.TemporaryDirectory(prefix="ojay-esp-") as cwd:
        cmd = [openocd, "-s", scripts] + select + [
            "-f", "board/esp32c6-builtin.cfg",
            "-c", "gdb port disabled; telnet port disabled; tcl port disabled",
            "-c", script]
        proc = subprocess.run(cmd, stderr=subprocess.PIPE, cwd=cwd, timeout=args.timeout + 30)
    log = proc.stderr.decode(errors="replace")
    if args.verbose:
        sys.stderr.write(log)
    if "OJAY-RUN-TIMEOUT" in log:
        print(f"run_esp32c6: timed out after {args.timeout:g}s", file=sys.stderr)
        return 124
    if "OJAY-RUN-HALTED" in log:
        print("run_esp32c6: the core halted without the program exiting", file=sys.stderr)
        return 126
    if "Examination succeed" not in log:
        if not args.verbose:
            sys.stderr.write(log)
        print("run_esp32c6: could not start the program on the board", file=sys.stderr)
        return 125
    return proc.returncode


if __name__ == "__main__":
    sys.exit(main())
