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
| `language-core` | 合并用户词条、libime 拼音候选和原文候选，并根据词元特征、输入上下文和本地偏好决定首选。 |
| `modern-ime-candidate-ui` | 通过 `org.modernime.UI1` 接收完整快照并渲染无焦点候选窗。 |
| `modern-ime-service` | 保存配置和词库、发现麦克风、管理 voice worker、修复 Fcitx profile。 |
| `modern-ime-voice-worker` | 常驻但暂停指定 PipeWire source，按会话采集音频并执行 VAD、流式 ASR 和标点恢复。 |
| `modern-ime-settings` | 通过 Service D-Bus 修改配置和词库。 |

键盘链路：`KeyEvent → engine → language-core → Fcitx InputPanel/UI1 → commitString`。

语音链路：`F8 → engine → Service → worker → PipeWire/ASR → Service/UI1 → engine commitString`。

## 持久化与接口

- Service 接口定义：`data/dbus/org.modernime.Service1.xml`。
- 配置：`$XDG_CONFIG_HOME/modern-ime/config.json`。
- 个人词库与中英文选择偏好：`$XDG_DATA_HOME/modern-ime/user.db`。
- 模型和运行时：`$XDG_DATA_HOME/modern-ime/models/`、`runtime/`。
- 日志：`$XDG_STATE_HOME/modern-ime/modern-ime.jsonl`。
- 音频使用内存环形缓冲，不写入音频文件。

## 不应破坏的实现约束

1. `CandidateSnapshot` 使用每次引擎启动唯一的 `producer_id`，同一 producer 内才比较全局递增 `sequence`。否则 Fcitx 重启后，新快照会被候选 UI 当作旧数据丢弃。
2. Qt/X11 的 Fcitx 光标矩形是物理像素，独立 Qt 候选窗使用逻辑坐标。`CandidateController` 必须按目标屏幕 DPR 逆变换，避免 150% 缩放被应用两次。
3. X11 候选窗必须不接受焦点，并使用 `X11BypassWindowManagerHint`；每次可见快照后重新 `raise()`，避免被 Plasma popup 遮挡。
4. 候选窗不参与桌面会话恢复，避免与 systemd/引擎按需拉起的实例争抢 `org.modernime.UI1`。systemd 单元只能挂到 `graphical-session.target`，安装时需导入当前图形会话环境。
5. X11 长按 F8 会产生自动重复的 release/press 对。引擎需忽略明确的 repeat，并保留短 release debounce，不能在每个合成 release 上结束录音。
6. 语音最终提交在 Fcitx 延迟事件中完成，并校验 session、输入上下文和 focus generation；不要从 worker 线程直接提交到应用。
7. `RepairFcitx` 只能向当前输入法组补回 `modernime`，不得调用会重置整个列表的 `ResetIMList`。
8. `.monitor` source 是输出监听设备，不能作为麦克风保存。敏感输入上下文不得启动语音。
9. 所选麦克风在线时，worker 必须先建立暂停的 PipeWire 流并后台预热模型；设备离线时不得无意义常驻大模型。冷启动会话需先激活采集再等待模型，保证首句音频进入环形缓冲。暂停状态不得接收或保存采样。
10. ASR 模型通过受限的 `model.json` manifest 解析。只有 transducer + `modified_beam_search` 可启用热词；`cjkchar+bpe` 模型必须同时提供 `bpe_vocab`。
11. 个人词条的选择计数必须和词条 upsert 在同一 SQLite 事务中分别执行；`sqlite3_prepare_v2` 不会自动执行同一字符串中的后续 SQL。分段组句结束时必须额外记录完整读音和完整输出。Service 成功修改词库后发出 `LexiconChanged`；引擎通过挂接到 Fcitx 事件循环的原生 D-Bus 监听刷新不可变词库快照，不能依赖未运行的 Qt 事件循环，也不能在按键热路径查询 SQLite。
12. 语音热词最多 256 条，必须去重并按词条类型/选择次数筛选；`cjkchar` 模型还要逐字符校验 `tokens.txt`，不能把 OOV 字符交给解码器。
13. 中英文偏好使用独立的 `input_preference` 表，按全局和应用作用域同时计数，最近一次选择用于平局裁决。原文选择不得写成拼音词条或进入语音热词。引擎只读取 `LexiconChanged` 后刷新的不可变偏好快照，不得在按键热路径查询 SQLite，也不得持久化光标周边文本。
14. 用户级桌面启动器不能依赖交互式 shell 的 `PATH`。安装脚本必须把 `modern-ime-settings.desktop` 的 `Exec` 改写为当前用户前缀下的绝对路径，并刷新桌面应用数据库。
15. 流式 ASR 的中间假设不得送入离线标点模型；只在会话最终化时对完整文本恢复标点。标点推理异常必须降级为原始识别文本，不能越过动态加载的 C API 导致 worker 退出。
16. 麦克风收到持续数字零不等于用户静音。达到 500 ms 时销毁并重建 PipeWire 流一次；重连后仍为数字零则以 `AudioStreamSilent` 结束会话，不要把蓝牙 SCO 假在线误报为 `EmptySpeech`。

