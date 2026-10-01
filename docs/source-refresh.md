# 官方原文增量更新

`tools/refresh_sources.py` 编排 Octo 剧情下载、MasterDB 导出、当前公告网页更新和增量翻译。它不发布产物。发布端应在此步骤成功后合入匹配当前原文的人工稿，再构建、验证和打包。

```sh
python tools/refresh_sources.py \
  --toolkit-dir /path/to/HoshimiToolkit \
  --translator-dir /path/to/Hoshimi_Teleprompter \
  --solis-dir /path/to/SolisClient --app-version CURRENT_VERSION \
  --translations-dir /path/to/translations \
  --source-dir /path/to/adventure --csv-dir /path/to/csv \
  --notice-account /path/to/HoshimiToolkit/cache/notice/collector-account.json \
  --work-dir /path/to/private-persistent-refresh-cache \
  --translate --accept-story-rebase
```

`--dry-run` 只打印步骤，不访问网络、不创建文件。省略 `--translate` 时更新原文、复用本地已验证译文并报告待译数，不请求外部模型。传入 `--translate` 表示允许将待译的官方静态正文及词典上下文发往 Teleprompter 已配置的服务；凭据、账号缓存和运行日志不作为翻译输入。

`--python` 选择通用工具解释器；`--api-python` 选择装有 Solis protobuf、gRPC、SQLCipher 的解释器。`--env-file` 可指定 Teleprompter 私有配置文件，也可使用其标准 `OPENAI_*` 环境变量。Octo 读取 Toolkit 的标准 `IDOLY_OCTO_*` 配置。MasterTag 使用匿名 `Master.Get`；公告只使用已存在且用途验证通过的专用 collector，不创建账号。显式添加 `--initialize-collector-day` 可在服务端要求时初始化该采集账号的新一天并领取自动登录奖励；默认关闭，玩家账号不适用。

发布任务可先以 `--no-translate` 抓取官方原文，再导入对应当前原文的已有机器译文，最后以 `--reuse-fetched --translate` 完成缺口。复用要求同一 `work-dir` 中存在成功的抓取凭据，严格校验剧情 TXT、MasterTag、原始表、Octo 清单和公告快照摘要；翻译数据可以在两阶段之间改变。凭据缺失或任何原文变化会停止，避免混用两个版本。复用阶段不重新访问官方接口。

持久保存 `--work-dir`，其中包含 MasterTag、原始表缓存、模型结果日志及精确原文对应的剧情翻译记忆。公开仓库不要收录该目录或完整运行日志。既有非空译文优先；MasterDB 原文变动的旧 JSON 由导出器备份。剧情变化按字段与原文匹配迁移，`--accept-story-rebase` 允许归档无法迁移的旧字段后重译，否则此情况停止供审阅。音乐制作人员字段保持原文；公告中明确的作词、作曲、编曲署名仅翻译标签。

输出 JSON 摘要默认位于 `work-dir/summary.json`，可用 `--summary` 更改。包括 Octo 前后 revision、原文新增/变化/删除数量、翻译前后待译数量、`master_version` 和后续打包需要的绝对 `master_tag` 路径。下载、导出或翻译任一阶段失败退出非零；启用翻译后仍有缺口也退出非零。发布工作流必须检查退出码及 `complete`，不能只依据有无新 Git 差异发布。
