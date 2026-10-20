# 运行时与 MasterDB 维护边界

## 运行时

| 位置 | 职责 |
| --- | --- |
| `runtime/dictionary_catalog.cpp` | 词典格式校验、I18n 原文约束、精确及限定场景查询 |
| `runtime/dictionary_runtime.cpp` | 词典状态、完整替换、需要重启的配置、MasterDB 冲突原文抑制 |
| `localify.cpp` 与 Unity `.inc` 文件 | JNI、IL2CPP 钩子、对象生命周期、控件路径、字体和布局操作 |
| `master_blob_matcher.h` | 校验记录补丁文件，并仅在原始 Blob 每个字节都相同时替换 |
| `tools/` | 采集、翻译、迁移、编译和发布；这些 Python 工具不进入游戏运行时 |

`idoly_runtime_core` 是独立的 C++ 构建目标，不依赖 Android、JNI 或 Unity。新增纯文本规则放在这一层；获取控件和修改字体仍由 Unity 适配层负责。查询控件路径采用延迟回调，普通文本无需遍历控件层级。

使用 `DictionaryRuntime.catalog()` 返回的引用时，调用方必须持有 `frameMutex()`；不得跨帧保存这些引用。词典先在锁外解析并完整校验，再在同一递归锁内替换。无效更新和需要重启的更新不改变当前数据或代次；启动配置不能通过热重载修改。I18n 指定了原文变体时，未匹配的原文不会回落到通用键译文。

## MasterDB 记录身份

原文与译文保持 `表 → 记录键 → 字段路径 → 文本` 结构。`tools/master_identity.py` 是记录键规则的唯一实现，JSON 导出和 protobuf Blob 编译共同使用它。

- 有标准 `id` 的表保留原来的记录键。
- 没有标准 `id` 的已知表使用显式业务字段，例如 `HomeAction.homeActionId`、`HomeTalk.homeTalkId`、`Wording.key`。
- 多字段记录使用组合键，例如 `CardEvolutionMessage` 的 `cardId + evolutionLevel + number`。
- 业务键编码为 `bk1:` 加紧凑 JSON 字段和值数组。例如 `bk1:[["cardId","card-a"],["evolutionLevel",0],["number",1]]`。
- 字符串保持原样；整数及枚举统一为数值，`0` 有效。缺少字段、重复身份或未知无 `id` 表会报错，不使用行号兜底。

完整规则随文本保存在 `master/record-keys.json`。同一记录身份下，原文变化仍会使旧译文失效；保留日文的审核结果同样受保护。业务键不包含文本，因此记录重排或插入其他记录不会改变它。

### 嵌套数组

`tools/master_field_paths.py` 统一解析 JSON 与 protobuf 的字段路径，规则随文本保存在 `master/field-keys.json`。列表身份只在所属记录和数组内生效：

| 数组 | 身份字段 |
| --- | --- |
| `ActivityAbility/ActivityFanEvent/ActivityPromotion/ActivityRefresh/LiveAbility/Skill.levels` | `level` |
| `Message.details` | `messageDetailId` |
| `HelpCategory.contents` | `helpContentId` |
| `HomeTalk.characterTalks`、`Character.activityFanEventWords` | `voiceAssetId` |
| `Character.loveInfos` | `loveId` |
| `CharacterGroup.mappings` | `characterId` |
| `ExerciseHint.contents` | `number` |
| `Stage.cameraPositions`、`Story.branchChoices` | `index` |
| `Tutorial.stepInfo` | `step + subStep` |

字段路径例如 `details[key={"messageDetailId":"message-a"}].text`，教程则为 `stepInfo[key={"step":0,"subStep":1}].texts[0]`。键值采用紧凑 JSON，不能用简单的句点或方括号拆分路径；`index` 是游戏数据中的显式字段，不是数组下标。

上述数组缺少身份字段或出现重复身份时会报错。语音资源 ID 改变视为新条目，不能仅凭原文相同沿用译文。未列出的数组继续使用位置和原文校验，例如没有独立 ID 的教程 `texts[]`；不从文本内容推算身份。

### 旧数据迁移

迁移需要与 `upstream.json` 所记录 MasterTag 相同的完整原始快照，且下载状态中的 `complete_version` 与版本一致、各表哈希校验通过：

```sh
python3 tools/migrate_master_keys.py \
  --raw-dir /path/to/verified-masterdata \
  --repository /path/to/translation-repository
```

默认只检查；确认结果后追加 `--apply`。工具同时迁移原文与译文的记录键及嵌套字段路径，保留字段子集、空译文和刻意保留的原文，并更新原文快照哈希。不能用新版本的行号或数组位置推断旧版本身份。

执行和恢复期间须暂停编辑器、翻译任务及发布任务对该目录的写入。文件哈希检查不能代替独占写入，无法阻止其他程序在最后一次检查与替换之间保存文件。

备份和迁移映射保存在翻译仓库的 `.agent-local/master-key-migrations/`。中断标记存在时，导出和编译会停止；重跑同一命令可恢复。恢复时发现文件与迁移前后状态均不一致，会拒绝覆盖。

## 验证

便携运行时测试直接链接正式代码，无需游戏：

```sh
python3 -m unittest discover -s tools -p 'test_dictionary_reload.py'
python3 -m unittest discover -s tools -p 'test_master*.py'
python3 -m unittest discover -s tools -p 'test_migrate_master_keys.py'
./gradlew :app:externalNativeBuildDebug
```

记录键和字段路径变化不改变 `IDOLYMB2` 的运行时补丁格式。迁移验证应比较同一官方快照在迁移前后生成的 Blob 内容，而不只比较译文条数。
