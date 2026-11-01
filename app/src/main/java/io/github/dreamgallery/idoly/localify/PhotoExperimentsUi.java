package io.github.dreamgallery.idoly.localify;

import android.app.Activity;
import android.app.Dialog;
import android.widget.LinearLayout;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;
import org.json.JSONArray;
import org.json.JSONObject;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;

/** Edits process-local native configuration; never calls game code on Android's UI thread. */
final class PhotoExperimentsUi {
    private PhotoExperimentsUi() {}

    static void show(Activity activity) {
        Dialog dialog = new SettingsDialog(activity);
        SettingsUiStyle.Page page = new SettingsUiStyle.Page(dialog, "实验性 · 拍摄与模型",
            "设置在本次游戏进程内生效。修改后重新进入鉴赏或拍摄。", "返回", dialog::dismiss);
        try {
            JSONObject state = new JSONObject(HookEntry.photoExperimentsStatus());
            JSONArray catalog = state.optJSONArray("catalog");
            JSONObject thumbnailDefaults = state.optJSONObject("thumbnail_defaults");
            JSONArray mappings = state.optJSONArray("rules");
            if (mappings == null) mappings = new JSONArray();
            final JSONArray rules = mappings;
            LinearLayout data = SettingsUiStyle.card(page.body, "拍摄数据",
                "拍摄时使用账号已有角色的初始服装、发型、姿势及表情。限时场景使用已开放场景的数据保存；照片画面保留本机效果。建议仅用测试账号。");
            Switch normalize = toggle(data, "使用已有角色初始数据", state.optBoolean("normalize"), state.optBoolean("request_available"));
            LinearLayout content = SettingsUiStyle.card(page.body, "联动内容", "开启后重新打开游戏的拍摄页面。资源仍需由游戏下载。");
            Switch limited = toggle(content, "显示限时 MV 与拍摄场景", state.optBoolean("limited"), state.optBoolean("limited_available"));
            Switch collaborators = toggle(content, "姿势拍摄允许联动角色", state.optBoolean("collaborators"), state.optBoolean("character_available"));
            LinearLayout models = SettingsUiStyle.card(page.body, "运行时模型替换",
                "用于 Live 鉴赏和拍摄，可将多个角色替换为同一模型。非可玩角色可能有动作或比例差异。");
            LinearLayout rows = new LinearLayout(dialog.getContext());
            rows.setOrientation(LinearLayout.VERTICAL);
            models.addView(rows);
            Runnable refresh = () -> renderRules(activity, rows, rules, catalog);
            refresh.run();
            models.addView(SettingsUiStyle.button(dialog.getContext(), "添加替换规则", false,
                () -> addRule(activity, rules, catalog, thumbnailDefaults, refresh)));
            if (!state.optBoolean("model_available") || catalog == null || catalog.length() == 0) {
                models.getChildAt(models.getChildCount() - 1).setEnabled(false);
                models.addView(SettingsUiStyle.text(dialog.getContext(), "请先进入游戏；若仍不可用，请重新打开拍摄页面。", 12, SettingsUiStyle.MUTED, false));
            }
            TextView message = SettingsUiStyle.text(dialog.getContext(), state.optString("error"), 12, SettingsUiStyle.MUTED, false);
            page.body.addView(message);
            page.body.addView(SettingsUiStyle.button(dialog.getContext(), "应用设置", true, () -> {
                try {
                    JSONObject config = new JSONObject().put("normalize", normalize.isChecked()).put("limited", limited.isChecked())
                        .put("collaborators", collaborators.isChecked()).put("rules", rules);
                    if (!HookEntry.setPhotoExperiments(config.toString())) {
                        message.setText(new JSONObject(HookEntry.photoExperimentsStatus()).optString("error", "设置未通过校验"));
                        return;
                    }
                    Toast.makeText(activity, "已应用，请重新进入鉴赏或拍摄", Toast.LENGTH_LONG).show();
                    dialog.dismiss();
                } catch (Exception ignored) { message.setText("无法应用设置，请重新打开后重试"); }
            }));
            LinearLayout.LayoutParams gap = new LinearLayout.LayoutParams(-1, -2);
            gap.topMargin = SettingsUiStyle.dp(activity, 12);
            page.body.addView(SettingsUiStyle.button(dialog.getContext(), "关闭全部拍摄实验功能", false, () -> {
                if (HookEntry.setPhotoExperiments("{\"normalize\":false,\"limited\":false,\"collaborators\":false,\"rules\":[]}")) {
                    Toast.makeText(activity, "已关闭，退出当前拍摄页面后生效", Toast.LENGTH_LONG).show();dialog.dismiss();
                }
            }), gap);
        } catch (Exception ignored) {
            page.body.addView(SettingsUiStyle.text(dialog.getContext(), "请在游戏加载完成后重试。", 14, SettingsUiStyle.MUTED, false));
        }
        dialog.setContentView(page.root);SettingsUiStyle.configureWindow(dialog);dialog.show();
    }

