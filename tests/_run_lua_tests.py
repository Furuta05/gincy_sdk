import os
import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[1]
os.chdir(root)

def run_lupa(script):
    try:
        from lupa.lua51 import LuaRuntime
    except Exception:
        from lupa import LuaRuntime
    lua = LuaRuntime(unpack_returned_tuples=True)
    root_posix = str(root).replace("\\", "/")
    lua.execute(f'arg = {{"{root_posix}"}}')
    with open(script, encoding="utf-8") as handle:
        source = handle.read()
    lua.execute(source)

scripts = [
    root / "tests" / "console_ascii.lua",
    root / "tests" / "bootstrap.lua",
    root / "tests" / "gvm_runtime.lua",
    root / "tests" / "runtime_v3.lua",
    root / "tests" / "disk_protection.lua",
]
failed = 0
for script in scripts:
    print("==>", script.name)
    try:
        run_lupa(script)
    except Exception as error:
        failed += 1
        print("FAIL", script.name, error)
sys.exit(failed)
