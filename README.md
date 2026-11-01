# Idoly-localify

《IDOLY PRIDE／偶像荣耀》日服 Android 简体中文本地化插件。提供界面与剧情文本替换、MasterDB 本地化、公告翻译、运行时字体和图片替换，以及文本在线更新。

## 安装与使用

- **LSPosed／Vector**：在兼容的 root 设备启用模块，作用域仅选择游戏；使用原版游戏 APK。
- **LSPatch**：将模块嵌入游戏安装包。字体与汉化图片由模块在运行时加载，保留游戏原始资源。
- 通用字体为 Noto Sans CJK SC SemiBold，若恋对白和按钮采用 Resource Han Rounded SC Bold；指定数字、价格等控件保留原字体。
- 悬浮窗中的详细设置可更新文本、调整字体、替换剧情，也可关闭翻译或清理本地文件。翻译开关在重启游戏后生效。
- 「额外功能」提供[实验性内容预览与拍摄](docs/content-preview.md)：临时试用外观、替换 3D 角色模型，以及查看部分限时和联动拍摄内容。不增加账号持有物品；正式保存照片前请阅读功能说明。

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
