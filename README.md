# PicoVR 2.0.13 现代化兼容补丁

让 2016 年的 PicoVR（`com.picovr.wing`，版本 2.0.13）在现代 Android（14 / 15）上重新跑起来的自动化补丁工具。

## 背景

PicoVR 2.0.13 是早期 Pico 手机 VR 头显（Pico 1 / 1S、Pico U / U Lite）配套的多功能应用，原始 APK 的 `targetSdkVersion=19`（Android 4.4 KitKat）。在 Android 14+ 上会因为 SDK 版本过低、`sharedUserId`、缺少 `exported` 属性、`MODE_WORLD_READABLE` 崩溃等问题无法安装或启动。

本项目把这些修复全部打包成一个用 C 写的自动化工具，一键完成反编译、修补、重打包和签名。

## 修复内容

- **安装被阻止**：`targetSdkVersion` 19 → 24，同步更新 `platformBuildVersionCode`
- **`sharedUserId` 拒绝安装**：移除 `android:sharedUserId="com.android.packageinstaller"`
- **缺少 `exported`（API 31+）**：为 18 个带 intent-filter 的组件补上 `android:exported="true"`
- **重复权限声明**：对 `<uses-permission>` 列表去重
- **`MODE_WORLD_READABLE` 崩溃**：遍历所有 smali，把 `MODE_WORLD_READABLE/WRITEABLE/MULTI_PROCESS` 重写为 `MODE_PRIVATE`（共 11 个文件）
- **距离传感器数组越界**：`BaseActivity$MySensorEventListener` 中 `event.values[1]/[2]` 在现代设备上抛 `ArrayIndexOutOfBoundsException`，替换为常量 `0x0`
- **预防性加固**：`requestLegacyExternalStorage`、`usesCleartextTraffic`、`extractNativeLibs`

## 使用方法

依赖：`apktool`、`apksigner`、`keytool`（JDK）、`gcc`。

```bash
gcc -o patch patch.c
./patch path/to/app.apk
# 输出: path/to/app_patched.apk
```

工具会自动完成：

1. 用 `apktool` 反编译 APK
2. 修补 `AndroidManifest.xml`
3. 修补 `apktool.yml`
4. 遍历 smali 修复 `MODE_WORLD_*` 和传感器越界
5. 用 `apktool b` 重新打包
6. 生成调试密钥库并用 `apksigner` 签名
7. 输出 `<原文件名>_patched.apk`

`TMPDIR` 环境变量可指定临时工作目录。

## 已知限制

- `libstreamingsdk_jni-*.so` 存在文本重定位，API 23+ 上被链接器阻止。仅影响视频商店，不影响启动。修复需要原生库源码。
- `libpenguin.so` 是 Pico 设备专用系统库，非 Pico 设备上缺失，应用会优雅降级。
- 后端 `http://video.picovr.com/api3/` 已返回 HTTP 500，在线功能（电影商店、应用商店、登录）不可用。本地视频播放和 2D 启动器仍可用。
- 完整 VR 模式测试仍需 Pico 1S + 合适手机（距离传感器触发 2D/VR 切换，蓝牙控制器通过 Lark 服务配对）。

## 测试

测试设备：三星 SM-S918U（Galaxy S23 Ultra），Android 15，SDK 35，arm64-v8a。

| 步骤 | 结果 |
|---|---|
| 安装 | 成功 |
| 授予权限 | 成功 |
| 启动 `StartupAnimActivity` | 成功，冷启动约 734ms |
| 切换到 `PicoVRMainActivity` | 成功 |
| 30 秒以上无崩溃 | 成功 |

## 文件

- `patch.c` — 自动化补丁工具源码
- `patch` — 编译后的二进制
- `docs/调查结果.md` — 完整调查文档

## 许可

见 [LICENSE](LICENSE)。
