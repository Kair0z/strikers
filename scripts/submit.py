import os
import subprocess

os.chdir("..")
subprocess.run([
    "git add -A && git commit -m \"linux commit\""
], check=True)