## 当前候选和提交规则

自动模式把输入视为一个尚未决定语言的词元。大写/驼峰、数字混合、`._-/@:+#>` 等代码结构、内置开发词和低歧义 Shell 命令（包括 `ls`、`ll`、`cd`、`pwd`、`rg` 等）、URL/邮箱能力标记、路径/参数前缀和光标前代码分隔符会提高原文优先级；应用名只作为个人偏好的作用域，不会把整个 IDE 粗暴切成英文。可能是完整拼音或更长拼音前缀的命令不得首次自动提交，以免 `bash` 截断 `bashi` 一类中文输入。`git` 等高置信开发词、明确代码结构和已经学成原文的词会在达到判定点时立即提交并关闭候选窗。完整小写开发词之后紧接的字母重新作为中文优先的组合，`agentnihao` 可得到 `agent你好`；用户输入空格时，`agent nihao` 得到 `agent 你好`；`gitlab` 等已知连写开发词会继续提交原文。大写、数字和代码结构仍在当前词元内直通，空格、回车或普通分隔符结束直通。若没有强信号，则沿用中文候选顺序：未禁用用户词条 → 高分整句 → 少量单字/短词前缀 → 其余整句/前缀 → 原文回退。中文优先模式始终使用后一顺序，英文直输模式不建立组合。

智能模式下，`Ctrl+Alt+Space` 默认切换当前输入上下文的“智能混合 / 临时中文”，快捷键可配置。临时中文使用与固定中文优先相同的候选和提交规则，禁止开发词自动直输；已有预编辑会立即按中文模式重新生成候选。其它 Ctrl 组合键交给应用，不触发模式切换；输入上下文失焦或重置后恢复智能模式。该行为由 `control_mode_toggle` 开关及 `mode_toggle_hotkey` 快捷键控制，旧版 `shift_mode_toggle` 仍可读取为开关；固定中文优先和英文直输模式不受此快捷键影响。

用户选择原文或中文候选后，Service 在一个事务中更新全局与当前应用两条偏好记录，再通过 `LexiconChanged` 热更新引擎。应用内记录权重高于全局记录；计数相同时以最近一次选择为准。普通小写拼音的一次原文选择只视为弱信号，至少重复选择两次才提升为英文，避免一次误按让 `nihao` 永久直通；已知开发词仍可在一次明确选择后学习。高置信词已经自动提交，因此后续 `Space` 和 `Enter` 是应用收到的正常边界键；仍处于模糊候选的原文首选则由一个 `Space` 同时提交词元和空格。普通拼音不会自动执行不可逆提交。候选窗只在仍需决定时显示，并以“代码”“开发词”“个人英文”等注释说明原文为何被提升。

libime 候选同时保存其格路径的输入终点；选择前缀候选时，引擎把已选汉字保留在预编辑区，只对剩余拼音重新生成候选，最后一段选完后才一次性提交整句，并学习完整“原始拼音 → 组句结果”映射。启用 `insert_spacing` 时，提交链路根据临时的光标前一个字符为中英文边界补空格，但不会保存周边文本。

`normalizeForCommit` 合并连续空白并去掉尾部空白。语音 worker 的流式预览只做轻量文本清理；松开 F8 后使用本地 CT-Transformer 对完整结果恢复一次标点，清理标点前空格并保证句末标点。模型漏判、临时不可用或 ONNX Runtime 推理异常时，提交链路为中文/英文分别补 `。`/`.`，但不终止识别进程。Service 将固定/手动词条和达到门槛的已学习词条写入 `voice-hotwords.txt`，模型重载后用于 ASR 上下文偏置。单字学习词不进入热词，两字学习词至少被选择两次，三字及以上学习词可以保留；热词文件经过字符安全校验和模型词表过滤。数字、日期和单位尚未做完整逆规范化。
