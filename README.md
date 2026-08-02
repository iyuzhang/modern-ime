# Modern IME

Modern IME 是一个面向 Fcitx5 的本地输入法，将全拼、英文原文候选、个人词库和离线语音输入整合到同一提交链路中。

当前版本已经能在 Ubuntu 24.04、KDE Plasma 5.27、X11 环境中日常输入，但交互和兼容性仍比较粗糙，暂不应视为成熟产品。

## 已实现

- libime 全拼候选、英文原文候选和 SQLite 个人词库。
- `Space`、数字键、方向键、翻页、`Enter` 和 `Esc` 等基本输入操作。
- 独立 Qt Quick 候选窗，支持 150% 缩放定位、无焦点置顶和引擎重启恢复。
- 候选窗提供午夜蓝、极光绿、云雾白、墨韵和带原创动态角色的星愿少女主题，可调整字号、圆角和背景不透明度。
- F8 按住说话、PipeWire 流复用、Silero VAD、sherpa-onnx 流式识别、INT8 离线中英标点恢复和松开提交。
- 默认使用 2025 普通话模型；设置页可在已安装模型中切换，并保留旧中英混输兼容模型。
- 个人词库自动生成本地 ASR 热词，稀有短词会使用 modified beam search 做上下文偏置。
- 设置界面、麦克风选择、快捷键设置、词库导入导出和 Fcitx 配置修复。
- 单元测试、候选窗 offscreen/X11 smoke test 和键盘候选延迟测试。

## 支持范围与限制

- 当前只验证 Ubuntu 24.04、Fcitx5 5.1.7、KDE Plasma 5.27 和 X11；Wayland、多显示器和其他发行版尚未验证。
- 语音识别尚无完整数字/日期逆规范化和 VAD 自动结束；普通话模型不适合长段中英混说。
- 真实口音、噪声和蓝牙设备仍需用用户授权录制的固定语料持续做 CER 回归。
- 候选窗尚不支持鼠标选择，视觉样式和长候选布局仍需改进。
- 安装脚本需要把 Fcitx 插件写入系统 ABI 目录，因此当前要求可用的非交互 `sudo`。
- 模型与 sherpa-onnx 运行时由安装脚本下载，不存放在仓库中；音频只在内存中处理。

## 使用截图

### 拼音候选

![拼音候选窗](docs/images/pinyin-candidates.jpg)

### 星愿少女主题

![星愿少女主题候选窗](docs/images/starlight-theme.jpg)

### 语音识别

![语音识别候选窗](docs/images/voice-recognition.jpg)

## 构建和安装

安装依赖和开发说明见 [docs/development.md](docs/development.md)。典型流程：

```bash
cmake -S . -B build-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DMODERN_IME_WARNINGS_AS_ERRORS=ON
cmake --build build-release --parallel
ctest --test-dir build-release --output-on-failure
./tools/install-user.sh build-release
```

卸载程序使用 `./tools/uninstall-user.sh`。个人配置、词库和模型默认保留。

## 文档

- [架构与关键约束](docs/architecture.md)
- [开发、测试和部署](docs/development.md)
- [当前状态与后续优先级](docs/status.md)

## 许可证

[MIT](LICENSE)
