# 文本本地化流程

## 独立仓库与路径

工具仓库可使用任意目录名和位置。环境变量 `IDOLY_TOOLKIT_DIR`、`IDOLY_TRANSLATOR_DIR` 分别指向 Toolkit 和 Teleprompter 根目录；省略时兼容现有同级目录布局。跨项目 Python 模块按各自命名空间加载，不将通用 `src` 包加入搜索路径。

剧情输入、编辑 CSV、合并 TXT 和图片输出分别可用 `IDOLY_STORY_SOURCE_DIR`、`IDOLY_STORY_CSV_DIR`、`IDOLY_STORY_OUTPUT_DIR`、`IDOLY_ASSET_OUTPUT_DIR` 设置。默认使用 Toolkit 的 `cache/` 子目录。环境变量中的相对路径按调用目录解析；自动化建议使用绝对路径。

`pipeline.py` 还提供 `--toolkit-dir`、`--translator-dir`、`--source-dir`、`--csv-dir`、`--output-dir`；命令行参数优先于环境变量。其他采集/翻译工具通过自身输入输出参数共享文件，无需复制到固定的相邻仓库。


命令默认从本地化项目根目录运行。HoshimiToolkit 与 Hoshimi_Teleprompter 放在主项目的同级目录；它们各自管理缓存、接口配置与独立命令。主项目的 `translations/` 保存审校结果，`tools/` 负责组合、验证和部署这些结果。

## 资源与剧情 CSV

在 HoshimiToolkit 中配置当前游戏运行时的 Octo 参数，按其 README 获取资源。联合使用时可执行：

```sh
(cd ../HoshimiToolkit && python3 main.py --source api --prefix adv_ \
  --download-type resource --no-images)
python3 tools/pipeline.py status
python3 tools/pipeline.py export
```

`status` 只读检查 TXT 与 CSV，报告新增、过期、孤立或损坏文件并给出下一步命令。`export` 为新增 TXT 创建 CSV，不覆盖现有校对。原 TXT 与 CSV 分别位于 HoshimiToolkit 的 `cache/resource/adventure/` 和 `cache/csv/`。

已有 TXT 更新后，运行 `python3 tools/pipeline.py refresh`。工具按字段 ID、原文和说话人迁移译文；行号变化时，仅迁移仍能唯一对应的字段。无法迁移的译文写入 `.analysis/rebase-report.json` 并停止。审阅后可用 `refresh --accept-dropped` 备份旧 CSV，并将变动字段留空待重译。

剧情 CSV 仅编辑 `trans` 列。`id`、`name`、`text`、源文件哈希和译者元数据保留。模型翻译命令如下，先用 `plan` 查看待译量，再显式执行 `translate`：

```sh
python3 ../Hoshimi_Teleprompter/main.py plan \
  --csv-dir ../HoshimiToolkit/cache/csv \
  --source-dir ../HoshimiToolkit/cache/resource/adventure \
  --glossary translations/glossary/names.json \
  --term-glossary translations/glossary/terms.json
# 将上面的 plan 改为 translate 执行模型翻译。
```

接口配置来自翻译器自己的 `.env` 或环境变量。`--file`、`--prefix` 可限定范围；主项目的人名和术语审校表通过参数显式传入。姓名不作为单独翻译行，构建 TXT 时合并人名表；若恋（`adv_love_*`）保留用于角色识别的原始 `name=`，由插件在显示时翻译姓名，保留头像和姓名框配色。

脚本中的 `{user}`、富文本标签和字面 `\n` 必须保留。游戏会把字面 `\n` 渲染为换行，不应改成脚本值中的实际 CRLF。

## MasterDB JSON

HoshimiToolkit 的 `master_fetch.py` 负责获取 MasterTag、下载并解密原始表，依赖 SolisClient 的 protobuf 定义与 SQLCipher 驱动。主项目的 `tools/fetch_master.py` 是兼容入口。原始表和认证文件留在本地缓存中。

将 `MASTER_DIR` 设置为已解密原始表目录，导出审校 JSON：

```sh
python3 tools/master_json.py export --input-dir "$MASTER_DIR" --prune-missing
python3 tools/master_json.py plan
python3 ../Hoshimi_Teleprompter/main.py master-plan \
  --orig-dir translations/master/orig --zh-dir translations/master/zh-Hans \
  --log-file translations/master/working/results.jsonl
```

模型翻译时将 `master-plan` 改为 `master-translate`，并添加 `--glossary translations/glossary/names.json --term-glossary translations/glossary/terms.json`。

