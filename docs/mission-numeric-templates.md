# 活动任务数字文本

`tools/mission_numeric_templates.py` 从 `translations/master/orig/Mission.json` 与对应 `zh-Hans/Mission.json` 自动导出以下任务的数字文本：

- `event-love-*`：若恋活动任务，包括后续新增角色。
- `event-ms-idoly-pride-day-*`：IDOLY PRIDE 日限时任务，包括后续新增日期。

只读取 `name`、`shortDescription`、`longDescription` 中恰好一个 `{threshold}` 的源文和译文，将占位转换为运行时使用的 `{number}`。固定数字、人物姓名和其他字面内容保持不变，不生成任意文本或姓名占位。不需要逐个填写任务阈值，也不用手工复制到 UI JSON。

同源同译自动去重；缺少译文、同源多译、额外占位、原译文占位不一致、与手工数字模板冲突均使编译失败。任务表两侧同时不存在时视为无任务输入，供精简测试项目使用；仅缺一侧时报错。

`pipeline build` 和 `localize build` 将导出结果与已有手工 `numeric_text_templates` 合并。合并后仍限制最多 128 条；每条必须满足原有字面长度与字节上限。运行时继续严格全字符串匹配 1–6 位 ASCII 数字，不放宽为部分匹配，不处理昵称或千位分隔符。

来源记录单独发布到 `.analysis/mission-template-sources.json`，每条模板可追溯至 `Mission/记录ID/字段`；不放入 APK 字典。`pipeline verify` 同时检查来源报告和编译字典。任务源文和译文有更新后重新运行现有 pipeline 即可。
