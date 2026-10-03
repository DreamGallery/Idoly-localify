# 字体页面配置 v1

`config/font-profiles.json` 是游戏 6.0.2、`replace-ui`、完整 Noto Sans CJK SC Bold 与独立 `module-font` 字体载荷的基线配置。原生插件与全屏参数预览面板共用这些参数。仓库基线修改后运行：

```sh
python3 tools/font_profiles.py
python3 -m unittest discover -s tools -p 'test_font_profiles.py'
python3 tools/font_profiles.py --write-header
```

配置使用 `schema_version=1`、游戏版本 `6.0.2`、独立 Unity 字体对象名 `Localify-ResourceHanRoundedSC-ExtraBold` 和十九个已识别 profile。模块提供完整 Noto Sans CJK SC Bold，游戏原字体保留。运行时只允许调整 `adjust` 中通过校验的数值、布尔值和 RGBA；`page`、`path_suffix`、`guard` 等选择条件锁定为编译基线，避免误命中其他页面。新控件或按钮必须先采集对象路径与原控件尺寸，再审核并新增 profile，不能用任意对象通配调参。

## 匹配与单位

每个规则同时指定 `page`、`path_suffix`（从上级到目标控件的 Unity 对象名）和 `guard`（原字号、原材质描边或文本框尺寸）。`page` 是运行时必须提供的页面类别；单靠 `Message Label` 等通用控件名不足以安全匹配。路径后缀与守卫必须全部匹配，多个 profile 命中应视为错误。`generic_ui` 是无调整的兜底规则，不提供全局字号覆盖。页类型与现有代码识别点的对应关系如下：

| page | 控件 | 当前设置 |
| --- | --- | --- |
| `ordinary_story` | `Message Label` | 独立材质：描边 1.0、字面扩张 0.9；可调字号（默认 50）和字间距（默认 8） |
| `ordinary_story` | `Name Label` | 独立材质：描边 0.5、字面扩张 0.5、黑字白边；保持原字号与字间距 |
| `portrait_love_event` | `Name Label/Text` | 原字号 40，短姓名关闭自动缩放，水平居中 |
| `portrait_love_event` | `Message Root/Message Text` | 行距从原 28.8 减 15.8 至 13，文本锚点上移 9 Unity 单位 |
| `home_talk` | `Bottom/SerifWIndow/Serif` | 字号为原 48 的 0.875 倍，即 42；锚点上移 12 Unity 单位 |
| `home_talk` | `Bottom/NameHeader/Name` | 绘制前重绑字体；局部宽度不足 155 Unity 单位时重建布局 |
| `home` | 两个 `*/Button/Rank` | 关闭自动缩放，按 148 单位可用宽度收缩；基准字号各为 52／50 |
| `card_support` | `Card/CardSupport/Label` | 字号 40、字距 −4、文本框 156×40、锚点纵坐标 −34，保持原横坐标 |
| `costume_preview` | 服装预览「隐藏 UI」「动作」 | 以父按钮宽度的 90% 水平居中，字号上限 40，按文字实际宽度缩小 |
| `costume_preview` | 换装页「隐藏 UI」「动作」「穿戴」 | 按 CharacterCostumeScreen 独立完整路径匹配，以父按钮宽度的 90% 水平居中，字号上限 40 |
| `shop` | 商店「新品上架」 | 保持原文本框尺寸与位置，框内居中；字号上限 48，按原框宽与初始父宽 85% 中的较小值适配 |
| `item_thumbnail` | 礼物／商品缩略图的「地面」「墙面」 | 原框 80.01×57.93，位置、对齐及边距不变；字号上限 32，使用原框宽度的 90% 适配 |
| `daily_roulette` | 转盘 `RouletteDrawButton/DescriptionLabel` | 原字号40、文本框712×40；仅独立材质的白描边向外扩展，字色、字号、位置不变 |
| `generic_ui` | 其他控件 | 不调整原字号或布局 |

插件“修改字体设置”仅显示当前模式下有效的可调项。界面移除“首页等级”“首页公司等级”两组及 HomeTalk 人名的“绘制前同步字体”开关；HomeTalk 人名的“重排宽度阈值”仍可调整。旧字段继续保留在内部配置及保存文件中，兼容已有用户参数和旧字体模式；预览、保存不删除这些字段，也不改变其他已保存参数。

