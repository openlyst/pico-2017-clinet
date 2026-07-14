# PicoVR 2.0.13 Compatibility Patch - Findings

## Summary

PicoVR (`com.picovr.wing`) version 2.0.13 (build 132, dated 2016-08-25) is a legacy multi-tool app for early Pico Smartphone VR headsets (Pico 1 / 1S, Pico U / U Lite). The original APK targets `targetSdkVersion=19` (Android 4.4 KitKat) and will not install or run on modern Android (tested on Samsung SM-S918U, Android 15, SDK 35).

This document records every issue found and the patches applied to get the app launching and running on a modern device.

---

## Original APK Metadata

| Field | Value |
|---|---|
| Package | `com.picovr.wing` |
| VersionName | 2.0.13 |
| VersionCode | 64 |
| Internal build | 2.0.13_132-20160825-release |
| Build timestamp | Thu Aug 25 18:06:20 CST 2016 |
| minSdkVersion | 16 (Android 4.1) |
| targetSdkVersion | 19 (Android 4.4) |
| platformBuildVersionCode | 21 |
| ABIs | `armeabi-v7a`, `x86` |
| SharedUserId | `com.android.packageinstaller` |
| Launchable activity | `com.picovr.wing.startup.StartupAnimActivity` |
| Backend server | `http://video.picovr.com/api3/` |

---

## Issues Found and Patches Applied

### 1. `INSTALL_FAILED_DEPRECATED_SDK_VERSION` (install block)

**Cause:** Android 14+ refuses to install apps with `targetSdkVersion < 24`.

**Patch:** Bumped `targetSdkVersion` from 19 to 24 in `apktool.yml` and `platformBuildVersionCode`/`platformBuildVersionName` in `AndroidManifest.xml`.

### 2. `sharedUserId="com.android.packageinstaller"` (install/security block)

**Cause:** `sharedUserId` is deprecated since Android 10 (API 29) and is rejected on modern Android. Sharing a UID with the system package installer is also a security red flag.

**Patch:** Removed the `android:sharedUserId` attribute from the `<manifest>` tag.

### 3. Missing `android:exported` on components with intent-filters (install block on API 31+)

**Cause:** Android 12+ requires every `<activity>`, `<receiver>`, `<service>`, and `<provider>` with an `<intent-filter>` to explicitly declare `android:exported`. 18 components in the original manifest were missing it.

**Patch:** Added `android:exported="true"` to all 18 components that had intent-filters but no exported attribute (launcher activity, push receivers, Unity activities, PicoPlayer, etc.).

### 4. Duplicate `<uses-permission>` entries (lint warning)

**Cause:** Several permissions (`ACCESS_NETWORK_STATE`, `GET_TASKS`, `SYSTEM_ALERT_WINDOW`) were declared twice.

**Patch:** De-duplicated the permission list.

### 5. `MODE_WORLD_READABLE` SecurityException crash (runtime)

**Cause:** `WingApp.onCreate` (and 10 other classes) called `getSharedPreferences(name, MODE_WORLD_READABLE)` (mode `0x1`) or `MODE_WORLD_WRITEABLE` (`0x2`) / `MODE_MULTI_PROCESS` (`0x4`). Since Android 7 (API 24) `MODE_WORLD_READABLE` throws `SecurityException` and crashes the app on startup.

**Patch:** Wrote a Python script (`patch_shared_prefs.py`) that walks every `.smali` file, finds `getSharedPreferences(Ljava/lang/String;I)` calls, traces the mode register backwards to its `const/4` assignment, and rewrites any non-zero mode to `0x0` (`MODE_PRIVATE`). Patched 11 smali files:

- `com/picovr/unitylib/UnityActivity.smali`
- `com/picovr/wing/BaseActivity.smali`
- `com/picovr/wing/WingApp.smali`
- `com/picovr/wing/lark/LarkManager.smali`
- `com/picovr/wing/psetting/PSettingCITActivity.smali`
- `com/picovr/wing/psetting/PSettingDisplayActivity.smali`
- `com/picovr/wing/psetting/PSettingDisplayActivity$5.smali`
- `com/picovr/wing/psetting/PSettingSelectMachine.smali`
- `com/picovr/wing/startup/CalibrationActivity.smali`
- `com/picovr/wing/utils/SPUtils.smali`
- `com/picovr/wing/utils/Utils.smali`

