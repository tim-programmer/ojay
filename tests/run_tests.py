#!/usr/bin/env python3
"""Test runner for the ojay compilers.

tests/run/*.jai     compiled, linked and executed; stdout must equal the .out
                    file. An optional `// EXIT: n` line gives the exit status.
                    `// COMPILE_OUTPUT: text` lines give what the compiler
                    itself must print on stdout (compile-time `print` in
                    `#run`), one line each, for every target; without them
                    the compiler must print nothing.
tests/errors/*.jai  must fail to compile; the first line `// ERROR: text`
                    gives a substring expected in the compiler's stderr (more
                    `// ERROR:` lines right after it give more substrings).
tests/regs2jai/     tools/regs2jai.py: each X_svd_test.jai / X_h_test.jai is
                    run like a run test, with the conversion of X.svd / X.h
                    next to it.

The compilers only produce object files. This script links them against the
runtime (the same commands as in README.md) and runs the result. ARM32
(Cortex-M33) images are executed, in order of preference, on:
  board        an FRDM-MCXN947 attached through its MCU-Link (RAM images,
               loaded with OpenOCD by tools/run_mcxn947.py)
  qemu         qemu-system-arm -M mps2-an505
otherwise they are only linked and checked. Override with
--arm=board|board-flash|qemu|none (board-flash programs each test into flash).

RISC-V (ESP32-C6) images are run on an attached ESP32-C6 through its built-in
USB-JTAG (RAM images, loaded with openocd-esp32 by tools/run_esp32c6.py) when
one is present and the ESP toolchain is installed; --riscv=board|none overrides.
"""
import argparse
import glob
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(ROOT)

ARM_GCC_FLAGS = ["-mcpu=cortex-m33", "-mthumb", "-mfloat-abi=hard", "-mfpu=fpv5-sp-d16"]
RISCV_GCC_FLAGS = ["-march=rv32imac_zicsr_zifencei", "-mabi=ilp32"]


def find_riscv_gcc():
    gcc = shutil.which("riscv32-esp-elf-gcc")
    if gcc:
        return gcc
    found = sorted(glob.glob(os.path.expanduser(
        "~/.espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin/riscv32-esp-elf-gcc")))
    return found[-1] if found else None


def esp_openocd_present():
    return bool(os.environ.get("OJAY_ESP_OPENOCD") or glob.glob(os.path.expanduser(
        "~/.espressif/tools/openocd-esp32/*/openocd-esp32/bin/openocd")))


def usb_present(vid_pid):
    for dev in glob.glob("/sys/bus/usb/devices/*/idVendor"):
        try:
            vid = open(dev).read().strip()
            pid = open(os.path.join(os.path.dirname(dev), "idProduct")).read().strip()
        except OSError:
            continue
        if (vid, pid) == vid_pid:
            return True
    return False


def mcu_link_present():
    return usb_present(("1fc9", "0143"))


def board_responds(runner):
    """Asks a board runner script whether its board answers (a USB device can
    be listed but unusable, e.g. a stale entry after re-plugging)."""
    try:
        return subprocess.run([sys.executable, os.path.join(REPO, "tools", runner), "--probe"],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60).returncode == 0
    except subprocess.TimeoutExpired:
        return False


class Config:
    def __init__(self, label, compile_cmd, link, runner):
        self.label = label
        self.compile_cmd = compile_cmd  # compiler + flags
        self.link = link                # function(obj, exe) -> argv
        self.runner = runner            # argv prefix, or None for a native run; False = do not run


