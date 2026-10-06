# 内置按钮图片汉化

主页、扭蛋页和 360°LIVE 的部分圆形按钮文字烘焙在 `HomeAtlas`、`GachaAtlas`、`PhotoAtlas` 内，无法由运行时文本字典替换。`config/button-images.json` 维护指定按钮的译名、字号、描边和图集定位；不修改按钮的布局、点击区域或符号。

## 生成与预览

准备自己的原版 `base.apk` 和 Resource Han Rounded SC Bold 字体，使用与 HoshimiToolkit 相同的 Python 环境（UnityPy、Pillow）：

```sh
python3 tools/build_button_images.py \
  --base /path/to/original/base.apk \
  --font /path/to/ResourceHanRoundedSC-Bold.ttf \
  --output-dir .analysis/button-images
```

输出包括三张中文图集、各按钮原图与中文图、`comparison.png` 对照图及 `manifest.json` 回填清单。原图、生成图集及字体仅在本机使用，不随 Git 提交。

脚本仅重排底部文字。旧文字描边超出圆形的部分一并清理，圆形下部使用原图上半部的透明度恢复。中文白字与底色描边独立绘制，保持文字笔画宽度。生成后应在透明底和实际游戏背景上检查残影、边缘、裁切和可读性。

定位目前绑定游戏 6.0.2 的原始图集哈希。版本不符会停止；不能仅更新哈希跳过审查，应重新核对精灵名称、矩形、旋转和字体留白。此脚本只支持已经检查过的未旋转矩形精灵及内联纹理。

## 选中标签与若恋按钮

`config/ui-label-images.json` 维护横竖“已选择”标签，以及若恋的设置、礼物、状态、课题、开始、继续、退出、玩法和剧情播放页的记录、自动、快进。先生成上述圆形按钮，再合并标签：

```sh
python3 tools/build_ui_label_images.py \
  --base /path/to/original/base.apk \
  --font /path/to/ResourceHanRoundedSC-Bold.ttf \
  --merge-manifest .analysis/button-images/manifest.json \
  --output-dir .analysis/ui-label-images
```

合并输出保留圆形按钮的原 PNG，另生成 CommonAtlas、LoveAtlas、LoveADVAtlas 中文纹理、逐图预览、`comparison.png` 和 `validation.json`。构建时使用合并目录中的 `manifest.json`。

该脚本只修改配置中的文字矩形及其采样留白。玩法的 info 图标保持原像素；品牌艺术标志不在替换范围。文字使用原来的颜色与描边，短中文保留正常字高，不拉伸填满原日文宽度。设置、礼物、状态、课题的原显示高度均为 40，统一使用 36 号整句居中排字。

剧情播放页的记录、自动、快进和选项上的“已选择”白字标签统一使用 31 号字，保留独立的按钮边框、青色选中底框与菜单图标。自动、快进的开启和关闭状态共用标签图片；三个选项按钮共用“已选择”图片，配置验证全部对应的 Image 组件。

工作页三个状态标签同样位于 HomeAtlas，译为“核心粉丝率提升中”“粉丝获取中”“体力恢复中”。按原图每行两端的纯色背景恢复文字区域，保留原圆角边缘与透明度，再绘制居中文字；第一项保留两行布局。此处理绑定精灵哈希、底色与颜色容差，仅用于这三个已核对的纯色标签。

工作奖励上的红色 `noti_limited`（期間限定）来自 `CommonAtlas`，使用同尺寸的「期间限定」替换，保留粉红字面和白色描边，封闭笔画内部补白以避免透出背景，外轮廓仍保持透明。该公共素材的所有引用随图集一并更新。

两份图片配置的 `clear_padding` 指定需要更新的图集边缘宽度，当前为 2 像素。生成时按新图的边缘像素重建这圈采样留白，清除旧字残影，并保留圆形按钮边缘的透明度。仅修改文字带所接触的边缘；检查相邻精灵及其半像素采样范围，避免缩放时混入旧字或影响其他图标。

若恋原版 UI.Image 的 `m_UseSpriteMesh` 为 false，因此按完整矩形留边排字。生成时验证对应预制件的哈希、Image 序列化布局及该开关，避免误把旧日文网格孔洞作为排版边界。CommonAtlas 的横竖“已选择”标签仍检查原网格安全区域。精灵网格、矩形、布局与点击区域不变。

“第 N 话进行中”的原始图片保持不变。插件在完整 TextRoot 路径上隐藏该 Image 组件，保留子级 EpisodeText，并在其中统一绘制整句和动态序号；气泡背景保持原样。此处理不依赖图片替换清单。

该提示同时在 TMP 字形生成前检查实际数字缓冲：剧情返回时的格式化赋值可能绕过普通字符串 setter。只对完整路径匹配的 EpisodeText 重建整句，按参数类型选择 `SetText(string)` 或 `SetText(string, bool)`，避免与 StringBuilder、数值格式化重载混淆。回归检查应包括进入剧情、中断返回和重新进入若恋。

配置同时绑定原版纹理、图集、精灵与字体的哈希。合并时验证旧图片清单，生成后逐像素验证指定区域外的 RGBA 没有变化；这些检查不代替游戏内的实际背景与显示尺寸检查。

## 构建测试包

在[测试包构建命令](build.md)中额外传入：

```text
--image-patches .analysis/ui-label-images/manifest.json
```

字体由模块提供；构建按清单替换图集，再进行 LSPatch 封装和签名。会校验源资源哈希、PNG 哈希、纹理名称和尺寸、允许编辑区域、其他 Unity 对象及 APK 条目，完成后再次读取最终包中的图集核对像素。采用 RGBA32 无损回填，包体与显存占用会高于原始 ASTC 纹理。

这是 APK 内置资源替换，当前文本热更新入口不发布这些图片。只装插件而不替换对应游戏 APK 时不会生效。省略 `--image-patches` 并重新构建安装，可恢复原始按钮；原版 APK、字体和文本配置不会被此脚本覆盖。

Octo 下载图片使用 HoshimiToolkit 的 `asset_patch.py` 回封；不能将本页的 APK 图集清单当成 Octo AssetBundle。

## 悬浮工具图标

插件悬浮入口参照官网 favicon 手绘近似的 IP 字母，以蓝色圆底、白色路径显示。矢量稿为 `config/overlay-icon.svg`，来源和绘制位置记录在 `config/overlay-icon.json`；界面使用相同坐标的 Canvas Drawable 随尺寸直接绘制，不放大 48 像素位图，也不依赖设备字体。拖动、边缘吸附和半透明行为保持一致，开始采集时增加粉色边框作为状态标记。
