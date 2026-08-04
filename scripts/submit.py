import os
import subprocess

os.chdir("..")

# add all
subprocess.run([
    "git",
    "add",
    "-A"
], check=True)

# commit
subprocess.run([
    "git",
    "commit",
    "-m",
    "\"linux commit\""
], check=True)

# push
subprocess.run([
    "git",
    "push"
], check=True)