def make_configs(build_dir, arm_mode, riscv_mode):
    lib = os.path.join(build_dir, "lib", "ojay")
    x64 = os.path.join(build_dir, "bin", "ojay-x86_64")
    arm = os.path.join(build_dir, "bin", "ojay-arm32")

    def link_hosted(obj, exe):
        return ["cc", obj, os.path.join(lib, "libojayrt_x86_64_hosted.a"), "-lm", "-o", exe]

    def link_static(obj, exe):
        return ["ld.lld", "-static", "--gc-sections", obj, os.path.join(lib, "libojayrt_x86_64_freestanding.a"),
                "-o", exe]

    def link_arm(script):
        def link(obj, exe):
            boards = os.path.join(lib, "boards")
            return (["arm-none-eabi-gcc"] + ARM_GCC_FLAGS +
                    ["-nostdlib", "-nostartfiles", "-Wl,--gc-sections", "-Wl,--no-warn-rwx-segments",
                     "-Wl,--no-warn-execstack", "-L" + boards, "-T", os.path.join(boards, script),
                     obj, os.path.join(lib, "libojayrt_arm32_baremetal.a"),
                     "-Wl,--start-group", "-lm", "-lc_nano", "-lgcc", "-Wl,--end-group", "-o", exe])
        return link

    if arm_mode == "auto":
        if mcu_link_present() and shutil.which("openocd") and board_responds("run_mcxn947.py"):
            arm_mode = "board"
        elif shutil.which("qemu-system-arm"):
            arm_mode = "qemu"
        else:
            arm_mode = "none"
    run_board = os.path.join(REPO, "tools", "run_mcxn947.py")
    if arm_mode == "board":
        arm_link, arm_run, where = link_arm("mcxn947-ram.ld"), [sys.executable, run_board, "--timeout", "20"], \
            "MCX N947 board (RAM)"
    elif arm_mode == "board-flash":
        arm_link, arm_run, where = link_arm("mcxn947-flash.ld"), \
            [sys.executable, run_board, "--flash", "--timeout", "20"], "MCX N947 board (flash)"
    elif arm_mode == "qemu":
        arm_link, arm_run, where = link_arm("mps2-an505.ld"), \
            [shutil.which("qemu-system-arm"), "-M", "mps2-an505", "-nographic", "-monitor", "none", "-serial",
             "none", "-semihosting-config", "enable=on,target=native", "-kernel"], "QEMU mps2-an505"
    else:
        arm_link, arm_run, where = link_arm("mcxn947-flash.ld"), False, "linked only, not executed"

    configs_rv = []
    rv_where = "toolchain (riscv32-esp-elf-gcc) not found; skipped"
    rv_gcc = find_riscv_gcc()
    if rv_gcc and riscv_mode != "skip":
        rv = os.path.join(build_dir, "bin", "ojay-riscv")

        def link_rv(obj, exe):
            boards = os.path.join(lib, "boards")
            return ([rv_gcc] + RISCV_GCC_FLAGS +
                    ["-nostdlib", "-nostartfiles", "-Wl,--gc-sections", "-L" + boards,
                     "-T", os.path.join(boards, "esp32c6.ld"), obj,
                     os.path.join(lib, "libojayrt_riscv32_esp32c6.a"),
                     "-Wl,--start-group", "-lm", "-lc_nano", "-lgcc", "-Wl,--end-group", "-o", exe])

        if riscv_mode == "auto":
            riscv_mode = "board" if (usb_present(("303a", "1001")) and esp_openocd_present()
                                     and board_responds("run_esp32c6.py")) else "none"
        if riscv_mode == "board":
            rv_run = [sys.executable, os.path.join(REPO, "tools", "run_esp32c6.py"), "--timeout", "20"]
            rv_where = "ESP32-C6 board (RAM)"
        else:
            rv_run, rv_where = False, "linked only, not executed"
        configs_rv = [Config("riscv -O0", [rv], link_rv, rv_run), Config("riscv -O2", [rv, "-O2"], link_rv, rv_run)]

    return [
        Config("x86_64 -O0", [x64], link_hosted, None),
        Config("x86_64 -O2", [x64, "-O2"], link_hosted, None),
        Config("x86_64 static", [x64, "-O1", "--no-pic"], link_static, None),
        Config("arm32 -O0", [arm], arm_link, arm_run),
        Config("arm32 -O2", [arm, "-O2"], arm_link, arm_run),
    ] + configs_rv, where, rv_where