`card_support_label` 在 `replace-all` 和 `replace-ui` 两种字体替换模式均应用；主页纯 Latin `Rank` 的旧缩放只适用于 `replace-all`。

`*_unity` 是 Unity TextMeshPro 字号、间距、RectTransform 尺寸或锚点单位；`font_size_ratio` 乘原字号。描边、字面扩张与 RGBA 是 TMP 材质属性的归一化值。配置不用截图像素。`rect_size_unity` 的守卫为 `[宽, 高]`，其余二元守卫为闭区间。运行时应沿用当前 C++ 的约 1 Unity 单位浮点容差，并在布局变更前读取原始值，避免每次绘制重复叠加 `*_delta_unity`。目前文档中的 HomeTalk 12 单位定位仅获用户画面目测确认，尚无同帧像素测量；配置保留已接受的现状。

`360°LIVE` 拉丁锁定、消息选择按钮的局部字体纵向度量、全局 TMP 图集/材质绑定与字形覆盖预检属于独立规则，不由此配置控制。普通剧情材质必须继续使用独立副本，防止一处描边调整影响其他 UI。

## 运行时编辑

普通剧情字幕支持单独调整 `font_size_unity`（1–200）与 `character_spacing_unity`（−100–100），正字间距拉开字符、负值收紧。按原始字号和材质识别字幕后，以控件弱引用记录身份；改变字号不会使后续预览失去匹配。字号与间距直接赋值，不逐帧累加；显式字号关闭字幕自动缩放。旧保存文件只补入这两个默认值，其余已有调整保留。普通剧情人名不提供这两个选项。

游戏内展开「IP」浮窗，进入「调整游戏字体」。选择已识别场景和参数后先预览；面板会关闭，便于查看游戏画面。再次进入可保存、取消本次预览或恢复默认。保存的配置位于游戏外部文件目录的 `idoly-localify/font-profiles.json`，下次启动会校验并载入；校验失败保持内置基线。运行时在 Unity 主线程按配置代数更新控件，从弱引用记录的原始字号、行距和锚点重算，避免反复应用增量。故事字幕与人名使用独立材质副本，调整其中一个描边不会修改共享原材质。

当前十九个 profile 只覆盖上表控件，字体预览对未匹配的按钮或未知游戏版本没有作用。修改配置后应在对应控件核对命中、预览与恢复默认效果。

三个商店／服装控件只匹配实测完整页面路径及原字号、尺寸。服装按钮渲染前依据父 RectTransform、子锚点与 pivot 计算居中，保持原纵坐标及高度。商店气泡由游戏控制布局与动画，不修改文本框尺寸或锚点位置，字号预算使用首次有效的原始宽度，避免父子尺寸反馈。横向边距对称保留；字号按配置代数、文本或可用宽度变化重算并缓存，不逐帧累乘。`content_width_ratio` 可热预览，范围 0.1–1。父容器无效、过小或变换不符时跳过。调整后可在相应页面预览字号与对齐，或恢复默认值。

配置现在共十九项。此前完整十六项配置会补入三个 `costume_selection_*` 默认值；此前完整十五项配置还会补入 `daily_roulette_description`。其余结构与已有调整均严格验证，缺少其他必需项的配置仍会被拒绝。物品标签只匹配已采集的 ShowcaseToyThumbnail 层级、原字号与尺寸，且仅调整中文「地面」「墙面」，不按这两个词全局修改字号。


## 正式字体契约

`Localify-ResourceHanRoundedSC-ExtraBold` 和 `resource-han-rounded.bundle` 是沿用的内部资源标识，用于兼容已有配置和旧版修补包；实际字体为 `config/font-source.json` 指定的 Noto Sans CJK SC Bold。

