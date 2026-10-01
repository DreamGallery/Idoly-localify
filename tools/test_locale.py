import json
import hashlib
from pathlib import Path
import tempfile
import unittest

from localize import build, collect


class LocalePipelineTest(unittest.TestCase):
    def test_legal_text_keeps_domain_and_rejects_stale_or_tampered_export(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, translated, runtime, legal, output = [root / name for name in
                ('ui-source.json', 'ui-translation.json', 'runtime.json', 'legal.json', 'dict.json')]
            source.write_text('{"text":{},"i18n":{}}')
            translated.write_text('{"text":{},"i18n":{}}')
            original, target = '利用規約\n第1条', '使用条款\n第1条'
            row = {'source': original, 'translation': target,
                   'source_sha256': hashlib.sha256(original.encode()).hexdigest(),
                   'translation_sha256': hashlib.sha256(target.encode()).hexdigest()}
            legal.write_text(json.dumps({'schema_version': 1, 'rules': {'1': {'source': original}}}))
            data = {'schema_version': 1, 'rules': {'1': row}}
            runtime.write_text(json.dumps(data))
            result = build(source, translated, output, False,
                           legal_runtime=runtime, legal_source=legal)
            self.assertNotIn(original, result['text'])
            self.assertEqual(result['legal_by_type']['1']['translation'], target)
            good = output.read_bytes()
            row['translation'] = '使用条款\n第2条'
            runtime.write_text(json.dumps(data))
            with self.assertRaisesRegex(ValueError, 'checksum'):
                build(source, translated, output, False, legal_runtime=runtime, legal_source=legal)
            self.assertEqual(output.read_bytes(), good)
            row['translation'] = target
            runtime.write_text(json.dumps(data))
            legal.write_text(json.dumps({'schema_version': 1, 'rules': {'1': {'source': original + '更新'}}}))
            with self.assertRaisesRegex(ValueError, 'stale'):
                build(source, translated, output, False, legal_runtime=runtime, legal_source=legal)

    def test_collect_preserves_canonical_source_and_reports_only_new_variants(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dump = root / "dump.jsonl"
            source = root / "source.json"
            report = root / "review.json"
            reviewed = root / "reviewed.json"
            source.write_text('{"i18n":{"notice":"原文A"},"text":{}}\n', encoding="utf-8")
            original_bytes = source.read_bytes()
            reviewed.write_text(json.dumps({"notice": ["原文A", "原文B"]},
                                           ensure_ascii=False), encoding="utf-8")
            for variants in (("原文B", "原文C", "原文A"),
                             ("原文A", "原文C", "原文B")):
                dump.write_text("".join(json.dumps({"kind": "i18n", "key": "notice",
                                                    "source": value}, ensure_ascii=False) + "\n"
                                        for value in variants), encoding="utf-8")
                self.assertEqual(collect(dump, source, "i18n", report, reviewed), 0)
                self.assertEqual(source.read_bytes(), original_bytes)
                self.assertEqual(json.loads(report.read_text(encoding="utf-8"))["candidates"],
                                 [{"key": "notice", "canonical": "原文A",
                                   "unreviewed_sources": ["原文C"]}])

    def test_collect_new_ambiguous_key_needs_review(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dump = root / "dump.jsonl"
            source = root / "source.json"
            report = root / "review.json"
            dump.write_text("".join(json.dumps({"kind": "i18n", "key": key,
                                                "source": value}, ensure_ascii=False) + "\n"
                                    for key, value in (("stable", "固定原文"),
                                                       ("varying", "版本一"),
                                                       ("varying", "版本二"))), encoding="utf-8")
            self.assertEqual(collect(dump, source, "i18n", report), 1)
            self.assertEqual(json.loads(source.read_text(encoding="utf-8"))["i18n"],
                             {"stable": "固定原文"})
            self.assertEqual(json.loads(report.read_text(encoding="utf-8"))["candidates"],
                             [{"key": "varying", "canonical": None,
                               "unreviewed_sources": ["版本一", "版本二"]}])

    def test_collect_recovers_nul_padded_line_and_rejects_bad_json_without_writes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dump = root / "dump.jsonl"
            source = root / "source.json"
            report = root / "review.json"
            dump.write_text('\x00\x00{"kind":"i18n","key":"title","source":"表題"}\n',
                            encoding="utf-8")
            self.assertEqual(collect(dump, source, "i18n", report), 1)
            self.assertEqual(json.loads(report.read_text(encoding="utf-8"))
                             ["recovered_nul_lines"], [1])
            before_source, before_report = source.read_bytes(), report.read_bytes()
            dump.write_text('{bad json}\n', encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "dump.jsonl:1"):
                collect(dump, source, "i18n", report)
            self.assertEqual(source.read_bytes(), before_source)
            self.assertEqual(report.read_bytes(), before_report)

    def test_collect_and_build_preserves_placeholders(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dump = root / "dump.jsonl"
            source = root / "source.json"
            translation = root / "zh-Hans.json"
            output = root / "dict.json"
            dump.write_text(
                '{"kind":"text","source":"残り {0} 回"}\n'
                '{"kind":"i18n","key":"home.title","source":"お知らせ"}\n',
                encoding="utf-8",
            )
            self.assertEqual(collect(dump, source), 2)
            translation.write_text(json.dumps({"text": {"残り {0} 回": "剩余 {0} 次"},
                                               "i18n": {"home.title": "公告"}}), encoding="utf-8")
            result = build(source, translation, output, True)
            self.assertEqual(result["text"]["残り {0} 回"], "剩余 {0} 次")
            self.assertEqual(result["i18n"]["home.title"], "公告")
            self.assertTrue(result["dump"])

    def test_rejects_mismatched_placeholder(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            translation = root / "zh-Hans.json"
            translation.write_text(json.dumps({"text": {"残り {0} 回": "剩余 {1} 次"}}), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "placeholders"):
                build(root / "source.json", translation, root / "dict.json", False)

    def test_identity_translation_does_not_replace_original_font(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source.json"
            translation = root / "zh-Hans.json"
            source.write_text(json.dumps({"i18n": {"story.chapter": "{0}章"}}),
                              encoding="utf-8")
            translation.write_text(json.dumps({"i18n": {"story.chapter": "{0}章"}}),
                                   encoding="utf-8")
            result = build(source, translation, root / "dict.json", False)
            self.assertEqual(result["i18n"], {})
            self.assertEqual(result["i18n_by_source"],
                             {"story.chapter": {"{0}章": "{0}章"}})

    def test_retained_i18n_does_not_lose_context_to_global_translation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source.json"
            translation = root / "zh.json"
            source.write_text(json.dumps({"i18n": {"setting.graphic.VeryHigh": "最高"}}))
            translation.write_text(json.dumps({"text": {"最高": "太棒了"},
                "i18n": {"setting.graphic.VeryHigh": "最高"}}))
            result = build(source, translation, root / "dict.json", False)
            self.assertEqual(result["text"]["最高"], "太棒了")
            self.assertEqual(result["i18n_by_source"]["setting.graphic.VeryHigh"],
                             {"最高": "最高"})

    def test_conflicting_derived_sources_are_not_selected_by_load_order(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            master = root / "master.json"
            titles = root / "titles.json"
            master.write_text(json.dumps({"text": {"出陣": "出发"}}))
            titles.write_text(json.dumps({"出陣": "出征"}))
            result = build(root / "source.json", root / "zh.json", root / "dict.json",
                           False, master_text=master, story_titles=titles)
            self.assertNotIn("出陣", result["text"])

    def test_varying_i18n_key_uses_original_text(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            translation = root / "zh-Hans.json"
            translation.write_text(json.dumps({"i18n_by_source": {
                "notification.message": {"終了しました。": "已结束。",
                                         "終了しました！": "已结束！"}}},
                ensure_ascii=False), encoding="utf-8")
            result = build(root / "source.json", translation, root / "dict.json", False)
            self.assertEqual(result["i18n_by_source"]["notification.message"],
                             {"終了しました。": "已结束。", "終了しました！": "已结束！"})

    def test_story_title_map_preserves_manual_override(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            translation = root / "zh-Hans.json"
            title_map = root / "story-title-map.json"
            translation.write_text(json.dumps({"text": {"夢の続き": "梦想的延续"}},
                                              ensure_ascii=False), encoding="utf-8")
            title_map.write_text(json.dumps({"夢の続き": "梦的后续", "新しい道": "崭新的道路"},
                                            ensure_ascii=False), encoding="utf-8")
            result = build(root / "source.json", translation, root / "dict.json", False,
                           story_titles=title_map)
            self.assertEqual(result["text"],
                             {"夢の続き": "梦想的延续", "新しい道": "崭新的道路"})

    def test_notice_preview_title_alias_preserves_explicit_short_title(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            translation = root / "zh-Hans.json"
            translation.write_text(json.dumps({"text": {
                "【予告】EXタワーにSTAGE追加！": "【预告】EX塔新增关卡！",
                "【予告】プレミアムガチャ更新！": "【预告】高级扭蛋更新！",
                "プレミアムガチャ更新！": "高级扭蛋即将更新！",
            }}, ensure_ascii=False), encoding="utf-8")
            result = build(root / "source.json", translation, root / "dict.json", False)
            self.assertEqual(result["text"]["EXタワーにSTAGE追加！"], "EX塔新增关卡！")
            self.assertEqual(result["text"]["プレミアムガチャ更新！"],
                             "高级扭蛋即将更新！")

    def test_real_notice_heading_wins_over_synthetic_preview_alias(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = build(root / 'source.json', root / 'zh.json', root / 'dict.json', False,
                           notice_headings=[('おでかけミッション開催！', '外出任务开启！'),
                               ('【予告】おでかけミッション開催！', '【预告】外出任务即将开启！')])
            self.assertEqual(result['text']['おでかけミッション開催！'], '外出任务开启！')

    def test_runtime_conflicts_report_keeps_selected_translation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            translation = root / "zh-Hans.json"
            master = root / "master.json"
            titles = root / "titles.json"
            report = root / "conflicts.json"
            translation.write_text(json.dumps({"text": {"夢の続き": "梦想的延续"}},
                                              ensure_ascii=False), encoding="utf-8")
            master.write_text(json.dumps({"text": {"夢の続き": "梦的延续"}},
                                         ensure_ascii=False), encoding="utf-8")
            titles.write_text(json.dumps({"夢の続き": "梦的后续"}, ensure_ascii=False),
                              encoding="utf-8")
            result = build(root / "source.json", translation, root / "dict.json", False,
                           master_text=master, story_titles=titles,
                           conflict_report=report)
            self.assertEqual(result["text"]["夢の続き"], "梦想的延续")
            self.assertEqual(json.loads(report.read_text(encoding="utf-8")), {
                "conflicts": [{
                    "source": "夢の続き",
                    "selected": {"origin": "ui-text", "translation": "梦想的延续"},
                    "discarded": [
                        {"origin": "master-text", "translation": "梦的延续"},
                        {"origin": "story-title", "translation": "梦的后续"},
                    ],
                }],
            })

    def test_story_patch_values_are_registered_for_font_replacement(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            story = root / "story"
            story.mkdir()
            (story / "episode.json").write_text(json.dumps({
                "source_sha256": "a" * 64,
                "translations": {"1:name:1": "长濑麻奈", "2:name:1": "长濑麻奈",
                                 "3:text:1": "要成为偶像？"},
            }, ensure_ascii=False), encoding="utf-8")
            glossary = root / "names.json"
            glossary.write_text(json.dumps({"長瀬琴乃": "长濑琴乃"}, ensure_ascii=False),
                                encoding="utf-8")
            result = build(root / "source.json", root / "zh-Hans.json",
                           root / "dict.json", False, story_dir=story,
                           name_glossary=glossary)
            self.assertEqual(result["translated_values"],
                             ["要成为偶像？", "长濑琴乃", "长濑麻奈"])
            self.assertEqual(result["text"]["長瀬琴乃"], "长濑琴乃")
            self.assertEqual(json.loads((root / "dict.json").read_text(encoding="utf-8"))
                             ["translated_values"], result["translated_values"])

    def test_story_values_include_rendered_newline_and_merged_user_honorific(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            story = root / "story"
            story.mkdir()
            (story / "episode.json").write_text(json.dumps({
                "translations": {"1:text:1": r"啊！来了来了！\n{user}さん！"},
            }, ensure_ascii=False), encoding="utf-8")
            glossary = root / "names.json"
            glossary.write_text(json.dumps({"{user}さん": "{user}先生"},
                                          ensure_ascii=False), encoding="utf-8")
            result = build(root / "source.json", root / "zh-Hans.json",
                           root / "dict.json", False, story_dir=story,
                           name_glossary=glossary)
            values = result["translated_values"]
            self.assertIn("啊！来了来了！\n{user}先生！", values)
            self.assertIn(r"啊！来了来了！\n{user}先生！", values)

    def test_master_flattened_alias_is_compiled(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            master = root / "master.json"
            master.write_text(json.dumps({"text": {"伝説の\nアイドル": "传说中的\n偶像"},
                                          "aliases": {"伝説のアイドル": "传说中的偶像"}},
                                         ensure_ascii=False), encoding="utf-8")
            result = build(root / "source.json", root / "zh-Hans.json",
                           root / "dict.json", False, master_text=master)
            self.assertEqual(result["text"]["伝説のアイドル"], "传说中的偶像")


if __name__ == "__main__":
    unittest.main()