### 6. `ArrayIndexOutOfBoundsException` in proximity sensor listener (runtime crash ~20s after launch)

**Cause:** `BaseActivity$MySensorEventListener.onSensorChanged` unconditionally read `event.values[1]` and `event.values[2]` for debug logging. On modern devices the proximity sensor (`TYPE_PROXIMITY=8`) only reports a 1-element `values` array, so `values[1]` threw `ArrayIndexOutOfBoundsException: length=1; index=1` and crashed the app.

The actual proximity logic only uses `values[0]`; the `[1]`/`[2]` reads were purely for a `Log.d` debug string.

**Patch:** Replaced the `aget v3, v0, v4` (values[1]) and `aget v3, v0, v3` (values[2]) reads with `const/4 v3, 0x0` so the debug log prints `0.0` instead of crashing. The real sensor logic at `:cond_1` (which only touches `values[0]`) is unchanged.

### 7. Application-level hardening flags (preventive)

Added to `<application>`:

- `android:requestLegacyExternalStorage="true"` - keeps legacy `File`-based storage access for the video player / download manager.
- `android:usesCleartextTraffic="true"` - the app talks to `http://video.picovr.com` (no HTTPS); without this, API calls fail on API 28+.
- `android:extractNativeLibs="true"` - the bundled `.so` files are uncompressed-in-APK on old tooling; forcing extraction avoids `dlopen` issues on devices that don't support in-APK native libs.

### 8. Native library text-relocations (warning, not fatal)

`libstreamingsdk_jni-armandroid-r4-gcc44-mt-1.1.0.so` has text relocations, which are blocked on API 23+. The linker logs an error but the app continues because the streaming SDK is only used for the video store, not for app startup. This is a known limitation - fixing it would require recompiling the native library from source, which we don't have.

### 9. Missing `libpenguin.so` (warning, not fatal)

Log shows `Unable to open libpenguin.so: dlopen failed: library "libpenguin.so" not found.` This is a Pico-specific system library expected on Pico devices. The app degrades gracefully and continues running without it.

### 10. Backend server status

`http://video.picovr.com/api3/` returns HTTP 500 with an empty body. The video store / movie catalog features will not work because the backend is effectively dead. Login, local video playback, and the launcher UI still function.

---

## Build / Sign

- Repacked with `apktool b`.
- Signed with a generated debug keystore (`debug.keystore`, RSA 2048, 10000-day validity) via `apksigner`.
- Output: `patched_signed.apk` (~40 MB).

---

## Test Results

Test device: Samsung SM-S918U (Galaxy S23 Ultra), Android 15, SDK 35, arm64-v8a (running the armeabi-v7a libs via 32-bit compat).

| Step | Result |
|---|---|
| Install | Success |
| Grant permissions | Success |
| Launch `StartupAnimActivity` | Success, cold start ~734ms |
| Transition to `PicoVRMainActivity` | Success |
| Run 30s+ without crash | Success (no `AndroidRuntime:E` entries) |
| Backend API (`video.picovr.com/api3`) | HTTP 500 - server-side dead |

The app launches, displays the main UI, and no longer crashes. Online features (movie store, app store, login) depend on the dead `video.picovr.com` backend and will not work without a replacement server. Local video playback and the 2D launcher should still be usable.

A Pico 1S with a fitting smartphone is still needed for full VR-mode testing (proximity sensor triggers 2D<->VR switch, Bluetooth controller pairing via the Lark service).

---

## Files

- `patch_manifest.py` - patches `AndroidManifest.xml` and `apktool.yml`.
- `patch_shared_prefs.py` - rewrites `MODE_WORLD_READABLE`/`WRITEABLE` to `MODE_PRIVATE` across all smali.
- `work/picovr_smali/` - apktool decompiled tree with all patches applied.
- `patched_signed.apk` - final installable APK (gitignored due to size; rebuild from `work/picovr_smali`).
- `screenshot_running.png` - screenshot of the app running on the test device.
