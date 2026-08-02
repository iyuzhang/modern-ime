# 开发、测试和部署

## 基线环境

当前已验证环境：Ubuntu 24.04 x86_64、KDE Plasma 5.27、X11、Fcitx5 5.1.7、Qt 6.4、libime 1.1、PipeWire 1.0、CMake 3.28 和 GCC 13。

Ubuntu 开发依赖：

```bash
sudo apt install cmake ninja-build g++ pkg-config catch2 xvfb \
  qt6-base-dev qt6-declarative-dev \
  qml6-module-qtquick-controls qml6-module-qtqml-workerscript \
  qml6-module-qtquick-layouts qml6-module-qtquick-templates \
  qml6-module-qtquick-window \
  libfcitx5core-dev libfcitx5config-dev libfcitx5utils-dev \
  libimecore-dev libimepinyin-dev libpipewire-0.3-dev
```

## 构建与最低检查

```bash
cmake -S . -B build-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DMODERN_IME_WARNINGS_AS_ERRORS=ON
cmake --build build-debug --parallel
ctest --test-dir build-debug --output-on-failure

cmake -S . -B build-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DMODERN_IME_WARNINGS_AS_ERRORS=ON
cmake --build build-release --parallel
ctest --test-dir build-release --output-on-failure
git diff --check
```

CTest 当前包含：

- 核心候选、词库和契约单元测试。
- offscreen Qt + 150% 缩放的候选窗 smoke test。
- Xvfb/xcb 候选窗层级和无焦点 smoke test。
- 键盘候选 P95 延迟门槛。

`modern-ime-asr-smoke` 需要已安装模型，用于固定音频的离线识别检查，不属于默认 CTest。

## 安装

```bash
./tools/install-user.sh build-release
```

脚本会重新构建和测试、备份现有部署文件、安装用户程序和 systemd user service，并下载经过 SHA-256 校验的 sherpa-onnx 1.13.2 与中英双语 Zipformer 模型。设置 `MODERN_IME_SKIP_MODEL=1` 可跳过模型下载。

当前 Fcitx5 从系统 ABI 目录加载插件，因此脚本需要 `sudo -n` 安装 `libmodernime.so` 和 `libmodernimeui.so`。后续应改为发行包或验证可靠的纯用户插件发现方式。

常用运行态检查：

```bash
systemctl --user status modern-ime-service.service modern-ime-ui.service
gdbus call --session \
  --dest org.modernime.Service1 \
  --object-path /org/modernime/Service1 \
  --method org.modernime.Service1.Diagnostics
journalctl --user -u modern-ime-service.service -u modern-ime-ui.service -b
```

改动引擎后必须重新安装系统 ABI 目录中的插件并重启 Fcitx；只改候选 UI 或 Service 时只需安装对应用户程序并重启相应 user service。

## 候选窗主题

- `ConfigSnapshot::theme` 是持久化主题字段；有效值为 `midnight`、`aurora`、`cloud`、`ink`、`starlight`、`sakura`、`matcha`、`lavender`、`peach-soda`、`moon-rabbit`、`mint-cat`、`berry-bear`。缺失或未知值回落到 `midnight`，以保持旧配置的原有外观。
- 设置页将主题和 `font_size`、`corner_radius`、`opacity` 通过 `UpdateConfig` 保存。Service 发出 `ConfigChanged` 后，候选窗的 `CandidateController::ApplyConfig` 立即更新外观；候选窗启动时通过 `GetConfig` 读取当前配置。
- `CandidateWindow.qml` 定义候选窗实际色板；`SettingsWindow.qml` 定义选择卡的预览色板。新增主题时，两处色值必须同步，并在 `CandidateTheme`、序列化/解析和主题单元测试中加入该主题。
- 字号会影响候选窗宽度和高度计算。修改文字尺寸、内边距或候选结构后，运行 offscreen 与 xcb 候选窗 smoke test。
- `starlight`、`moon-rabbit`、`mint-cat`、`berry-bear` 是大图标主题，分别使用 `anime-mascot.png`、`lunar-rabbit-mascot.png`、`mint-cat-mascot.png`、`strawberry-bear-mascot.png`。这些资源均以 `themes/<name>` 别名同时加入候选窗与设置页的 QRC；候选窗为角色预留 120 px 宽度，并将角色画布向背景框上方扩展 66 px，图标本身保留 4 px 顶部安全边距。星愿少女、月兔少女和莓果小熊使用轻摆，薄荷猫咪使用探头；语音“聆听中”统一播放呼吸缩放。
- 所有大图标素材均为项目生成的原创角色插画；本地保存的是经背景移除后的最终 PNG，不依赖第三方主题角色或在线资源。

## 调试守则

- 不向真实桌面的候选 UI 注入测试快照，也不向用户正在输入的应用注入按键；使用私有 D-Bus 和 Xvfb。
- 部署前保留当前二进制与 Fcitx profile，避免用旧构建覆盖源码基线。
- 模型、运行时、用户配置、数据库、音频和日志不得提交到仓库。
- 修改 D-Bus contract 时同步修改序列化测试和调用方。
- 修改候选窗口定位、层级或大小时，同时运行 offscreen 和 xcb smoke test。
- 修改 F8 状态机时覆盖 press、真实 release、自动重复、取消、焦点切换和服务重启。