原文与译文分别位于 `translations/master/orig/`、`translations/master/zh-Hans/`，按 `表名 → 记录 ID → 字段路径` 对应。原文未改变时，重新导出会保留已有修订。人名使用统一词典，音乐制作人员署名可以保留日文；译文与原文相同表示有意保留，空值表示待处理。

MasterDB 二次校对由 Teleprompter 生成建议，报告先在主项目本地生成：

```sh
python3 tools/audit_master_json.py
python3 ../Hoshimi_Teleprompter/suggest_master_kana.py \
  --report .analysis/reports/master-review.json \
  --orig-dir translations/master/orig --zh-dir translations/master/zh-Hans \
  --name-glossary translations/glossary/names.json \
  --term-glossary translations/glossary/terms.json \
  --output .analysis/master-kana-suggestions.json \
  --log .analysis/master-kana-suggestions.jsonl
```

建议供人工审阅，不自动回写译文。

## 界面与公告

界面键词典为 `translations/ui/source.json` 和 `translations/ui/zh-Hans.json`。新日志可用 `tools/localize.py collect` 收集稳定 i18n 键；同一键的原文变体需审阅后写入 `translations/ui/variants-source.json`。运行日志可能包含玩家文本，保存在 Git 忽略目录中。

重复的数字句式可在这两份 UI JSON 的 `numeric_text_templates` 下成对维护：原文文件把模板映射到自身，译文文件把同一模板映射到简中。例如 `{number}日目のログインボーナスです` → `这是第{number}天的登录奖励`，会覆盖第 2 天、第 8 天及以后同句式。编译器要求两边键完全一致、各只有一个 `{number}`、无其他占位符；运行时仅对整句匹配的 1–6 位 ASCII 数字替换，保留原数字。全角数字、用户名、额外标点和模糊相似句不会命中。已生成中文由同一 matcher 识别，以继续使用中文字体，且不再作为未译文本采集。新增模板须先有实机原文证据；它不是任意数字句子的通配翻译。

界面机器翻译入口已移至 Teleprompter，使用它的接口配置与提示词：

```sh
python3 ../Hoshimi_Teleprompter/translate_ui.py \
  --source translations/ui/source.json --translations translations/ui/zh-Hans.json \
  --variants-source translations/ui/variants-source.json \
  --glossary translations/glossary/names.json
```

加入 `--variants-only` 可处理同键原文变体。运行时 Text 仍需先由 `tools/runtime_text_review.py` 生成、人工审阅并 `apply` 清单，再在上述翻译命令中加入 `--text-only --text-candidates .analysis/runtime-text-candidates.json --text-review .analysis/runtime-text-review.json`。翻译器只发送标记为需要翻译且尚无译文的项目。

活动的“玩法”入口可能打开官方公告网页，也可能打开结构化帮助。公告入口先使用 HoshimiToolkit 的 `notice_fetch.py official-index` 获取当前索引，再以 `archive-pages --input cache/notice/official-index.json --refresh` 更新这些页面，使用 `tools/web_notice.py import-pages --pages <页面源文件> --index <官方索引>` 增量导入；不需要逐页在模拟器采集。该抓取命令虽然名为 `archive-pages`，范围由输入索引决定，当前索引只覆盖首页窗口。

结构化帮助、FAQ 和 Tips 可用 HoshimiToolkit 的 `help_fetch.py` 从官方 `Master.GetHelpCategory` 匿名读取。复用已有 MasterDB 译文前，按分类 ID 和子条目的 `helpContentId` 对齐并比较原文；接口数组次序可能不同，不能直接覆盖按 `contents[n]` 定位的现有翻译。具体参数见 Toolkit 的帮助采集文档。

公告网页原文独立维护在 `translations/notice/source.json`，译文为 `translations/notice/zh-Hans.json`。`tools/web_notice.py` 提供页面采集、导入和编译命令，具体参数见 `--help`。机器初译先生成审阅清单：

```sh
python3 ../Hoshimi_Teleprompter/main.py notice-plan \
  --source translations/notice/source.json \
  --translations translations/notice/zh-Hans.json \
  --ui translations/ui/zh-Hans.json \
  --log-file translations/notice/working/results.jsonl \
  --output .analysis/web-notice-pending.json
```

审阅后将 `notice-plan` 改为 `notice-translate`，将 `--output` 改为 `--review-list`，并传入主项目的两份词典。清单绑定源文件哈希；原文更新后需重新生成。插件按公告页面和原文精确替换网页节点，图片内文字不包含在此流程中。