    private static Switch toggle(LinearLayout parent, String name, boolean checked, boolean available) {
        Switch toggle = new Switch(parent.getContext());toggle.setText(name);SettingsUiStyle.styleSwitch(toggle);
        toggle.setChecked(checked);toggle.setEnabled(available);parent.addView(toggle, new LinearLayout.LayoutParams(-1, -2));return toggle;
    }
    private static String label(JSONArray catalog, String id) {
        JSONObject c = entry(catalog, id);
        return c == null ? id : c.optString("name", id);
    }
    private static JSONObject entry(JSONArray catalog, String id) {
        if (catalog != null) for (int i = 0; i < catalog.length(); i++) {
            JSONObject c = catalog.optJSONObject(i);
            if (c != null && c.optString("id").equals(id)) return c;
        }
        return null;
    }
    private static String characterLabel(JSONArray catalog, String id) {
        String name = label(catalog, id);int variants = 0, number = 0;
        for (JSONObject row : sorted(catalog)) if (name.equals(row.optString("name"))) {
            variants++;if (id.equals(row.optString("id"))) number = variants;
        }
        return variants > 1 ? name + "（外观 " + number + "）" : name;
    }
    private static void renderRules(Activity activity, LinearLayout parent, JSONArray rules, JSONArray catalog) {
        parent.removeAllViews();
        for (int i = 0; i < rules.length(); i++) {
            JSONObject rule = rules.optJSONObject(i);if (rule == null) continue;
            final int index = i;int scope = rule.optInt("context", 3);
            JSONObject target = entry(catalog, rule.optString("target"));
            String appearance = target != null && target.optBoolean("fixed_model") ? "独立模型" : "默认外观";
            if (target != null && !rule.optString("costume").isEmpty()) {
                appearance = label(target.optJSONArray("costumes"), rule.optString("costume")) + " / "
                    + label(target.optJSONArray("hairs"), rule.optString("hair"));
                if (!rule.optString("ornament").isEmpty()) appearance += " / " + label(target.optJSONArray("hairs"), rule.optString("ornament"));
            }
            LinearLayout row = new LinearLayout(parent.getContext());row.setOrientation(LinearLayout.VERTICAL);
            row.setPadding(0, SettingsUiStyle.dp(activity, 10), 0, SettingsUiStyle.dp(activity, 12));
            LinearLayout heading = new LinearLayout(parent.getContext());heading.setGravity(android.view.Gravity.CENTER_VERTICAL);
            heading.addView(SettingsUiStyle.text(parent.getContext(), label(catalog, rule.optString("source")) + " → "
                + characterLabel(catalog, rule.optString("target")), 16, SettingsUiStyle.INK, true), new LinearLayout.LayoutParams(0, -2, 1));
            heading.addView(SettingsUiStyle.button(parent.getContext(), "删除", false, () -> {
                rules.remove(index);renderRules(activity, parent, rules, catalog);
            }));
            row.addView(heading);
            TextView detail = SettingsUiStyle.text(parent.getContext(),
                (scope == 1 ? "Live" : scope == 2 ? "场景拍摄" : "Live / 场景拍摄") + " · " + appearance, 13, SettingsUiStyle.MUTED, false);
            detail.setPadding(0, SettingsUiStyle.dp(activity, 6), 0, 0);row.addView(detail);parent.addView(row);
        }
    }
    private static void addRule(Activity activity, JSONArray rules, JSONArray catalog, JSONObject defaults, Runnable refresh) {
        if (catalog == null || catalog.length() == 0) return;
        new RuleEditor(activity, rules, catalog, defaults, refresh).show();
    }

