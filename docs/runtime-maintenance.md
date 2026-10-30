# 运行时与 MasterDB 维护边界

## 运行时

| 位置 | 职责 |
| --- | --- |
| `runtime/dictionary_catalog.cpp` | 词典格式校验、I18n 原文约束、精确及限定场景查询 |
| `runtime/dictionary_runtime.cpp` | 词典状态、完整替换、需要重启的配置、MasterDB 冲突原文抑制 |
| `localify.cpp` 与 Unity `.inc` 文件 | JNI、IL2CPP 钩子、对象生命周期、控件路径、字体和布局操作 |
| `runtime/preview_access.cpp` 与 `preview_decorations.inc` | 实验性内容预览的游戏适配、独立页面副本及账号写入隔离 |
| `preview_access_policy.h`、`appearance_overrides.h`、`profile_decoration_state.h` | 类别开关、进程内外观状态及过期页面检查，不依赖 Unity |
| `master_blob_matcher.h` | 校验记录补丁文件，并仅在原始 Blob 每个字节都相同时替换 |
| `tools/` | 采集、翻译、迁移、编译和发布；这些 Python 工具不进入游戏运行时 |

`idoly_runtime_core` 是独立的 C++ 构建目标，不依赖 Android、JNI 或 Unity。新增纯文本规则放在这一层；获取控件和修改字体仍由 Unity 适配层负责。查询控件路径采用延迟回调，普通文本无需遍历控件层级。

使用 `DictionaryRuntime.catalog()` 返回的引用时，调用方必须持有 `frameMutex()`；不得跨帧保存这些引用。词典先在锁外解析并完整校验，再在同一递归锁内替换。无效更新和需要重启的更新不改变当前数据或代次；启动配置不能通过热重载修改。I18n 指定了原文变体时，未匹配的原文不会回落到通用键译文。

## 实验性预览

预览模块独立于词典核心和翻译总开关，校验游戏方法的静态属性、参数、返回类型、字段类型及值类型 ABI。集合返回值允许受限的引用类型匹配，服装选择器的委托参数只接受 `System.Func`。必要接口缺失时关闭对应能力；普通换装适配失败不影响仍兼容的拍摄选择器。初始化异常不阻断本地化加载。JNI 状态查询、开关和安装结果共用模块锁，不能暴露安装中的部分状态。

扩展资源列表从 MasterDB 创建独立集合；普通换装使用角色数据副本，名片显示使用资料副本。不得把这些集合写回 `UserDataManager`，也不能直接修改共享的持有条目。外观选择只在进程内存中保存，账号初始化时清除。

普通换装和名片编辑必须先建立账号写入隔离，再显示扩展内容。晚开启、页面隔离失败、设置代次变化或关闭开关都不能使旧实验页面转回原保存接口。确认前核对当前选择、账号加载周期和已载入资源；检查失败时提示重新进入。

拍摄选择器的作用范围按窗口类型判断。3D 姿势通过 `PhotographyExpression3DModel.CreateSpotData` 的同步调用范围接入，鉴赏和正式拍摄均按姿势开关使用独立集合，其他类别的读取不随之扩展。保留游戏传入的场景模式、搭配检查和资源加载流程；正式拍摄请求仍使用当前选择的内容 ID，不拦截上传或伪造服务器结果。临时作用范围在嵌套调用和异常退出时恢复，不以曾打开过某页面作为永久标记。

原函数指针由 `inline_hook.h` 直接交给 ShadowHook 发布，不能先安装跳转再从临时变量复制。新增适配须验证缺失接口、开关关闭、旧页面隔离和对象复用；使用方式见[内容预览](content-preview.md)。

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
python3 -m unittest discover -s tools -p 'test_preview_runtime.py'
python3 -m unittest discover -s tools -p 'test_master*.py'
python3 -m unittest discover -s tools -p 'test_migrate_master_keys.py'
./gradlew :app:externalNativeBuildDebug
```

记录键和字段路径变化不改变 `IDOLYMB2` 的运行时补丁格式。迁移验证应比较同一官方快照在迁移前后生成的 Blob 内容，而不只比较译文条数。
