# 架构与关键约束

## 进程与数据流

```text
应用程序
  ↕
Fcitx5 + libmodernime.so
  ├─ language-core（libime + SQLite）
  ├─ D-Bus → modern-ime-candidate-ui（Qt Quick）
  └─ D-Bus → modern-ime-service
                    └─ modern-ime-voice-worker
                         └─ PipeWire + sherpa-onnx

modern-ime-settings ── D-Bus ── modern-ime-service
```

| 组件 | 职责 |
| --- | --- |
| `libmodernime.so` | Fcitx5 输入法引擎；维护组合、候选、快捷键、语音会话并调用 `commitString`。 |
| `language-core` | 合并用户词条、libime 拼音候选和原文候选。 |
| `modern-ime-candidate-ui` | 通过 `org.modernime.UI1` 接收完整快照并渲染无焦点候选窗。 |
| `modern-ime-service` | 保存配置和词库、发现麦克风、管理 voice worker、修复 Fcitx profile。 |
| `modern-ime-voice-worker` | 从指定 PipeWire source 采集音频并调用 sherpa-onnx 流式识别。 |
| `modern-ime-settings` | 通过 Service D-Bus 修改配置和词库。 |

键盘链路：`KeyEvent → engine → language-core → Fcitx InputPanel/UI1 → commitString`。

语音链路：`F8 → engine → Service → worker → PipeWire/ASR → Service/UI1 → engine commitString`。

## 持久化与接口

- Service 接口定义：`data/dbus/org.modernime.Service1.xml`。
- 配置：`$XDG_CONFIG_HOME/modern-ime/config.json`。
- 个人词库：`$XDG_DATA_HOME/modern-ime/user.db`。
- 模型和运行时：`$XDG_DATA_HOME/modern-ime/models/`、`runtime/`。
- 日志：`$XDG_STATE_HOME/modern-ime/modern-ime.jsonl`。
- 音频使用内存环形缓冲，不写入音频文件。

## 不应破坏的实现约束

1. `CandidateSnapshot` 使用每次引擎启动唯一的 `producer_id`，同一 producer 内才比较全局递增 `sequence`。否则 Fcitx 重启后，新快照会被候选 UI 当作旧数据丢弃。
2. Qt/X11 的 Fcitx 光标矩形是物理像素，独立 Qt 候选窗使用逻辑坐标。`CandidateController` 必须按目标屏幕 DPR 逆变换，避免 150% 缩放被应用两次。
3. X11 候选窗必须不接受焦点，并使用 `X11BypassWindowManagerHint`；每次可见快照后重新 `raise()`，避免被 Plasma popup 遮挡。
4. X11 长按 F8 会产生自动重复的 release/press 对。引擎需忽略明确的 repeat，并保留短 release debounce，不能在每个合成 release 上结束录音。
5. 语音最终提交在 Fcitx 延迟事件中完成，并校验 session、输入上下文和 focus generation；不要从 worker 线程直接提交到应用。
6. `RepairFcitx` 只能向当前输入法组补回 `modernime`，不得调用会重置整个列表的 `ResetIMList`。
7. `.monitor` source 是输出监听设备，不能作为麦克风保存。敏感输入上下文不得启动语音。

## 当前候选和提交规则

候选顺序为：未禁用用户词条 → libime 拼音候选 → 原文候选。用户词条按固定、手动、选择次数、最近时间、权重和文本排序。

`normalizeForCommit` 当前只合并连续空白并去掉尾部空白；不会补标点或重写 ASR 文本。个人词库只影响键盘候选，尚未接入 ASR 热词或纠正。