公告入口标题（包括网页 H1 和索引 title/listTitle）只有在各页面译文一致时才加入通用映射。同名异译默认保留各页正文、跳过通用入口映射；构建报告位于 `.analysis/notice-title-conflicts.json`。如需统一入口用语，在 `translations/notice/title-resolutions.json` 保存显式决议：`schema_version: 1`，`resolutions` 按原文索引，每项包含选用的 `translation`、`review_method`（`human` 或 `assistant`）、`reason` 和 `candidates`（所有相关页面路径到各自译文的字典）。决议必须选用已有候选；页面或译文变化后会拒绝过期决议，需重新审阅，不按文件顺序覆盖。入口决议不更改网页正文译文。

首页工作槽的“名称 Lv等级”使用 UI JSON 中独立的 `home_activity_names`：来源文件按原名映射自身，译文文件维护对应中文名称，例如 `サイン会` → `签名会`。仅首页 `HomeTopScreen/ActivityPage/.../ActivityCells` 下 `FanEvent`、`Promotion`、`Refresh` 三个 `NameBase/Name`，以及工作中 `FanEventScreen`、`PromotionScreen`、`RefreshScreen` 的 `ActGameFooter/ActGameInfo/Level` 完整控件路径可使用，保留 ` Lv` 和 1–6 位 ASCII 等级数字。此规则不会加入通用文本词典，也不放宽全局数字模板的匹配门槛。名称不接受空白、控制符或占位符，最多 64 项、每个名称最多 64 UTF-8 字节；拒绝会重复翻译的名称链。旧词典缺少此字段时按空规则处理。只有文本、没有控件路径的历史日志仍标记为待确认，不因此宣称已覆盖。

粉丝活动排行榜会把原始活动名填入已翻译的标题句式。`FanEventRankingSheet/Canvas/Root/UIContentArea/SheetContainer/Header/Description` 使用专门的名称匹配：仅处理握手会、摄影会、签名会，读取当前 `fanEvent.ranking.sheet.description` 与 `common.group.prefix` 译文模板，要求名次范围和小组编号均为1–6位 ASCII 数字。名称沿用现有精确词典，数字及前导零原样保留，不改排行榜中的玩家昵称。

## 构建与校验

```sh
python3 tools/pipeline.py build
python3 tools/pipeline.py verify
```

构建生成剧情 JSON 补丁、合并 TXT、剧情标题映射、MasterDB 文本映射与按记录索引、最终运行时词典及公告词典。合并 TXT 保存在 HoshimiToolkit 的 `cache/local-files/resource/`，补丁和审校 JSON 保存在主项目；本机构建清单与冲突报告位于 `.analysis/`。

卡片显示文本由 `card_display_aliases.py` 结合 Card 表、剧情目录中的角色对应关系和姓名词典生成完整匹配项，覆盖“卡名　姓名”及末尾带 `　★1` 至 `　★5` 的扭蛋记录，也接受卡名已翻译而姓名仍为日文的状态。未知卡片关系不猜测，关系或译文冲突会停止生成；不会对任意文本做姓名子串替换。更新卡名或姓名后运行同一构建流程即可重新生成。

构建先暂存并校验，再检查输入没有被并行编辑，最后逐文件发布。`verify` 只读核对源文件、译文和输出的一致性。缺少 CSV、源文件版本不符、占位符损坏或孤立数据均会导致失败；根据 `status` 的提示修复后重跑。

运行时先按界面键和原文、MasterDB 记录字段、剧情脚本或公告页面匹配。通用原文映射中，已审核的界面 `text` 是明确覆盖；其余派生来源同文异译时不按读取顺序强选，冲突记录在 `.analysis/runtime-text-conflicts.json`。`tools/audit_master_runtime.py` 和 `tools/audit_runtime_coverage.py` 可辅助检查映射，日志命中率不代表整个游戏的翻译覆盖率。

检查页面漏译时，同时核对标题、标签和说明，不能只处理商品名。旧 `runtime_capture.py` 的增量文件只包含本次新增日志；游戏日志会去重，因此以前记录过但尚未翻译的原文可能不在增量里。用完整本地日志重新运行覆盖审计，或用浮窗开启独立采集，并结合布局记录和截图确认实际可见内容。已有 `i18n` 译文也不代表直接写入 TMP 的同文标签会命中；确认语义一致后可在界面 `text` 中增加精确映射，避免全局合并所有界面键。占位模板、账号数值和领取日期不进入固定翻译词典。

## 本地热更新发布目录

