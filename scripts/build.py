import os
import subprocess

os.chdir("../generated")
subprocess.run([
    "ninja",
    "debug_linux"
], check=True)