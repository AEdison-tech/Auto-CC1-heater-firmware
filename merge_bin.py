# originally from here: https://github.com/platformio/platform-espressif32/issues/1078#issuecomment-1636793463
Import("env")
import os
from os.path import join

APP_BIN = "$BUILD_DIR/${PROGNAME}.bin"
LITTLEFS_BIN = "$BUILD_DIR/littlefs.bin"
MERGED_BIN = "$BUILD_DIR/${PROGNAME}_merged.bin"
BOARD_CONFIG = env.BoardConfig()


def get_littlefs_partition_address(env):
    """Get the LittleFS partition address from the partition table"""
    try:
        partitions_csv = None
        
        if hasattr(env, 'GetProjectOption'):
            partitions_csv = env.GetProjectOption("board_build.partitions", None)
        
        if not partitions_csv:
            # Get the board's partition table from the framework
            board_name = env.subst("$BOARD")
            framework_dir = env.subst("$PROJECT_PACKAGES_DIR/framework-arduinoespressif32")
            
            # Try to find the default partition table for the board
            variants_dir = os.path.join(framework_dir, "variants", board_name)
            if os.path.exists(variants_dir):
                for file in os.listdir(variants_dir):
                    if file.endswith(".csv"):
                        partitions_csv = os.path.join(variants_dir, file)
                        break
            
            # Fallback to default partition table
            if not partitions_csv:
                # Check flash size to determine which default partition table to use
                flash_size = BOARD_CONFIG.get("upload.flash_size", "4MB")
                print(f"Board flash size: {flash_size}")
                
                # TODO: make this less dumb
                if flash_size == "8MB":
                    default_csv = "default_8MB.csv"
                else:
                    default_csv = "default.csv"
                
                print(f"Getting partitions from default {os.path.join(framework_dir, 'tools', 'partitions', default_csv)}")
                partitions_csv = os.path.join(framework_dir, "tools", "partitions", default_csv)
        
        # If we still don't have a partition table file, try to get it from the build process
        if not partitions_csv or not os.path.exists(partitions_csv):
            # Try to find the generated partition table
            build_dir = env.subst("$BUILD_DIR")
            generated_partitions = os.path.join(build_dir, "partitions.csv")
            if os.path.exists(generated_partitions):
                partitions_csv = generated_partitions
            else:
                # Fall back to the default known address
                print("Warning: Could not find partition table, using default LittleFS address 0x3d0000")
                return "0x3d0000"
        
        # Read and parse the partition table
        with open(partitions_csv, 'r') as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith('#'):
                    parts = [p.strip() for p in line.split(',')]
                    if len(parts) >= 5:
                        name, type_field, subtype, offset, size = parts[:5]
                        # Look for spiffs or littlefs partition
                        if ('spiffs' in name.lower() or 'littlefs' in name.lower() or 
                            type_field.lower() == 'data' and subtype.lower() in ['spiffs', 'littlefs']):
                            # Clean up the offset (remove quotes and whitespace)
                            offset = offset.strip('"\'')
                            if not offset.startswith('0x'):
                                offset = '0x' + offset
                            print(f"Found LittleFS partition '{name}' at address {offset}")
                            return offset
        
        print("Warning: LittleFS partition not found in partition table, using default address 0x3d0000")
        return "0x3d0000"
        
    except Exception as e:
        print(f"Error reading partition table: {e}")
        print("Using default LittleFS address 0x3d0000")
        return "0x3d0000"


def build_littlefs(source, target, env):
    """Build LittleFS filesystem (always runs to pick up webui changes)"""
    import sys
    # Remove cached binary so SCons is forced to rebuild it,
    # which guarantees the build_web_ui pre-action fires and npm is run.
    littlefs_bin_path = env.subst("$BUILD_DIR/littlefs.bin")
    if os.path.exists(littlefs_bin_path):
        os.remove(littlefs_bin_path)
    print("Building LittleFS filesystem...")
    ret = env.Execute("$PYTHONEXE -m platformio run -e $PIOENV --target buildfs --disable-auto-clean")
    if ret != 0:
        print("build_littlefs: buildfs failed", file=sys.stderr)
        env.Exit(1)


def merge_bin(source, target, env):
    import subprocess

    # Get the dynamic LittleFS partition address
    littlefs_address = get_littlefs_partition_address(env)

    # The list contains all extra images (bootloader, partitions, eboot) and
    # the final application binary
    flash_images = env.Flatten(env.get("FLASH_EXTRA_IMAGES", [])) + ["$ESP32_APP_OFFSET", APP_BIN, littlefs_address, LITTLEFS_BIN]

    # Expand all SCons variables into real paths before passing to subprocess.
    # Using a list avoids shell quoting issues with paths that contain spaces.
    cmd = [
        env.subst("$PYTHONEXE"),
        env.subst("$OBJCOPY"),
        "--chip",
        BOARD_CONFIG.get("build.mcu", "esp32"),
        "merge_bin",
        "--fill-flash-size",
        BOARD_CONFIG.get("upload.flash_size", "4MB"),
        "-o",
        env.subst(MERGED_BIN),
    ] + [env.subst(x) for x in flash_images]

    result = subprocess.run(cmd)
    if result.returncode != 0:
        import sys
        print("merge_bin failed", file=sys.stderr)
        env.Exit(1)

# Build LittleFS before main firmware (web build happens inside as pre-action for littlefs.bin)
env.AddPreAction(APP_BIN, build_littlefs)

# Merge after successful firmware build
env.AddPostAction(APP_BIN, merge_bin)

# Always rebuild LittleFS (+ webui) and re-merge before upload,
# even when firmware is cached and APP_BIN pre/post-actions didn't fire.
def pre_upload(source, target, env):
    build_littlefs(source, target, env)
    merge_bin(source, target, env)

env.AddPreAction("upload", pre_upload)

# Patch the upload command to flash the merged binary at address 0x0.
# Expand MERGED_BIN to a real path now so the shell command handles spaces correctly.
merged_bin_path = env.subst(MERGED_BIN)
env.Replace(
    UPLOADCMD=f'"$PYTHONEXE" "$UPLOADER" write_flash 0x0 "{merged_bin_path}"',
)

