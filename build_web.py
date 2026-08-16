Import("env")

import gzip
import os
import platform
import shutil
import subprocess

WEBUI_DIR = os.path.join(env.subst("$PROJECT_DIR"), "webui")
DIST_DIR  = os.path.join(WEBUI_DIR, "dist")
DATA_DIR  = os.path.join(env.subst("$PROJECT_DIR"), "data")

NPM = "npm.cmd" if platform.system() == "Windows" else "npm"

GZIP_EXTENSIONS = {".htm", ".html", ".css", ".js"}


def _run(cmd, cwd):
    result = subprocess.run(cmd, cwd=cwd, shell=False, capture_output=True)
    if result.returncode != 0:
        print(result.stderr.decode("utf-8", errors="replace"))
        raise SystemExit(f"build_web: command failed: {' '.join(cmd)}")


def _gzip_file(path):
    gz_path = path + ".gz"
    with open(path, "rb") as f_in, gzip.open(gz_path, "wb", compresslevel=9) as f_out:
        shutil.copyfileobj(f_in, f_out)


def _copy_and_compress(src_dir, dst_dir):
    """Copy src_dir contents to dst_dir, gzip-compressing web assets."""
    os.makedirs(dst_dir, exist_ok=True)
    for item in os.listdir(src_dir):
        src = os.path.join(src_dir, item)
        dst = os.path.join(dst_dir, item)
        if os.path.isdir(src):
            _copy_and_compress(src, dst)
        else:
            shutil.copy2(src, dst)
            if os.path.splitext(item)[1].lower() in GZIP_EXTENSIONS:
                _gzip_file(dst)


def build_web_ui(source, target, env):
    if not os.path.isdir(WEBUI_DIR):
        print("build_web: webui/ not found, skipping")
        return

    if not os.path.isdir(os.path.join(WEBUI_DIR, "node_modules")):
        print("build_web: Installing npm dependencies...")
        _run([NPM, "install"], cwd=WEBUI_DIR)

    print("build_web: Building web UI...")
    _run([NPM, "run", "build"], cwd=WEBUI_DIR)

    print("build_web: Copying dist/ -> data/ ...")

    # Clean old web assets from data/
    for item in ("assets", "index.htm", "index.htm.gz"):
        target_path = os.path.join(DATA_DIR, item)
        if os.path.isdir(target_path):
            shutil.rmtree(target_path)
        elif os.path.isfile(target_path):
            os.remove(target_path)

    # Copy assets/ with gzip
    assets_src = os.path.join(DIST_DIR, "assets")
    if os.path.exists(assets_src):
        _copy_and_compress(assets_src, os.path.join(DATA_DIR, "assets"))

    # index.html → index.htm  (+ .gz)
    index_src = os.path.join(DIST_DIR, "index.html")
    if os.path.exists(index_src):
        index_dst = os.path.join(DATA_DIR, "index.htm")
        shutil.copy2(index_src, index_dst)
        _gzip_file(index_dst)

    # Other root-level files (favicon, etc.)
    for item in os.listdir(DIST_DIR):
        if item in ("index.html", "assets"):
            continue
        src = os.path.join(DIST_DIR, item)
        if os.path.isfile(src):
            dst = os.path.join(DATA_DIR, item)
            shutil.copy2(src, dst)
            if os.path.splitext(item)[1].lower() in GZIP_EXTENSIONS:
                _gzip_file(dst)

    print("build_web: Done")


littlefs_bin = env.subst("$BUILD_DIR/littlefs.bin")
env.AddPreAction(littlefs_bin, build_web_ui)
