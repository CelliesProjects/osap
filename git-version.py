import subprocess

Import("env")

def get_git_version():
    try:
        return subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            stderr=subprocess.DEVNULL
        ).decode().strip()
    except Exception:
        return "unknown"

git_version = get_git_version()

env.Append(CPPDEFINES=[("GIT_VERSION", f'\\"{git_version}\\"')])

print(f"Open Sores Audio Player build: {git_version}")
