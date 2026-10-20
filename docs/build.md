# 构建与部署

以下命令从本地化项目根目录执行。构建插件需要 Android SDK、arm64 NDK 26.3、CMake 3.22、JDK 17+ 和 Python 3.12+；LSPatch 修补步骤需要 JDK 21+。游戏适配版本由构建脚本检查；其他版本需要重新核对钩子和资源结构。请使用独立测试 AVD。

## 加载方式

推荐在已 root 的 Android 上，通过兼容系统版本的 LSPosed 或 Vector 启用独立模块，作用域只选 `game.qualiarts.idolypride`。游戏可使用未修补原版 APK；中文字体与汉化图片由模块提供。LSPatch 同样仅嵌入模块，保留游戏原始字体与图片资源。不要让同一游戏进程同时使用 LSPatch 内嵌模块和系统框架重复加载。

模块在原生钩子安装前，将两份内置字体按 SHA-256 校验后原子安装到游戏私有外部目录 `files/idoly-localify/`：`resource-han-rounded.bundle` 为通用字体，`love-rounded.bundle` 为若恋圆体。模块升级或文件损坏时自动替换，失败则保留之前完整文件。此二进制不使用用户词典的保留修改规则。运行时从模块字体包加载；暂时加载失败时保留原字体并重试。

## 准备文件

```sh
export ANDROID_HOME="$HOME/Library/Android/sdk"
export JAVA_HOME="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
python3 -m pip install -r tools/requirements-font.txt
ANDROID_SERIAL="your-device-serial" tools/pull_game_apks.sh .analysis/apk
python3 tools/build_test_apks.py --preflight-only \
  --base .analysis/apk/base.apk \
  --split .analysis/apk/split_config.arm64_v8a.apk
```

