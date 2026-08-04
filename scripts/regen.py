import os
import subprocess

os.chdir("../premake")
subprocess.run([
    "premake5",
    # "vs2022",
    "ninja",
    "--file=premake5.lua"
], check=True)