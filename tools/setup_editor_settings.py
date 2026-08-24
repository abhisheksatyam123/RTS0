#!/usr/bin/env python3
import os
import sys
from pathlib import Path

def main():
    config_dir = Path.home() / ".config" / "godot"
    config_dir.mkdir(parents=True, exist_ok=True)

    android_home = os.environ.get("ANDROID_HOME", "/usr/local/lib/android/sdk")
    java_home = os.environ.get("JAVA_HOME", "")
    debug_keystore = str((Path.home() / ".android" / "debug.keystore").resolve())

    print(f"[setup_editor_settings] ANDROID_HOME={android_home}")
    print(f"[setup_editor_settings] JAVA_HOME={java_home}")
    print(f"[setup_editor_settings] debug_keystore={debug_keystore}")

    # Update export_presets.cfg keystore fields dynamically with absolute paths
    preset_cfg = Path("export_presets.cfg")
    if preset_cfg.exists():
        text = preset_cfg.read_text()
        text = text.replace('keystore/debug=""', f'keystore/debug="{debug_keystore}"')
        text = text.replace('keystore/debug_user=""', 'keystore/debug_user="androiddebugkey"')
        text = text.replace('keystore/debug_password=""', 'keystore/debug_password="android"')
        preset_cfg.write_text(text)
        print(f"[setup_editor_settings] Updated export_presets.cfg keystore settings.")

    content = f"""[gd_resource type="EditorSettings" format=3]

[resource]
export/android/android_sdk_path = "{android_home}"
export/android/java_sdk_path = "{java_home}"
export/android/debug_keystore = "{debug_keystore}"
export/android/debug_keystore_user = "androiddebugkey"
export/android/debug_keystore_pass = "android"
"""

    for filename in ["editor_settings-4.3.tres", "editor_settings-4.tres"]:
        target = config_dir / filename
        target.write_text(content)
        print(f"[setup_editor_settings] Wrote {target}")

if __name__ == "__main__":
    main()