从 [Noto 官方固定版本](https://github.com/notofonts/noto-cjk/blob/Sans2.004/Sans/Variable/OTF/NotoSansCJKsc-VF.otf) 下载 `NotoSansCJKsc-VF.otf`，保存到 `.analysis/fonts/`，再生成静态 SemiBold：

```sh
python3 tools/instantiate_font.py \
  .analysis/fonts/NotoSansCJKsc-VF.otf \
  .analysis/fonts/LocalifyNotoSansCJKsc-SemiBold.otf
```

`config/font-source.json` 固定源文件、600 字重及输出 SHA-256；使用 `requirements-font.txt` 中的固定依赖版本，保留完整字库，不单独改写标点轮廓。许可证随模块放在 `assets/licenses/NotoSansCJK-OFL.txt`。将 [LSPatch CLI](https://github.com/JingMatrix/LSPatch/releases) 的 JAR 保存为 `.analysis/tools/lspatch.jar`。

## 准备模块字体

先生成独立 AssetBundle，再构建模块。需要一个本地 UnityFS 资源作格式模板，其序列化文件格式须与游戏一致；脚本会移除模板中的全部图片和其他载荷，仅保留构建字体所需的结构，不下载资源。

```sh
python3 tools/build_font_bundle.py \
  .analysis/apk/base.apk \
  .analysis/fonts/LocalifyNotoSansCJKsc-SemiBold.otf \
  .analysis/font-bundle-template \
  .analysis/font-bundle/resource-han-rounded.bundle
mkdir -p app/src/main/assets/idoly-localify
cp .analysis/font-bundle/resource-han-rounded.bundle app/src/main/assets/idoly-localify/
```

已有独立模块字体 bundle 时，无需再次提供游戏 APK，可直接更新其中的字体：

```sh
python3 tools/prepare_module_font.py \
  .analysis/font-bundle/resource-han-rounded.bundle \
  app/src/main/assets/idoly-localify/resource-han-rounded.bundle
```

此命令按 `config/font-source.json` 下载并校验官方可变字体，生成和缓存静态 SemiBold；离线可加 `--font .analysis/fonts/LocalifyNotoSansCJKsc-SemiBold.otf`。源文件与静态结果均检查 SHA-256。正式发布脚本同样将运行器旧 bundle 作为只读结构模板，重新装入当前默认字体，避免沿用旧字库。通用字体 Unity 对象名为 `Localify-DefaultCJK`；安装文件名 `resource-han-rounded.bundle` 保持稳定。

若恋圆体与按钮统一使用 Resource Han Rounded SC Bold 700。`config/love-font-source.json` 固定 [Resource Han Rounded 官方 1.910 发布包](https://github.com/CyanoHao/Resource-Han-Rounded/releases/tag/v1.910)，构建工具下载 7z 包，只提取完整 SC 可变字体，再生成静态 Bold。压缩包、源字体和静态结果分别校验 SHA-256；下载与生成结果缓存在 `.analysis/fonts/`，也可用 `--cache` 指定持久目录。生成独立圆体包：

```sh
python3 tools/prepare_module_font.py \
  app/src/main/assets/idoly-localify/resource-han-rounded.bundle \
  app/src/main/assets/idoly-localify/love-rounded.bundle \
  --config config/love-font-source.json
```

缓存完整后可离线构建；也可用 `--font` 提供与配置摘要一致的静态字体。字体源文件和生成的 bundle 均不提交仓库，许可证随模块提供。

正式发布流程会准备上述两份字体包。将 `.analysis/font-bundle-template` 替换为实际模板文件。工具检查字体字节、对象及引用完整性；本地构建前须先执行准备步骤，Gradle 不联网获取字体。LSPosed 用户完成模块构建安装后，启用作用域并启动一次游戏即可安装字体。

## 模块图片

已审核图集随代码保存在 `app/src/main/assets/idoly-localify/images/`，构建时直接打包，无需从 Patcher 获取图片或重新下载游戏。执行 `python3 tools/check_runtime_images.py` 校验清单、PNG 尺寸和哈希；修改素材的流程见[图片本地化](image-localization.md)。

## 生成测试包

先按[文本流程](text-pipeline.md)准备好 TXT、CSV 和审校 JSON，再构建：

```sh
python3 tools/pipeline.py build
python3 tools/pipeline.py verify
./gradlew :app:assembleDebug
python3 tools/build_test_apks.py \
  --base .analysis/apk/base.apk \
  --split .analysis/apk/split_config.arm64_v8a.apk \
  --font .analysis/fonts/LocalifyNotoSansCJKsc-SemiBold.otf \
  --font-patch-mode module-font \
  --lspatch .analysis/tools/lspatch.jar \
  --font-cache .analysis/font-cache \
  --output-dir .analysis/signed
```

`build_test_apks.py` 只封装已有模块 APK，不会执行 Gradle。修改 Java、原生代码或内置资源后，必须先成功执行 `assembleDebug`，再封装；嵌入一致性检查只证明所选 APK 被正确嵌入，不证明它对应最新源码。

默认 `module-font` 模式只嵌入模块；字体来自模块 AssetBundle，图片来自模块运行时载荷（见[图片本地化](image-localization.md)）。使用运行时图片时不要传入 `--image-patches`，以便关闭翻译后恢复原图。旧字体及图片烘焙模式仅用于兼容已有部署。

打包脚本会检查 APK 包名、版本、原版签名和字体覆盖率，检查模块内置词典的字体模式与实际载荷是否配对，再按所选模式处理字体、嵌入模块、签名，并核对输出。兼容模式 `add-font` 保留游戏全部原字体资源，须搭配正式 pipeline 生成的 `replace-ui` 词典：界面文字默认使用 Noto Sans CJK SC SemiBold，纯数字及符号组合（包含千分位逗号、时间及加号数字）、商品售价整组（含币种符号与单位）、指定装饰标题及 Rank 下方等级保留原字体，Rank 标题及好友列表昵称与等级整行仍用 Noto Sans CJK SC SemiBold，具体规则由 `ui_font_policy.h` 管理。旧 `preserve-non-cjk` 同样使用 `add-font`，仍保留其按字符分类的兼容行为。旧方案可显式构建 `--font-patch-mode replace-fot`，但模块内置词典也须使用旧模式。默认使用 Android 调试密钥；正式签名可通过 `--keystore` 和 `IDOLY_KEYSTORE_PASSWORD` 指定。测试包包含游戏文件，仅供本地使用。

兼容字体修补模式下，可选的 `--font-cache` 缓存字体替换后的 APK。缓存按字体 patch 模式、原版 base、字体、构建脚本及依赖版本的 SHA/版本绑定；修改插件代码后再次打包可跳过 Unity 字体重写。每次仍检查原版拆分 APK 的版本和签名、当前字符覆盖、缓存字体字节，并重新嵌入和签名最新模块、检查最终产物。缺少缓存时自动生成，输入或工具变化时生成新缓存；缓存损坏则停止并提示删除对应条目。缓存含游戏资源，放在 Git 忽略的本地区域，不上传仓库。

## 安装与部署

将下面的序列号和 AVD 名称替换为实际测试设备：

```sh
export ADB="$ANDROID_HOME/platform-tools/adb"
python3 tools/install_test_apks.py --serial "$ANDROID_SERIAL" --expected-avd "$IDOLY_TEST_AVD" \
  --base .analysis/signed/base.apk \
  --split .analysis/signed/split_config.arm64_v8a.apk
python3 tools/deploy_text.py plan --serial "$ANDROID_SERIAL"
python3 tools/deploy_text.py apply --serial "$ANDROID_SERIAL" --expected-avd "$IDOLY_TEST_AVD" --restart
python3 tools/deploy_text.py verify --serial "$ANDROID_SERIAL"
```

安装 APK 不会自动复制剧情 TXT。`deploy_text.py` 校验本地构建后同步词典、公告译文和剧情文件；MasterDB 记录补丁另用 `deploy_master_blobs.py`，命令见[文本流程](text-pipeline.md)。

部署工具会读取已安装游戏 APK 的 SHA-256，并检查实际字体载荷与词典模式配对。原版游戏或仅嵌入模块的 LSPatch 包还会读取设备上已安装的字体 bundle，解析实际 Font 字节、校验结构与字符覆盖；首次部署前需要启用模块并启动一次游戏。缓存同时绑定 APK 和 bundle 哈希，不能凭文件存在跳过检查。首次遇到一个 APK 时，只读复制其公开安装文件到临时目录，检查后删除；字体摘要和字符覆盖表按 APK 摘要缓存在 `.analysis/font-payload-cache-v2/`。更换 APK 后自动重新检查，不以设备外部字典的旧模式推断字体。同一 APK 下也会对当前原文、译文及目标字典重新核对缺字。模式不符或缺字时在写入前停止，需先安装匹配的测试包。在线文本热更新也不能改变运行中的字体模式；字体模式变更需要匹配的字体载荷并重启。LSPosed 下更新字体只需更新模块 APK；LSPatch 的内嵌模块方式仍需重新封装并安装测试 APK，但无需重写游戏字体资源。

剧情为实时演算，直接进入待验证的话数即可。界面检查使用截图、手动采集和本地加载日志，避免对 Unity 游戏调用 UI 元素列表。

## 常规验证

运行 `python3 tools/check_workspace.py` 检查三个项目的测试、文本产物及仓库内容。安装 C++ 编译器后，测试也会执行整条文本的字体选择、受限文字框的纵向度量和字形预生成回归；缺少编译器时这些原生检查会明确标记为跳过。

字体或 TMP 渲染逻辑变更后，还应在独立测试设备检查列表滚动往返、详情页返回以及消息选择按钮。首次显示正常不能替代控件复用验证。

## 正式插件签名

发布构建使用稳定的维护者密钥，避免换服务器后无法覆盖安装旧版本。设置 `IDOLY_KEYSTORE_FILE`、`IDOLY_KEYSTORE_PASSWORD`、`IDOLY_KEY_ALIAS`、`IDOLY_KEY_PASSWORD` 后执行 `./gradlew :app:assembleRelease`。缺少签名配置时发布构建失败。密钥与密码只保存在本机或自托管运行器的私有配置目录，不能进入代码或翻译仓库。

所有 GitHub Actions 均在翻译仓库配置；插件源码、工具源码从各自私有仓库拉取。正式构建的 APK 发布到翻译仓库，仅版本变化时创建新版本。测试设备的调试签名与正式签名不同，不能直接覆盖安装；验证正式包时应使用单独模块配置或独立测试环境并先处理数据迁移。
