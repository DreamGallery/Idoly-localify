# Idoly-localify

《IDOLY PRIDE／偶像荣耀》日服 Android 简体中文本地化插件。提供界面与剧情文本替换、MasterDB 本地化、公告翻译、运行时字体及文本在线更新。

## 使用与构建

- **LSPosed／Vector**：在兼容的 root 设备启用模块，作用域仅选择游戏；使用原版游戏 APK。
- **LSPatch**：将模块嵌入游戏安装包。字体由模块在运行时加载，无需修改游戏字体资源。
- 默认字体为 Noto Sans CJK SC SemiBold，指定数字、价格等控件保留原字体。图片汉化为可选功能。

当前构建目标为 Android ARM64，也支持 Windows MuMu Android 12 的 ARM64 转译环境（LSPatch）。构建插件需要 Android SDK、NDK、JDK 17+、Python 3.12+；使用 LSPatch 修补游戏需要 JDK 21+。具体依赖和命令见[构建说明](docs/build.md)。

## 项目结构

| 路径 | 内容 |
| --- | --- |
| `app/` | Android 模块、原生钩子与运行时界面 |
| `config/` | 字体、控件与图片处理规则 |
| `tools/` | 文本编译、检查、打包及部署工具 |
| `docs/` | 构建、文本维护与功能说明 |

译文和校对数据维护在 [Idoly-localify-translations](https://github.com/DreamGallery/Idoly-localify-translations)。本机的 `translations/`、下载缓存、生成字典、字体及测试 APK 不随代码仓库提交。

配套工具独立维护：[HoshimiToolkit](https://github.com/DreamGallery/HoshimiToolkit) 用于资源获取与脚本转换，[Hoshimi_Teleprompter](https://github.com/DreamGallery/Hoshimi_Teleprompter) 用于模型翻译。路径可配置，见[文本流程](docs/text-pipeline.md)。

## 文档与反馈

- [文本流程](docs/text-pipeline.md) · [在线更新](docs/hot-update.md)
- [字体配置](docs/font-profiles.md) · [图片汉化](docs/image-localization.md)
- [卡片剧情替换](docs/card-story-swap.md) · [活动剧情替换](docs/event-story-swap.md)

在[翻译仓库 Issues](https://github.com/DreamGallery/Idoly-localify-translations/issues)反馈漏翻或显示问题。插件采集文件仅保存本机，提交前请自行检查个人信息。自动抓取、翻译、校对导入与发布的 GitHub Actions 均放在翻译仓库。

## 许可

方案参考 [gkms-localify](https://github.com/imas-tools/gkms-localify)。代码采用 [GPL-3.0](LICENSE)；游戏资源、字体和第三方组件遵循各自许可。
