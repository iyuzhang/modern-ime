# 当前状态与后续优先级

最后验证：2026-08-02。

## 可用基线

- 普通拼音输入、原文提交、数字选择、页内移动和翻页可用。
- 候选窗定位、150% 缩放、Plasma popup 层级和引擎重启后的快照恢复已修复。
- 候选窗支持午夜蓝、极光绿、云雾白和墨韵主题；主题、字号、圆角和透明度可在设置页修改并即时应用。
- F8 按住说话可录音，松开后提交一次最终文本；X11 自动重复不会再切出大量短会话。
- SQLite 个人词库、麦克风选择、快捷键保存和 Fcitx profile 修复可用。
- Debug/Release 在 `-Werror` 下构建通过，默认四项 CTest 全部通过。

总体评价：功能勉强可用于当前机器，但交互、识别结果和安装体验都未达到生产质量。

## 已知不足

- 候选窗无鼠标选择；长候选、分页提示和边缘布局仍需改进。
- 只完成少量真实应用验证，缺少 Qt、GTK、浏览器、Electron、终端和焦点快速切换矩阵。
- 语音输出为裸 ASR 文本，没有标点、数字逆规范化、VAD、热词或个人词纠正。
- 蓝牙输入使用固定软件增益策略，设备重连、source 更名、静音和异常恢复覆盖不足。
- 安装依赖当前机器的 Fcitx 系统插件目录和非交互 sudo；没有 deb 包、CI 或发布流程。
- Wayland、多显示器、动态缩放、GNOME、Flatpak/Snap 和其他发行版未验证。

## 下一步建议

1. 先改善键盘输入体验：候选窗样式、布局、分页、鼠标操作以及常用应用回归。
2. 建立可重复的桌面端到端测试，覆盖焦点切换、Fcitx 重启、服务重启和 100%/150% 缩放。
3. 为语音增加独立、可评测的后处理管线，优先比较离线标点模型与保守规则；任何文本改写都需要固定音频准确率回归。
4. 改善麦克风生命周期、VAD、长文本和设备断线恢复。
5. 增加 CI、许可证清单、可卸载的发行包和无需特殊 sudo 配置的安装方式。

## 下一次开发的启动检查

```bash
git status --short
cmake --build build-debug --parallel
ctest --test-dir build-debug --output-on-failure
systemctl --user is-active modern-ime-service.service modern-ime-ui.service
```

开始修改前先读 [architecture.md](architecture.md) 的关键约束；完成修改后至少执行 [development.md](development.md) 中的 Debug/Release 检查。