界面文字默认整段使用完整 Noto Sans CJK SC Bold，包括英文、玩家昵称与标签；纯数量与等级数字（包含货币千分位逗号）、商品售价整组（含币种符号与单位）、指定装饰标题及 Venus／事务所 Rank 下方等级保留游戏原字体，Rank 标题及好友列表昵称与等级整行仍用替换字体，具体规则由 `app/src/main/cpp/ui_font_policy.h` 管理。旧 `preserve-non-cjk` 仍兼容并配对 `add-font`。运行时 `replace-ui` 配对 `module-font`，并兼容旧 `add-font`，Unity Font 名为 `Localify-ResourceHanRoundedSC-ExtraBold`。正式 pipeline 与内置词典采用该模式；APK 构建器在构建前及签名结果验证时检查模块内置词典与载荷模式。旧 `fallback`／`replace-translated`／`replace-all` 模式仍可显式使用对应 `replace-fot` 载荷。

新模式在 TMP `SetArraySizes(UnicodeChar[])` 查找字形前按真实字符缓冲选择字体，避免格式化文本的空字符串 getter。普通 Unity UI Text 按翻译后的 setter 参数选择字体。选择消息按钮独立使用共享完整替换字体atlas 的静态字体副本，其纵向 FaceInfo 按原字体 pointSize 比例归一化；字形与英文标点仍全部来自替换字体，其他控件的 FaceInfo 不变。

旧版保存的 profiles 若字体元数据恰为 `preserve-non-cjk`／`Localify-ResourceHanRoundedSC-ExtraBold` 或 `replace-all`／`FOT-UDKakugo_LargePro-DB`，原生校验器只迁移此元数据；所有 schema、控件选择器、guard 和 adjust 仍严格校验，合法的用户调整值保留。其他旧或未知元数据不自动接受。主页 Rank 的旧替换字体字号补偿参数仅旧 `replace-all` 模式使用，新模式的纯 Latin Rank 保留原字体布局。HomeTalk 的旧 `rebind_font_before_render` 开关也保留供旧模式使用；新模式统一在实际字符缓冲的字形查找入口选择字体，无需该开关。

留言板列表的单行标题有额外的字体兼容规则：仅识别 `ForumThreadListCell(Clone)/Button/Title` 的完整场景路径、740×49 Unity 单位和原字号48，且省略／截断模式下实际替换字体首行过高、原字体首行能容纳时，独立规范该控件的字体纵向度量。原字号、文字框和剧情字体度量不变。按控件保留共享替换字体图集的静态副本，文本改变时检查本体字形表，按需预暖新字形及省略号，再确认覆盖完整；不以 `TryAddCharacters` 返回值判断覆盖（全部字形已存在时也可能返回 false）。控件销毁或恢复原字体后释放副本；不按玩家帖子内容建立永久缓存。这是受控的渲染兼容规则，不是玩家文本翻译规则。

UGUI 控件在字形查找阶段切换字体或共享材质后，会记录待提交状态。原 `OnPreRenderCanvas` 完成后，若仍待提交或主渲染器图集与当前材质不符，调用 TMP 自身的 `UpdateMaterial` 同步材质及纹理，避免网格使用新字体、主渲染器仍绑定旧图集。恢复明确保留控件的原字体也走相同流程；提交失败或图集仍不匹配时保留重试状态。状态随控件弱引用管理，不手动改写 shader 或纹理，也不应用到 3D TMP。


## 每日转盘外描边

`daily_roulette_description` 仅匹配完整 `WindowSystem/WindowRoot/RouletteWindow(Clone)/RouletteTopScreen(Clone)/ScreenCanvas/ScreenRoot/TopScreen/RouletteDrawButton/DescriptionLabel`、原字号39–41与712×40文本框。`outline_outward` 默认开启：隔离当前替换字体材质，保留描边宽度和颜色，以该宽度等量补偿 `_FaceDilate`，使描边内缘退回原字面边界。关闭时还原隔离副本的原始字面扩张值。重复启用、材质重绑及预览均从未补偿的基准重算，不累加；超出可表示范围时保持当前材质。该规则不调整普通剧情或全局 UI。有限数字日志记录字号、尺寸、描边及字面扩张前后值，不输出正文。

BGM 商店、展示柜标签与好友昵称遵循默认替换字体策略，无需逐个增加英文白名单。好友昵称及等级整行统一替换字体；其余纯数量与等级数字保留原字体；其余原字体例外按明确控件路径管理。生成方式见[字体选择与渲染](font-evaluation.md)。
