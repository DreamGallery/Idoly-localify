# UI 图片本地化

主页、扭蛋、360°LIVE、若恋及工作页面的部分文字位于图片图集中。插件通过 `CanvasRenderer.SetTexture` 在图集首次显示时载入中文 PNG，保留原纹理对象、精灵 UV、布局和点击区域。文本更新清单不分发图片；更新图片需要重新构建插件。

## 运行时图片

`app/src/main/assets/idoly-localify/images/` 保存模块图片及校验清单。初始化时验证 SHA-256 后安装到本地；仅匹配完整纹理名称和尺寸，加载失败时保留原图。图集与未匹配纹理都使用有界弱引用缓存，避免逐帧读取文件；上传后释放 CPU 像素副本。RGBA 纹理的显存占用高于原始 ASTC。

关闭翻译并重启后，游戏重新读取原始图集。LSPosed 使用原版游戏；LSPatch 应仅嵌入模块，不再向游戏 APK 烘焙图片。旧修补包已经写入的中文图片不会因关闭插件而恢复，需要用原版 APK 重新封装。Idoly-Patcher 只嵌入插件并保留原始游戏资源。

运行时图集与已审核区域图片都由本仓库维护。常规插件构建直接打包现有图集；修改素材时，用原版 APK 和区域包重新生成：

```sh
python3 tools/build_runtime_images.py --base /path/to/original/base.apk --patches image-patches/ui-6.0.2/manifest.json --output app/src/main/assets/idoly-localify/images
```

工具验证原 Unity 资源哈希、纹理身份、区域 PNG 哈希、边界和最终 RGBA 哈希。资源不匹配时停止，不能仅修改哈希跳过图像审核。当前区域包对应游戏 6.0.2。

## 修改图片文字

`config/button-images.json` 与 `config/ui-label-images.json` 维护圆形按钮、若恋标签及工作标签的译名、文字区域及字体校验。图片文字使用 `config/font-source.json` 中与游戏替换字体一致的 Noto Sans CJK SC SemiBold（字重 600）。使用 UnityPy、Pillow 和该字体生成候选图片：

```sh
python3 tools/build_button_images.py --base /path/to/original/base.apk --font /path/to/LocalifyNotoSansCJKsc-SemiBold.otf --output-dir .analysis/button-images
python3 tools/build_ui_label_images.py --base /path/to/original/base.apk --font /path/to/LocalifyNotoSansCJKsc-SemiBold.otf --merge-manifest .analysis/button-images/manifest.json --output-dir .analysis/ui-label-images
```

生成器校验允许编辑区域及外围像素，保留按钮边框和采样留白。扭蛋票券先按原轮廓补全，再叠加中文及描边；封闭字腔填入描边底色，外侧保持透明。审核对照图和游戏实际显示后，用本仓库的 `tools/export_image_pack.py` 转为区域包，再构建运行时载荷。截图、预览、原版 APK 和字体文件不提交源码仓库。

```sh
python3 tools/export_image_pack.py --base /path/to/original/base.apk --manifest .analysis/ui-label-images/manifest.json --output .analysis/reviewed-image-pack
python3 tools/build_runtime_images.py --base /path/to/original/base.apk --patches .analysis/reviewed-image-pack/manifest.json --output app/src/main/assets/idoly-localify/images
python3 tools/check_runtime_images.py
```

审核后将区域包保存到 `image-patches/`，与运行时图集一并提交。发布流程会校验图集文件，并确认 APK 内的图片与仓库中的审核版本一致。

游戏原美术和商标归各自权利人；GPL 不对原游戏美术授予许可。图片中文字形来自采用 SIL OFL 1.1 的 Noto Sans CJK SC。区域素材与归属说明见本仓库的 [`image-patches/NOTICE.md`](../image-patches/NOTICE.md)。

## 其他显示规则

- 若恋“第 N 话进行中”由插件在指定控件中绘制动态文本，保留气泡背景，不依赖图集替换。
- Octo 下载图片需单独处理 AssetBundle，不能使用本页的 APK 图集清单。
- 悬浮入口使用 `config/overlay-icon.svg` 对应的矢量 IP 字母，界面由 Canvas 绘制；采集时增加粉色边框。
