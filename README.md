# Modern IME

Modern IME 是一个面向 Fcitx5 的本地输入法，将全拼、英文原文候选、个人词库和离线语音输入整合到同一提交链路中。

当前版本已经能在 Ubuntu 24.04、KDE Plasma 5.27、X11 环境中日常输入，但交互和兼容性仍比较粗糙，暂不应视为成熟产品。

## 已实现

- libime 全拼候选、可连续逐字/逐词组句、整句自动学习和 SQLite 个人词库。
- 默认智能混合输入：`git`、`ls`、`ll`、常见低歧义 Shell 命令、驼峰、路径和命令参数等高置信英文会自动提交并临时直通，不显示待确认候选；模糊输入仍合并拼音与原文候选，并按应用记住选择。按 `Ctrl+Alt+Space` 可关闭智能判断、切换为当前输入上下文的临时中文，再按一次恢复智能；快捷键可在设置中修改，也可选择固定中文优先或英文直输。
- `Space`、数字键、方向键、翻页、`Enter` 和 `Esc` 等基本输入操作。
- 独立 Qt Quick 候选窗，支持 150% 缩放定位、无焦点置顶和引擎重启恢复。
- 候选窗提供 12 套主题，包含简洁配色和原创动态角色主题，可调整字号、圆角和背景不透明度。
- F8 按住说话、PipeWire 流复用、Silero VAD、sherpa-onnx 流式识别、最终阶段 INT8 离线中英标点恢复、句末标点兜底和松开提交；蓝牙麦克风数字静音时会自动重连一次并给出明确提示。
- 默认使用 2025 普通话模型；设置页可在已安装模型中切换，并保留旧中英混输兼容模型。
- 个人词库自动生成本地 ASR 热词，稀有短词会使用 modified beam search 做上下文偏置。
- 设置界面、麦克风选择、快捷键设置、词库导入导出和 Fcitx 配置修复。
- 单元测试、候选窗 offscreen/X11 smoke test、私有 Fcitx 连续组句端到端测试和键盘候选延迟测试。

## 支持范围与限制

- 当前只验证 Ubuntu 24.04、Fcitx5 5.1.7、KDE Plasma 5.27 和 X11；Wayland、多显示器和其他发行版尚未验证。
- 智能混合输入目前使用确定性规则、内置开发词和本地选择历史；尚未接入编辑器工作区符号，陌生且恰好是合法拼音的标识符仍可能需要首次选择原文。
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

## 安装（Ubuntu 24.04 x86_64）

推荐使用 Debian 安装包。拿到项目生成的 `.deb` 后，在文件所在目录运行：

```bash
sudo apt install ./modern-ime_0.1.4_amd64.deb
```

安装完成后，从应用菜单打开“Modern 输入法”，点击“启用 Modern IME”，再用 `Ctrl+Space` 或 Fcitx 输入法切换器选择它。首次使用语音时，在设置的“语音”页点击“下载离线语音模型”；键盘输入不需要下载模型。

语音输入时按住 `F8` 说话、松开提交。候选窗会立即显示连接或聆听状态；实时文字保持低延迟原文预览，只在松开后对完整结果恢复标点。标点模型异常会自动退回未加标点的识别文本，不会中断语音进程。

卸载使用：

```bash
sudo apt remove modern-ime
```

个人配置、词库和已下载的离线模型会保留。

## 从源码构建

安装依赖和开发说明见 [docs/development.md](docs/development.md)。典型流程：

```bash
cmake -S . -B build-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DMODERN_IME_WARNINGS_AS_ERRORS=ON
cmake --build build-release --parallel
ctest --test-dir build-release --output-on-failure
./tools/install-user.sh build-release
```

源码安装默认只安装键盘输入功能，不再自动下载语音模型；可在设置页按需下载，或设置 `MODERN_IME_INSTALL_MODEL=1` 让脚本同时安装。卸载程序使用 `./tools/uninstall-user.sh`，个人配置、词库和模型默认保留。

维护者可运行 `./tools/build-deb.sh`，经过 Release 构建和完整 CTest 后，安装包会生成到 `dist/`。

## 文档

- [架构与关键约束](docs/architecture.md)
- [开发、测试和部署](docs/development.md)
- [当前状态与下一阶段](docs/status.md)

## 许可证

[MIT](LICENSE)
