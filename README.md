# Modern IME

Modern IME 是一个面向 Fcitx5 的实验性本地输入法，将全拼、英文原文候选、个人词库和离线语音输入整合到同一提交链路中。

当前版本已经能在 Ubuntu 24.04、KDE Plasma 5.27、X11 环境中日常输入，但交互和兼容性仍比较粗糙，暂不应视为成熟产品。

## 已实现

- libime 全拼候选、英文原文候选和 SQLite 个人词库。
- `Space`、数字键、方向键、翻页、`Enter` 和 `Esc` 等基本输入操作。
- 独立 Qt Quick 候选窗，支持 150% 缩放定位、无焦点置顶和引擎重启恢复。
- F8 按住说话、PipeWire 采集、sherpa-onnx 中英双语离线识别和松开提交。
- 设置界面、麦克风选择、快捷键设置、词库导入导出和 Fcitx 配置修复。
- 单元测试、候选窗 offscreen/X11 smoke test 和键盘候选延迟测试。

## 支持范围与限制

- 当前只验证 Ubuntu 24.04、Fcitx5 5.1.7、KDE Plasma 5.27 和 X11；Wayland、多显示器和其他发行版尚未验证。
- 语音识别没有标点恢复、数字格式化、VAD 自动结束或用户词热词纠正。
- 候选窗尚不支持鼠标选择，视觉样式和长候选布局仍需改进。
- 安装脚本需要把 Fcitx 插件写入系统 ABI 目录，因此当前要求可用的非交互 `sudo`。
- 模型与 sherpa-onnx 运行时由安装脚本下载，不存放在仓库中；音频只在内存中处理。

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
