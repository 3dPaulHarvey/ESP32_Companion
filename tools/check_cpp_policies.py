"""Compile the existing C++ policy assertions and run local capture/state tests.

Requires a native GCC-compatible C++20 compiler (g++ or clang++). No device,
credentials, network, or ESP-IDF environment is needed. Build outputs use the
system temporary directory and are removed automatically.
"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def find_compiler(requested):
    if requested:
        found = shutil.which(requested)
        if found:
            return Path(found)
        raise RuntimeError(f"C++ compiler not found: {requested}")
    for name in ("g++", "clang++"):
        found = shutil.which(name)
        if found:
            return Path(found)
    if os.name == "nt":
        # Common MSYS2 locations, including this development workstation.
        for environment in ("ucrt64", "mingw64", "clang64"):
            folder = Path("C:/msys64") / environment / "bin"
            for name in ("g++.exe", "clang++.exe"):
                candidate = folder / name
                if candidate.is_file():
                    return candidate
    raise RuntimeError("Install native g++/clang++ or pass --compiler PATH.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", help="Path to native g++ or clang++")
    args = parser.parse_args()
    compiler = find_compiler(args.compiler).resolve()
    tests = Path(__file__).resolve().parent.parent / "Source/esp32-companion/tests"
    environment = os.environ.copy()
    # MinGW executables need their compiler's runtime DLLs on Windows.
    environment["PATH"] = str(compiler.parent) + os.pathsep + environment.get("PATH", "")
    flags = [str(compiler), "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O2"]
    policy_tests = sorted(tests.glob("companion_*_policy_test.cpp"))
    if not policy_tests:
        raise RuntimeError(f"No policy assertions found in {tests}")
    print(f"Compiler: {compiler}", flush=True)
    for source in policy_tests:
        subprocess.run(flags + ["-fsyntax-only", str(source)], check=True, env=environment)
        print(f"PASS {source.name} (compile-time assertions)", flush=True)
    runtime_tests = ("local_companion_cycle_test.cpp", "local_pcm_capture_test.cpp")
    with tempfile.TemporaryDirectory(prefix="esp32-companion-cpp-") as temporary:
        for name in runtime_tests:
            executable = Path(temporary) / (Path(name).stem + (".exe" if os.name == "nt" else ""))
            subprocess.run(flags + [str(tests / name), "-o", str(executable)], check=True, env=environment)
            subprocess.run([str(executable)], check=True, env=environment)
            print(f"PASS {name} (runtime assertions)", flush=True)
    print(f"PASS {len(policy_tests)} policy suites and {len(runtime_tests)} local runtime suites", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
