"""Verify the Morse Messenger MIPS artifact without executing it.

Checks performed:
  * the file is an ELF32 little-endian MIPS executable;
  * the entry point is 0x80200000 and the only loadable segment is in RAM;
  * the O32 ABI, soft-float and MIPS32r2 flags are present;
  * all application symbols required by the design are defined;
  * no undefined symbols remain and no host-test symbol leaked into the image.

The result is written to Build/verification.json.
"""
import hashlib
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "Build")
TOOLS = r"D:\LoongIDE\mips-2015.05\bin"
READELF = os.path.join(TOOLS, "mips-sde-elf-readelf.exe")
NM = os.path.join(TOOLS, "mips-sde-elf-nm.exe")
ELF = os.path.join(BIN, "MorseMessenger.exe")

REQUIRED_SYMBOLS = [
    "morse_encode", "morse_service", "parse_command", "cmd_msg",
    "cmd_speed", "cmd_stop", "cmd_reset", "poll_uart", "main",
]
FORBIDDEN_SYMBOLS = ["host_uart_send", "host_uart_read", "host_now"]


def run(*args):
    return subprocess.run(args, capture_output=True, text=True, check=False).stdout


def main():
    if not os.path.exists(ELF):
        print("missing", ELF)
        return 1
    with open(ELF, "rb") as f:
        data = f.read()
    sha = hashlib.sha256(data).hexdigest()

    header = run(READELF, "-h", ELF)
    loads = run(READELF, "-l", ELF)
    nm = run(NM, ELF)

    info = {
        "artifact": "Build/MorseMessenger.exe",
        "bytes": len(data),
        "sha256": sha,
        "format": "ELF32 little-endian MIPS" if "ELF32" in header and "2's complement, little endian" in header else "UNEXPECTED",
        "type": re.search(r"Type:\s+(.+)", header).group(1).strip() if re.search(r"Type:\s+(.+)", header) else "",
        "machine": re.search(r"Machine:\s+(.+)", header).group(1).strip() if re.search(r"Machine:\s+(.+)", header) else "",
        "entry": re.search(r"Entry point address:\s+(\S+)", header).group(1) if re.search(r"Entry point address:\s+(\S+)", header) else "",
        "flags": re.search(r"Flags:\s+(\S+)", header).group(1) if re.search(r"Flags:\s+(\S+)", header) else "",
        "abi": "o32" if "o32" in header else "UNEXPECTED",
    }
    m = re.search(r"Flags:\s+0x[0-9a-f]+,\s*(.+)", header)
    if m:
        info["flag_text"] = m.group(1).strip()

    seg = []
    for line in loads.splitlines():
        line = line.strip()
        if line.startswith("LOAD"):
            parts = line.split()
            seg.append({"type": parts[0], "virt": parts[2], "filesz": parts[4], "memsz": parts[5]})
    info["load_segments"] = seg
    info["only_ram_segment"] = all(s["virt"].startswith("0x80") for s in seg) and len(seg) >= 1

    syms = {}
    undefined = []
    for line in nm.splitlines():
        parts = line.split()
        if len(parts) == 3:
            addr, kind, name = parts
            if kind == "U":
                undefined.append(name)
            else:
                syms[name] = {"address": addr, "type": kind}
    info["undefined_symbols"] = undefined
    info["application_symbols"] = {s: syms.get(s) for s in REQUIRED_SYMBOLS}
    info["host_symbols_in_image"] = [s for s in FORBIDDEN_SYMBOLS if s in syms or s in undefined]

    checks = {
        "elf_format": info["format"].startswith("ELF32 little-endian MIPS"),
        "executable": info["type"].startswith("EXEC"),
        "entry_0x80200000": info["entry"] == "0x80200000",
        "o32_abi": info["abi"] == "o32",
        "mips32r2": "mips32r2" in info.get("flag_text", ""),
        "ram_only_segment": info["only_ram_segment"],
        "no_undefined_symbols": len(undefined) == 0,
        "application_linked": all(info["application_symbols"].values()),
        "no_host_test_code": len(info["host_symbols_in_image"]) == 0,
    }
    info["checks"] = checks
    info["result"] = "PASS" if all(checks.values()) else "FAIL"

    os.makedirs(BIN, exist_ok=True)
    with open(os.path.join(BIN, "verification.json"), "w", encoding="utf-8") as f:
        json.dump(info, f, indent=2)
    for k, v in checks.items():
        print(("PASS " if v else "FAIL ") + k)
    print("RESULT:", info["result"])
    print("ELF bytes:", len(data), "sha256:", sha)
    return 0 if info["result"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
