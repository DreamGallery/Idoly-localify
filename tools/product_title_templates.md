# 商品序号模板候选

从明确商品标题的已审原文／译文生成候选；工具只写指定报告，不修改正式UI词典，不调用API。

在 `Idoly-localify` 目录运行现有证据：

```sh
.analysis/toolkit-venv/bin/python tools/product_title_templates.py \
  --titles .analysis/shop-detail-title-model-review.json \
  --config translations/ui/product-title-template-evidence.json \
  --output .analysis/product-title-template-candidates.json
```

新标题输入格式：

```json
{"kind":"reviewed_product_titles","entries":[
  {"source":"特別パック1","translation":"特别礼包1"},
  {"source":"特別パック2","translation":"特别礼包2"}
]}
```

批准配置格式：

```json
{"approved_stems":["特別パック"],"confirmed_single_stems":[]}
```

默认要求同一批准词干至少两个不同序号且译名一致。重复样本不增加票数；不保留原编号、同词干异译或非法模板会报错。只有另外确认了商品系列的单样本词干才能明确加入 `confirmed_single_stems`；其必须是批准词干的子集。旧 `shop_detail_title_model_review` 输入只使用人工选定的 `selected`，不把模型建议自动视为已审。

候选仅涵盖完整批准词干与末尾1–6位ASCII数字；不做通用前缀替换，不翻译玩家名字，也不推广所有带编号的字符串。报告包含 `templates`、仍需确认的 `pending_stems`、无样本批准词干及输入SHA。后续人工核对后才将候选配对加入UI源文／译文并跑数字模板边界测试，再交设备验收。

## Master Item 数量

已确认页面商品可按有限 `approved_item_ids` 从 Master Item 已译基础名派生完整 `名字×{number}` 候选。只读导出，不扫描全表生成，也不拼入库存日期：

```sh
.analysis/toolkit-venv/bin/python tools/item_quantity_templates.py \
  --master-orig translations/master/orig/Item.json \
  --master-zh translations/master/zh-Hans/Item.json \
  --approved-ids translations/ui/exchange-item-template-evidence.json \
  --output .analysis/exchange-item-quantity-candidates.json
```

同一个原名对应不同已译名字会拒绝，短词干仍受现有12字节门槛限制。`×` 保持原样，不把字母 `x`、全角数字、额外日期后缀或玩家前缀视为匹配。库存重置基础标签和日期须由相应页面的严格动态格式处理，禁止写死某天的整段商品说明。