`pipeline.py build/verify` 只编译和校验本地文件，不会刷新游戏资源或下载最新译文。本地测试前应核对最新发布的 `manifest.json`，同步公告源、译文和已完成的协作内容，再叠加本次修改；公告列表标题来自官方公告索引及网页标题，不由 Octo 清单单独提供。正式发布的 `refresh_sources.py` 会分别刷新 Octo、MasterTag 和公告索引。

`deploy_text.py` 会比较设备现有词典，遇到本地字典丢失已有译文时停止部署，并将差异保存到 `.analysis/text-deploy-dictionary-diff.json`。先同步缺失数据；仅在确认是有意撤回或已被新版发布淘汰的映射时使用 `--allow-dictionary-removals`。不要用直接覆盖字典文件的方式跳过这项检查。

确认 `python3 tools/pipeline.py verify` 通过后，可生成供静态 HTTPS 主机托管的目录：

```sh
python3 tools/build_text_update.py \
  --base-url https://example.org/idoly/text-update
# 如需同时发布全部剧情 TXT：
python3 tools/build_text_update.py \
  --base-url https://example.org/idoly/text-update --include-stories
```

工具会再次执行完整 `pipeline.py verify`，默认只从已编译产物复制 `dict.json` 和可选的 `web-notice.json`；`--include-stories` 会追加全部已验证的 `resource/adv_*.txt`，不截取部分剧情。`--omit-notice` 可只发布界面词典。工具逐份检查 UTF-8、文件名、大小与 SHA-256，并限制总量。默认输出为 `.analysis/text-update-release/`；其 `manifest.json` 包含显示版本、文件的绝对 HTTPS URL 和 SHA-256。文件按内容哈希放在 `objects/` 下，全部就绪后才原子更新 manifest。可用 `--version` 指定显示版本，否则使用内容哈希。将整个目录原样上传到 HTTPS 静态主机，并确保上述 URL 返回文件字节（允许 HTTPS 重定向）；发布工具本身不上传文件。在游戏内展开「IP」浮窗，填写托管后的 `manifest.json` URL。

正式编译词典使用 `replace-ui`，与 APK 的 `add-font` 载荷配对。修改字体路由模式需安装对应字体 APK 并重启，文本构建不会把正式模式重新设为旧 `replace-all`。字体构建契约见[构建与部署](build.md)。

此发布目录仅含已选的运行时文本词典及剧情 TXT，不包含 MasterDB Blob、字体、APK、账号配置、日志或密钥。当前热更新不会替换字体或插件代码；这些变化需要安装新包。

## MasterDB 记录补丁

按记录替换可以保留相同原文在不同字段中的译法。准备与游戏版本一致的 MasterTag 和 SolisClient 后执行：

```sh
python3 tools/master_blob_patches.py \
  --master-tag /path/to/master-tag.pb --solis-dir /path/to/SolisClient --all-tables
python3 tools/master_blob_patches.py \
  --master-tag /path/to/master-tag.pb --all-tables --verify
```

生成器核对原始记录、字段路径和审校原文，只替换有变化的译文。首次运行会下载所需表；缓存完整后可添加 `--offline`。输出 `.analysis/master-blobs.bin` 含原始记录字节，仅保存在本机。插件按完整原始记录字节匹配，游戏数据库更新后需重新构建。

## 部署与完整检查

先通过文本校验，再对明确指定的测试模拟器同步：

```sh
export ADB="$HOME/Library/Android/sdk/platform-tools/adb"
python3 tools/deploy_text.py plan --serial "$ANDROID_SERIAL"
python3 tools/deploy_text.py apply --serial "$ANDROID_SERIAL" --expected-avd "$IDOLY_TEST_AVD" --restart
python3 tools/deploy_text.py verify --serial "$ANDROID_SERIAL"
python3 tools/deploy_master_blobs.py apply --serial "$ANDROID_SERIAL" --expected-avd "$IDOLY_TEST_AVD" \
  --master-tag /path/to/master-tag.pb --all-tables --restart
```

将序列号和 AVD 名称换成实际测试设备。部署脚本会校验文件哈希，暂存成功后再替换；游戏重启后载入新文件。测试包的构建与安装见[构建与部署](build.md)。

`python3 tools/check_workspace.py` 使用当前解释器运行四个代码项目的测试、MasterDB 完整性检查、文本产物验证与提交内容检查。需要在同一 Python 环境安装 Toolkit 的依赖。可追加 `--master-tag` 检查记录补丁，追加 `--serial` 检查设备文件；`--live-runtime --expected-avd` 可进一步检查游戏实际加载状态。

