import subprocess

Import("env")

def _git(*args):
    try:
        ret = subprocess.run(["git"] + list(args), stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL, text=True, cwd=env.subst("$PROJECT_DIR"))
        return ret.stdout.strip() if ret.returncode == 0 else ""
    except Exception:
        return ""

def get_firmware_specifier_build_flags():
    # AUTO_VERSION: tag-based description, falls back to hash when no tag reachable
    build_version = _git("describe", "--tags", "--always", "--dirty").replace('Release', '') or "unknown"
    # FW_GIT: short commit hash + "-dirty" if the working tree has uncommitted changes
    short = _git("rev-parse", "--short", "HEAD") or "nogit"
    if short != "nogit" and _git("status", "--porcelain", "--untracked-files=no"):
        short += "-dirty"
    print("Firmware Revision: " + build_version + "  git: " + short)
    return ["-D AUTO_VERSION=\\\"" + build_version + "\\\"",
            "-D FW_GIT=\\\"" + short + "\\\""]

env.Append(BUILD_FLAGS=get_firmware_specifier_build_flags())