    private static List<JSONObject> sorted(JSONArray values) {
        ArrayList<JSONObject> result = new ArrayList<>();
        for (int i = 0; values != null && i < values.length(); i++) {
            JSONObject value = values.optJSONObject(i);if (value != null) result.add(value);
        }
        result.sort(Comparator.comparingInt((JSONObject row) -> row.optInt("order"))
            .thenComparing(row -> row.optString("id")));
        return result;
    }
    private static boolean contains(JSONArray values, String value) {
        if (values != null) for (int i = 0; i < values.length(); i++) if (value.equals(values.optString(i))) return true;
        return false;
    }
    private static boolean fits(JSONObject hair, String costume) {
        JSONArray allowed = hair.optJSONArray("wearable");
        return (allowed == null || allowed.length() == 0 || contains(allowed, costume))
            && !contains(hair.optJSONArray("excluded"), costume);
    }
    private static SettingsChoiceField.Choice choice(JSONObject row) {
        return new SettingsChoiceField.Choice(row.optString("id"), row.optString("name", row.optString("id")), "", row.optString("thumbnail"));
    }

    private static final class RuleEditor {
        final Activity activity;
        final Dialog dialog;
        final JSONArray rules, catalog;
        final String noOrnamentThumbnail;
        final Runnable refresh;
        final PhotoThumbnailLoader thumbnails = new PhotoThumbnailLoader();
        final SettingsChoiceField source, target, costume, hair, ornament, scope;