`python3 tools/check_repository_contents.py` 只读检查三个代码仓库的提交候选，排查本机配置、缓存、测试包等。`--repo /path/to/repo` 可检查单个仓库。


### 商品文本与动态格式

静态 MasterDB 的 `StoreProduct` 主要保存商品 ID、价格和数量；动态接口的 `ShopInfo.name` 与 `ShopItem.name/description` 也会提供商品文案。优先复用 `Item` 等表中已有的名称和介绍译文；动态礼包名由运行时采集补入 UI 字典。不要根据 MasterDB 的待译数推断商店界面覆盖率。

HoshimiToolkit 的 `shop_fetch.py` 可获取当前采集账号返回的全部商店及商品列表，并记录逐店请求状态。用下面的离线审计检查编译词典是否覆盖快照：

```sh
python3 tools/audit_shop_catalog.py \
  --catalog /path/to/shop-snapshot.json \
  --dictionary app/src/main/assets/idoly-localify/dict.json \
  --output .analysis/shop-coverage.json \
  --pending .analysis/shop-pending.json
```

审计以商店 ID、商品 ID 和字段组成稳定身份，重复相同字段去重、不同原文报冲突；只认可正式运行时精确词典及已批准的整句数字模板。报告包含输入 SHA，退出码 0 表示本次完整快照无待译，1 表示仍有待译，2 表示快照不完整。待译文件仅供审阅，不直接合入字典。

专用采集账号的目录受等级、活动等条件影响，不等同于玩家当前目录。还需在实际账号打开商店，开启插件采集，依次选中页面内第二件及后续商品，并滚到底部检查名称和介绍；验证导出完整性后，把目录外的实际原文补入 UI 词典并保存来源证据。横幅第一件已翻译不能作为整页通过的依据，图片中的文字另行处理。

编号礼包可用 `tools/product_title_templates.py` 从明确批准的已审商品词干生成候选，物品数量可用 `tools/item_quantity_templates.py` 从批准的 Master Item ID 生成候选；两者只写报告，不直接修改正式字典。具体参数见同名工具的 `--help` 和商品模板说明文档。

### 兑换介绍的库存重置日期

运行时仅处理 `ShopWindow(Clone)` 或独立入口 `ExchangeWindow(Clone)` 下 `ExchangeDetailScreen(Clone)` 已采集的完整 `Description` 对象路径。内容必须是完整已审基础介绍、`\n在庫リセット\n`、`M/D(星期) HH:MM`；基础介绍和“在庫リセット”都从普通 `text` 字典查找非空译文。月份、日期、时分严格校验，数字保留，日文星期七值转“周一”至“周日”。未知基础介绍、额外尾巴、其他路径保持整段原文，不硬编码某日日期。复用控件的新日期是新 source；同对象重复赋已渲染值保留原 source，供热重载及 OnEnable 使用。


同包另对三个已采集商品名称完整路径（上述两个入口的兑换 `SelectItem/MaskBase/Name`、普通商店 `Upper/Header/Name`）支持完整已审 `text` 基础商品名加 `×` 与 1–6 位 ASCII 数量。路径和基础名称均需精确命中，未知商品、多个乘号、其他尾巴或其他控件保持原文；现有精确数字模板仍兼容。

数量也接受严格千位分组：首组为不以零开头的 1–3 位数字，之后为 1–2 组逗号和 3 位数字，例如 `3,000`、`10,000,000`，最大 `999,999,999`。数字和逗号原样保留；错误分组、全角数字、额外空格或尾缀不匹配。这项规则仅用于上述商品名称控件，不扩大通用数字模板的范围。

HoshimiToolkit 的 `exchange_fetch.py` 通过官方 `Exchange.List` 保存当前采集账号返回的兑换目录。静态输出包括兑换所和商品 ID、名称、介绍及资源关联，不包含余额和账号状态。使用独立审计器检查每件商品，而不是只检查第一件：

```sh
python3 tools/audit_exchange_catalog.py \
  --catalog /path/to/exchange-snapshot.json \
  --dictionary app/src/main/assets/idoly-localify/dict.json \
  --output .analysis/exchange-coverage.json \
  --pending .analysis/exchange-pending.json
```

报告分别标记精确译文、数字模板，以及限于兑换详情对象的数量名称和库存日期覆盖，退出码与商店审计一致。采集账号未开放的目录仍需在测试游戏中补充采集。通用术语 `交換所` 统一为“兑换所”，由 `translations/glossary/terms.json` 维护；历史审阅候选保留原记录。
