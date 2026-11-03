# Idoly-localify

《IDOLY PRIDE／偶像荣耀》日服 Android 简体中文本地化插件。提供界面与剧情文本替换、MasterDB 本地化、公告翻译、运行时字体和图片替换，以及文本在线更新。

[下载最新插件](https://github.com/DreamGallery/Idoly-localify/releases/latest) · [下载修补后的游戏](https://github.com/DreamGallery/Idoly-Patcher/releases/latest) · [最新文本](https://github.com/DreamGallery/Idoly-localify-translations/releases/latest)

## 安装与使用

- **LSPosed／Vector**：在兼容的 root 设备启用模块，作用域仅选择游戏；使用原版游戏 APK。
- **LSPatch**：将模块嵌入游戏安装包。字体与汉化图片由模块在运行时加载，保留游戏原始资源。
- 通用字体为 Noto Sans CJK SC SemiBold，若恋对白和按钮采用 Resource Han Rounded SC Bold；指定数字、价格等控件保留原字体。
- 悬浮窗中的详细设置可更新文本、调整字体、替换剧情，也可关闭翻译或清理本地文件。翻译开关在重启游戏后生效。
- 「额外功能」提供[实验性内容预览与拍摄](docs/content-preview.md)：临时试用外观、替换 3D 角色模型，以及查看部分限时和联动拍摄内容。不增加账号持有物品；正式保存照片前请阅读功能说明。
- 「额外功能」提供高级画质和自定义服务器连接；「账号与数据」可备份和导入账号，默认保存到系统下载目录。使用方式及限制见[功能说明](docs/account-and-connection.md)。

当前构建目标为 Android ARM64；Windows MuMu Android 12 可通过 ARM64 转译使用 LSPatch 版。其他模拟器取决于其兼容性。开发依赖和编译命令见[构建说明](docs/build.md)。

## 项目结构

| 路径 | 内容 |
| --- | --- |
| `app/` | Android 模块、原生钩子与运行时界面 |
| `config/` | 字体、控件与图片处理规则 |
| `image-patches/` | 已审核的图片编辑区域与来源说明 |
| `tools/` | 文本编译、检查、打包及部署工具 |
| `docs/` | 构建、文本维护与功能说明 |

译文和校对数据维护在 [Idoly-localify-translations](https://github.com/DreamGallery/Idoly-localify-translations)。运行时图片保存在 `app/src/main/assets/idoly-localify/images/`，随插件发布；本机的 `translations/`、下载缓存、生成字典、字体文件及测试 APK 不随代码仓库提交。两种字体均在构建准备阶段按配置下载官方固定版本、校验并缓存；许可证位于 `app/src/main/assets/licenses/`。

配套工具独立维护：[HoshimiToolkit](https://github.com/DreamGallery/HoshimiToolkit) 用于资源获取与脚本转换，[Hoshimi_Teleprompter](https://github.com/DreamGallery/Hoshimi_Teleprompter) 用于模型翻译。路径可配置，见[文本流程](docs/text-pipeline.md)。

## 文档与反馈

- [文本流程](docs/text-pipeline.md) · [在线更新](docs/hot-update.md)
- [字体配置](docs/font-profiles.md) · [图片汉化](docs/image-localization.md)
- [卡片剧情替换](docs/card-story-swap.md) · [活动剧情替换](docs/event-story-swap.md)

在[翻译仓库 Issues](https://github.com/DreamGallery/Idoly-localify-translations/issues)反馈漏翻或显示问题。插件采集文件仅保存本机，提交前请自行检查个人信息。自动抓取、翻译、校对导入与发布的 GitHub Actions 均放在翻译仓库。

## 许可

方案参考 [gkms-localify](https://github.com/imas-tools/gkms-localify)。代码采用 [GPL-3.0](LICENSE)；游戏资源、字体和第三方组件遵循各自许可。

## 更新日志

<details>
<summary>0.7.0 · 账号备份、画质与连接设置</summary>

- 新增「账号与数据」，支持加密登录备份、完整数据快照和可选照片，默认导出到系统下载目录。
- 新增目标帧率、渲染倍率、FXAA／SMAA 设置和恢复游戏默认。
- 可连接兼容的自建 gRPC 服务，使用独立凭据；插件本身不提供离线服务器。

</details>

<details>
<summary>0.6.0 · 拍摄与模型替换</summary>

- 扩展实验性拍摄和模型替换，支持服装、发型、饰品及部分限时、联动内容。
- 统一设置页、下拉框与过渡动画，增加外观缩略图和按组合排列的角色列表。
- 优化剧情替换目录的读取与筛选，修正「自动拍摄」译文。

</details>

<details>
<summary>0.5.0 · 实验性预览与布局修正</summary>

- 新增服装、发型／发饰、姿势和名片装饰的分类预览，支持在本次运行中保留替换外观。
- 改善自适应文字按钮的尺寸和对齐，补充界面译文。
- 将实验性功能与翻译开关分开，完善原生模块状态及文本更新错误提示。

</details>

<details>
<summary>0.4.0 · 运行时图片与动态剧情目录</summary>

- UI 汉化图片随插件在运行时替换，LSPosed 与 LSPatch 共用；新增翻译开关、译文重置和采集清理。
- 若恋对白、姓名和按钮统一为 Resource Han Rounded SC Bold，调整标题及多处界面布局。
- 卡牌、普通活动和特殊活动的剧情目录随文本更新，支持后台读取、校验与失败恢复。
- 整理字典加载边界；MasterDB 采用稳定记录及嵌套条目标识，减少数据重排造成的错配。

</details>

<details>
<summary>0.3.0 · 字体与更新稳定性</summary>

- 通用字体改为 Noto Sans CJK SC SemiBold，改善普通剧情描边及重复进入后的字幕显示。
- 修正文字缺失、自动缩小和错位，完善纯数字、价格、符号的原字体保留规则。
- 修复若恋姓名、头像及配色，补充界面译文和配套汉化图片。
- 提供固定文本入口与新版插件自动修补，修复升级后旧字典配置导致的更新校验失败。

</details>

<details>
<summary>0.2.0 · 运行时字体与剧情替换</summary>

- 字体随模块在运行时加载，整理文本、公告、MasterDB 与剧情的更新流程。
- 新增插件和文本版本检查、当前版本显示，以及普通剧情字幕字号和字间距调整。
- 剧情替换同步标题与登场人物资料，修正人工标题格式匹配。
- 修复 Windows MuMu ARM64 转译环境中的初始化问题，开始采用 Noto Sans CJK SC 字体。

</details>

<details>
<summary>0.1.0 · 本地化基础</summary>

- 接入界面固定键、普通文本与剧情脚本替换，建立人名词典及脚本转换流程。
- 提供简体中文字体处理、原文采集和本地翻译加载。

</details>