        RuleEditor(Activity activity, JSONArray rules, JSONArray catalog, JSONObject defaults, Runnable refresh) {
            this.activity = activity;this.rules = rules;this.catalog = catalog;this.refresh = refresh;
            noOrnamentThumbnail = defaults == null ? "" : defaults.optString("ornament");
            dialog = new SettingsDialog(activity);
            SettingsUiStyle.Page page = new SettingsUiStyle.Page(dialog, "添加模型替换", "", "返回", dialog::dismiss);
            LinearLayout actors = SettingsUiStyle.card(page.body, "替换对象", "按角色匹配，不受原服装或发型影响。");
            source = new SettingsChoiceField(actors, "原角色", null);
            target = new SettingsChoiceField(actors, "显示为", null);
            ArrayList<SettingsChoiceField.Choice> sources = new ArrayList<>(), targets = new ArrayList<>();
            List<JSONObject> characters = sorted(catalog);
            CharacterGroups groups = CharacterGroups.load(activity);
            Comparator<JSONObject> members = Comparator
                .comparingInt((JSONObject row) -> groups.order(row.optString("id")))
                .thenComparingInt(row -> row.optInt("order"))
                .thenComparing(row -> row.optString("id"));
            characters.sort(members);
            for (JSONObject character : characters) if (character.optBoolean("playable"))
                sources.add(new SettingsChoiceField.Choice(character.optString("id"), character.optString("name"),
                    groups.name(character.optString("id"), character.optBoolean("collaboration") ? "联动角色" : "偶像"), ""));
            // Keep groups contiguous even when the game's ordering interleaves
            // collaboration characters, staff and story-only model records.
            characters.sort(Comparator.comparingInt(RuleEditor::group).thenComparing(members));
            for (JSONObject character : characters) targets.add(new SettingsChoiceField.Choice(character.optString("id"), characterLabel(catalog, character.optString("id")),
                group(character) == 0 ? "非可玩角色" : groups.name(character.optString("id"),
                    group(character) == 1 ? "偶像" : "联动角色"), ""));
            source.setChoices(sources, "", "暂无角色");target.setChoices(targets, "", "暂无模型");
            LinearLayout appearance = SettingsUiStyle.card(page.body, "外观", "饰品随对应发型显示。小图由游戏资源加载。");
            costume = new SettingsChoiceField(appearance, "服装", thumbnails);
            hair = new SettingsChoiceField(appearance, "发型", thumbnails);
            ornament = new SettingsChoiceField(appearance, "饰品", thumbnails);
            LinearLayout context = SettingsUiStyle.card(page.body, "应用范围", "");
            scope = new SettingsChoiceField(context, "场景", null);
            scope.setChoices(java.util.Arrays.asList(new SettingsChoiceField.Choice("3", "Live 与场景拍摄"),
                new SettingsChoiceField.Choice("1", "仅 Live"), new SettingsChoiceField.Choice("2", "仅场景拍摄")), "3", "");
            target.onChanged(this::refreshTarget);costume.onChanged(this::refreshHair);hair.onChanged(() -> refreshOrnament(""));
            refreshTarget();
            LinearLayout.LayoutParams gap = new LinearLayout.LayoutParams(-1, -2);gap.topMargin = SettingsUiStyle.dp(activity, 8);
            page.body.addView(SettingsUiStyle.button(dialog.getContext(), "添加规则", true, this::add), gap);
            dialog.setOnDismissListener(d -> thumbnails.close());
            dialog.setContentView(page.root);SettingsUiStyle.configureWindow(dialog);
        }
        static int group(JSONObject character) {
            return !character.optBoolean("playable") ? 0 : character.optBoolean("collaboration") ? 2 : 1;
        }
        JSONObject character() { return entry(catalog, target.selectedId()); }
        void refreshTarget() {
            JSONObject character = character();ArrayList<SettingsChoiceField.Choice> options = new ArrayList<>();
            for (JSONObject row : sorted(character == null ? null : character.optJSONArray("costumes"))) options.add(choice(row));
            costume.setChoices(options, character == null ? "" : character.optString("default_costume"), "固定外观");
            refreshHair();
        }
        void refreshHair() {
            JSONObject character = character();JSONArray hairs = character == null ? null : character.optJSONArray("hairs");
            JSONObject clothes = character == null ? null : entry(character.optJSONArray("costumes"), costume.selectedId());
            String defaultId = clothes == null ? "" : clothes.optString("hair");
            JSONObject defaultHair = entry(hairs, defaultId);
            String base = defaultHair == null || defaultHair.optString("ornament_base").isEmpty() ? defaultId : defaultHair.optString("ornament_base");
            ArrayList<SettingsChoiceField.Choice> options = new ArrayList<>();
            for (JSONObject row : sorted(hairs)) if (row.optString("ornament_base").isEmpty() && fits(row, costume.selectedId())) options.add(choice(row));
            hair.setChoices(options, base, "固定外观");refreshOrnament(defaultId.equals(base) ? "" : defaultId);
        }
        void refreshOrnament(String preferred) {
            JSONObject character = character();ArrayList<SettingsChoiceField.Choice> options = new ArrayList<>();
            options.add(new SettingsChoiceField.Choice("", "无", "", noOrnamentThumbnail));
            for (JSONObject row : sorted(character == null ? null : character.optJSONArray("hairs")))
                if (!hair.selectedId().isEmpty() && row.optString("ornament_base").equals(hair.selectedId()) && fits(row, costume.selectedId())) options.add(choice(row));
            if (options.size() == 1) options.clear();
            ornament.setChoices(options, preferred, "当前发型无可选饰品");
        }
        void add() {
            if (!source.hasSelection() || !target.hasSelection()) return;
            try {
                int context = Integer.parseInt(scope.selectedId());
                for (int i = 0; i < rules.length(); i++) {
                    JSONObject rule = rules.getJSONObject(i);
                    if (rule.optString("source").equals(source.selectedId()) && (rule.optInt("context", 3) & context) != 0) {
                        Toast.makeText(activity, "同一角色在同一范围已有替换规则，请先删除原规则", Toast.LENGTH_LONG).show();return;
                    }
                }
                rules.put(new JSONObject().put("source", source.selectedId()).put("target", target.selectedId()).put("context", context)
                    .put("costume", costume.selectedId()).put("hair", hair.selectedId()).put("ornament", ornament.selectedId()));
                refresh.run();dialog.dismiss();
            } catch (Exception ignored) { Toast.makeText(activity, "无法添加规则", Toast.LENGTH_SHORT).show(); }
        }
        void show() { dialog.show(); }
    }
}