def run(cmd, timeout=120):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", default=os.path.join(REPO, "build"))
    ap.add_argument("--filter", default="")
    ap.add_argument("--arm", choices=["auto", "board", "board-flash", "qemu", "none"], default="auto",
                    help="where to execute ARM32 tests (default: auto)")
    ap.add_argument("--riscv", choices=["auto", "board", "none", "skip"], default="auto",
                    help="where to execute RISC-V tests (default: auto)")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    configs, where, rv_where = make_configs(args.build_dir, args.arm, args.riscv)
    print(f"ARM32 tests: {where}")
    print(f"RISC-V tests: {rv_where}")
    failures = []
    passed = 0
    not_run = 0
    tmp = tempfile.mkdtemp(prefix="ojay-tests-")

    def check_program(name, src, want_out, want_code, want_compile_out=""):
        nonlocal passed, not_run
        for cfg in configs:
            base = os.path.join(tmp, name[:-4] + "_" + re.sub(r"\W+", "_", cfg.label))
            obj, exe = base + ".o", base + ".exe"
            r = run(cfg.compile_cmd + [src, "-o", obj])
            if r.returncode != 0:
                failures.append((name, cfg.label, "compile failed:\n" + r.stderr.decode()))
                continue
            if r.stdout.decode() != want_compile_out:
                failures.append((name, cfg.label, f"compiler output differs\n--- got\n{r.stdout.decode()}"
                                                  f"--- want\n{want_compile_out}"))
                continue
            r = run(cfg.link(obj, exe))
            if r.returncode != 0:
                failures.append((name, cfg.label, "link failed:\n" + r.stderr.decode()))
                continue
            if cfg.runner is False:
                not_run += 1
                passed += 1
                continue
            r = run((cfg.runner or []) + [exe])
            out = r.stdout.decode()
            if out != want_out or r.returncode != want_code:
                msg = f"exit {r.returncode} (want {want_code})\n--- got\n{out}--- want\n{want_out}"
                if r.stderr:
                    msg += "--- stderr\n" + r.stderr.decode()
                failures.append((name, cfg.label, msg))
                continue
            passed += 1
            if args.verbose:
                print(f"ok   {name} [{cfg.label}]")

    run_dir = os.path.join(ROOT, "run")
    for name in sorted(os.listdir(run_dir)):
        if not name.endswith(".jai") or args.filter not in name:
            continue
        src = os.path.join(run_dir, name)
        with open(src) as f:
            text = f.read()
        m = re.search(r"^// EXIT: (\d+)", text, re.M)
        compile_out = "".join(line + "\n" for line in re.findall(r"^// COMPILE_OUTPUT: ?(.*)$", text, re.M))
        with open(src[:-4] + ".out") as f:
            want_out = f.read()
        check_program(name, src, want_out, int(m.group(1)) if m else 0, compile_out)

    # tools/regs2jai.py: X_svd_test.jai #loads the conversion of X.svd (X_h_test.jai: of X.h)
    # as X_svd.jai. An optional `// REGS2JAI: args` line gives extra converter arguments.
    regs_dir = os.path.join(ROOT, "regs2jai")
    for name in sorted(os.listdir(regs_dir)):
        m = re.fullmatch(r"(\w+)_(svd|h)_test\.jai", name)
        if not m or args.filter not in name:
            continue
        with open(os.path.join(regs_dir, name)) as f:
            text = f.read()
        extra = re.search(r"^// REGS2JAI:(.*)$", text, re.M)
        generated = os.path.join(tmp, f"{m.group(1)}_{m.group(2)}.jai")
        r = subprocess.run([sys.executable, os.path.join(REPO, "tools", "regs2jai.py"),
                            f"{m.group(1)}.{m.group(2)}", "-o", generated] +
                           (shlex.split(extra.group(1)) if extra else []),
                           cwd=regs_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
        if r.returncode != 0 or r.stderr:
            failures.append((name, "regs2jai", "conversion failed or warned:\n" + r.stderr.decode()))
            continue
        src = os.path.join(tmp, name)
        shutil.copy(os.path.join(regs_dir, name), src)
        with open(os.path.join(regs_dir, name[:-4] + ".out")) as f:
            want_out = f.read()
        check_program(name, src, want_out, 0)

    err_dir = os.path.join(ROOT, "errors")
    x64 = os.path.join(args.build_dir, "bin", "ojay-x86_64")
    for name in sorted(os.listdir(err_dir)):
        if not name.endswith(".jai") or args.filter not in name:
            continue
        src = os.path.join(err_dir, name)
        wants = []
        with open(src) as f:
            for line in f:
                m = re.match(r"// ERROR: (.*)", line)
                if not m:
                    break
                wants.append(m.group(1).strip())
        if not wants:
            failures.append((name, "error", "missing '// ERROR:' header"))
            continue
        r = run([x64, "-I", os.path.join(ROOT, "run", "modules"), src, "-o", os.path.join(tmp, "err.o")])
        err = r.stderr.decode()
        missing = [w for w in wants if w not in err]
        if r.returncode == 0:
            failures.append((name, "error", "compiled successfully but should have failed"))
        elif missing:
            failures.append((name, "error", f"stderr does not contain {missing[0]!r}:\n{err}"))
        else:
            passed += 1
            if args.verbose:
                print(f"ok   {name} [error]")

    shutil.rmtree(tmp, ignore_errors=True)
    for name, label, msg in failures:
        print(f"FAIL {name} [{label}]\n{msg}\n")
    note = f" ({not_run} embedded programs were linked but not executed)" if not_run else ""
    print(f"{passed} passed, {len(failures)} failed{note}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
