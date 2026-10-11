package io.github.dreamgallery.idoly.localify;

import android.app.Activity;
import android.app.Dialog;
import android.text.InputType;
import android.util.Log;
import android.widget.EditText;
import android.widget.Toast;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.lang.ref.WeakReference;
import java.nio.charset.StandardCharsets;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.List;

/** Edits only adjustment fields in native validated font profiles. */
final class FontProfileEditor {
    private static final String TAG = "IdolyLocalify";
    private static final String FILE = "font-profiles.json";
    private static final int LIMIT = 64 * 1024;
    private static Session pending;
    private static WeakReference<Activity> pendingOwner = new WeakReference<>(null);
    private static Dialog opened;
    private static WeakReference<Activity> dialogOwner = new WeakReference<>(null);

    private FontProfileEditor() {}

    static void close(Activity owner, boolean destroyed) {
        if (dialogOwner.get() == owner && opened != null) opened.dismiss();
        if (destroyed && pending != null && pendingOwner.get() == owner) {
            HookEntry.applyFontProfiles(pending.before);
            pending = null;
            pendingOwner.clear();
        }
    }

    static void restoreSaved(File dataDir) {
        File saved = new File(dataDir, FILE);
        if (!saved.isFile()) return;
        try {
            if (saved.length() < 1 || saved.length() > LIMIT) throw new IllegalArgumentException("size");
            String json = new String(Files.readAllBytes(saved.toPath()), StandardCharsets.UTF_8);
            if (!HookEntry.applyFontProfiles(json)) throw new IllegalArgumentException("validation");
            Log.i(TAG, "Loaded saved font profile settings");
        } catch (Exception error) {
            Log.e(TAG, "Saved font profile settings rejected; using built-in defaults", error);
        }
    }

    static void show(Activity activity, File dataDir) {
        if (pending != null && (pendingOwner.get() != activity ||
                !pending.dataDir.getAbsolutePath().equals(dataDir.getAbsolutePath()))) {
            HookEntry.applyFontProfiles(pending.before);
            pending = null;
        }
        if (pending != null) {
            reopen(activity, pending);
            return;
        }
        String initial = HookEntry.getFontProfiles();
        if (initial == null || initial.isEmpty()) {
            Toast.makeText(activity, "字体配置尚未就绪", Toast.LENGTH_LONG).show();
            return;
        }
        try {
            Session session = new Session(dataDir, initial);
            menu(activity, session);
        } catch (Exception error) {
            Toast.makeText(activity, "字体配置无法读取", Toast.LENGTH_LONG).show();
        }
    }

    private static final class Session {
        final File dataDir;
        String before;
        JSONObject current;
        boolean committed;
        Session(File directory, String initial) throws Exception {
            dataDir = directory;
            before = initial;
            current = new JSONObject(initial);
        }
    }

    private static String profileLabel(String id) {
        switch (id) {
            case "ordinary_story_subtitle": return "普通剧情字幕";
            case "ordinary_story_name": return "普通剧情人名";
            case "portrait_love_event_name": return "竖屏活动人名";
            case "portrait_love_event_dialogue": return "竖屏活动对白";
            case "home_talk_dialogue": return "HomeTalk 对白";
            case "home_talk_name": return "HomeTalk 人名";
            case "card_support_label": return "卡片辅助标签";
            case "costume_preview_hide_ui": return "服装预览 · 隐藏 UI";
            case "costume_preview_reaction": return "服装预览 · 动作";
            case "costume_selection_hide_ui": return "换装 · 隐藏 UI";
            case "costume_selection_reaction": return "换装 · 动作";
            case "costume_selection_determine": return "换装 · 确定";
            case "shop_new_arrivals": return "商店 · 新品上架";
            case "daily_roulette_description": return "每日转盘 · 底部说明";
            case "item_floor_badge": return "物品缩略图 · 地面";
            case "item_wall_badge": return "物品缩略图 · 墙面";
            case "generic_ui": return "通用界面";
            default: return id;
        }
    }

    private static String fieldLabel(String name) {
        switch (name) {
            case "outline_width": return "描边宽度";
            case "outline_outward": return "描边向外扩展";
            case "original_punctuation": return "标点使用原版字体";
            case "face_color_rgba": return "文字颜色 (RGBA)";
            case "outline_color_rgba": return "描边颜色 (RGBA)";
            case "vertex_color_rgba": return "叠加颜色 (RGBA)";
            case "face_dilate": return "字面膨胀";
            case "font_size_ratio": return "字号比例";
            case "font_size_base_unity": return "基准字号";
            case "font_size_unity": return "字号";
            case "font_size_max_unity": return "最大字号";
            case "line_spacing_delta_unity": return "行距增量";
            case "anchored_y_delta_unity": return "竖向偏移增量";
            case "anchored_y_unity": return "竖向位置";
            case "character_spacing_unity": return "字间距";
            case "alignment": return "对齐";
            case "content_width_ratio": return "容器可用宽度比例";
            case "rect_size_unity": return "文字框尺寸";
            case "rebuild_parent_layout_below_width_unity": return "重排宽度阈值";
            default: return name;
        }
    }

    // Filter presentation only: retain the complete validated profile payload
    // when previewing/saving so existing settings and legacy modes stay intact.
    private static List<String> editableKeys(JSONObject profile) throws Exception {
        List<String> keys = new ArrayList<>();
        String id = profile.getString("id");
        if ("home_venus_rank".equals(id) || "home_company_rank".equals(id)) return keys;
        JSONObject adjust = profile.getJSONObject("adjust");
        JSONArray names = adjust.names();
        if (names == null) return keys;
        for (int i = 0; i < names.length(); i++) {
            String key = names.getString(i);
            if ("home_talk_name".equals(id) && "rebind_font_before_render".equals(key)) continue;
            // Enum strings are fixed to the verified native baseline.
            if (!(adjust.get(key) instanceof String)) keys.add(key);
        }
        return keys;
    }

    private static void menu(Activity activity, Session session) throws Exception {
        JSONArray profiles = session.current.getJSONArray("profiles");
        List<String> labels = new ArrayList<>();
        List<Integer> indices = new ArrayList<>();
        for (int i = 0; i < profiles.length(); i++) {
            JSONObject profile = profiles.getJSONObject(i);
            if (editableKeys(profile).isEmpty()) continue;
            labels.add(profileLabel(profile.getString("id")));
            indices.add(i);
        }
        labels.add("保存当前参数");
        labels.add("恢复内置默认参数");
        if (pending == session && !session.committed) labels.add("取消本次预览");
        fullScreenList(activity, "字体与排版", labels, null, indices.size(), which -> {
                if (which < indices.size()) fields(activity, session, indices.get(which));
                else if (which == indices.size()) save(activity, session);
                else if (which == indices.size() + 1) reset(activity, session);
                else rollback(activity, session);
            }, "返回游戏", () -> { /* Keep an active preview until explicitly saved or cancelled. */ });
    }

    private static void fields(Activity activity, Session session, int index) {
        try {
            JSONObject profile = session.current.getJSONArray("profiles").getJSONObject(index);
            JSONObject adjust = profile.getJSONObject("adjust");
            List<String> keys = editableKeys(profile);
            if (keys.isEmpty()) { reopen(activity, session); return; }
            List<String> labels = new ArrayList<>();
            List<String> values = new ArrayList<>();
            for (String key : keys) {
                Object value = adjust.get(key);
                labels.add(fieldLabel(key));
                values.add(value instanceof Boolean ? ((Boolean) value ? "开启" : "关闭") : value.toString());
            }
            fullScreenList(activity, profileLabel(profile.getString("id")), labels, values, labels.size(),
                which -> edit(activity, session, index, keys.get(which)),
                "返回", () -> reopen(activity, session));
        } catch (Exception error) {
            Toast.makeText(activity, "参数读取失败", Toast.LENGTH_LONG).show();
        }
    }

    private static void showDialog(Activity activity, Dialog dialog) {
        Dialog previous = opened;
        dialog.show();
        if (previous != null) previous.dismiss();
        opened = dialog;
        dialogOwner = new WeakReference<>(activity);
        dialog.setOnDismissListener(ignored -> {
            if (opened == dialog) { opened = null; dialogOwner.clear(); }
        });
    }

    private interface ListAction { void select(int index); }

    private static void fullScreenList(Activity activity, String title, List<String> labels,
                                       List<String> values, int actionStart, ListAction select,
                                       String backLabel, Runnable back) {
        Dialog dialog = new Dialog(activity, android.R.style.Theme_Material_Light_NoActionBar);
        SettingsUiStyle.Page page = new SettingsUiStyle.Page(dialog, title,
            values == null ? "选择要调整的场景。预览后可返回游戏查看效果。" :
                "点击参数修改并预览；保存后会在下次启动时自动载入。",
            backLabel, () -> { back.run(); dialog.dismiss(); });
        android.widget.LinearLayout rows = SettingsUiStyle.card(page.body,
            values == null ? "游戏场景" : "当前参数", null);
        for (int i = 0; i < actionStart; i++) {
            final int index = i;
            SettingsUiStyle.row(rows, labels.get(i), values == null ? null : values.get(i),
                () -> { select.select(index); dialog.dismiss(); });
        }
        if (actionStart < labels.size()) {
            android.widget.LinearLayout actions = SettingsUiStyle.card(page.body, "预览与保存",
                "保存当前预览，或恢复此前参数。恢复默认会覆盖已保存的设置。");
            for (int i = actionStart; i < labels.size(); i++) {
                final int index = i;
                Runnable action = () -> { select.select(index); dialog.dismiss(); };
                if (i == actionStart) {
                    actions.addView(SettingsUiStyle.button(dialog.getContext(), labels.get(i), true, action),
                        new android.widget.LinearLayout.LayoutParams(-1, -2));
                } else SettingsUiStyle.row(actions, labels.get(i), null, action);
            }
        }
        dialog.setContentView(page.root);
        dialog.setOnCancelListener(ignored -> back.run());
        dialog.setCanceledOnTouchOutside(false);
        SettingsUiStyle.configureWindow(dialog);
        showDialog(activity, dialog);
    }

    private static void edit(Activity activity, Session session, int index, String key) {
        try {
            Object value = session.current.getJSONArray("profiles").getJSONObject(index)
                .getJSONObject("adjust").get(key);
            if (value instanceof Boolean) {
                showDialog(activity, new PluginDialog.Builder(activity).setTitle(fieldLabel(key))
                    .setItems(new String[]{Boolean.TRUE.equals(value) ? "开启 · 当前" : "开启",
                        Boolean.FALSE.equals(value) ? "关闭 · 当前" : "关闭"}, (dialog, which) ->
                        preview(activity, session, index, key, which == 0))
                    .setOnCancelListener(dialog -> fields(activity, session, index)).create());
                return;
            }
            EditText input = PluginDialog.input(activity);
            input.setSingleLine(true);
            input.setText(value.toString());
            input.setHint(fieldLabel(key));
            input.setSelectAllOnFocus(true);
            if (value instanceof Number) input.setInputType(InputType.TYPE_CLASS_NUMBER |
                InputType.TYPE_NUMBER_FLAG_SIGNED |
                InputType.TYPE_NUMBER_FLAG_DECIMAL);
            showDialog(activity, new PluginDialog.Builder(activity).setTitle(fieldLabel(key)).setView(input)
                .setPositiveButton("预览", (dialog, which) -> {
                    try {
                        String raw = input.getText().toString().trim();
                        Object edited;
                        if (value instanceof Number) {
                            double number = Double.parseDouble(raw);
                            if (!Double.isFinite(number)) throw new IllegalArgumentException("数字无效");
                            edited = number;
                        } else if (value instanceof JSONArray) {
                            edited = new JSONArray(raw);
                        } else edited = raw;
                        preview(activity, session, index, key, edited);
                    } catch (Exception error) {
                        Toast.makeText(activity, "数值或格式无效", Toast.LENGTH_LONG).show();
                        fields(activity, session, index);
                    }
                }).setNegativeButton("取消", (dialog, which) -> fields(activity, session, index))
                .setOnCancelListener(dialog -> fields(activity, session, index)).create());
        } catch (Exception error) {
            Toast.makeText(activity, "参数读取失败", Toast.LENGTH_LONG).show();
        }
    }

    private static void preview(Activity activity, Session session, int index, String key, Object value) {
        try {
            JSONObject candidate = new JSONObject(session.current.toString());
            candidate.getJSONArray("profiles").getJSONObject(index)
                .getJSONObject("adjust").put(key, value);
            if (!HookEntry.applyFontProfiles(candidate.toString())) {
                Toast.makeText(activity, "参数未通过校验", Toast.LENGTH_LONG).show();
            } else {
                session.current = candidate;
                session.committed = false;
                pending = session;
                pendingOwner = new WeakReference<>(activity);
                Toast.makeText(activity, "预览中；可在游戏内查看效果，再次打开字体菜单保存或取消",
                    Toast.LENGTH_LONG).show();
            }
        } catch (Exception error) {
            Toast.makeText(activity, "参数格式无效", Toast.LENGTH_LONG).show();
        }
        // Leave the game visible so the preview can actually be judged.
    }

    private static void write(File directory, String json) throws Exception {
        byte[] bytes = json.getBytes(StandardCharsets.UTF_8);
        if (bytes.length < 1 || bytes.length > LIMIT) throw new IllegalArgumentException("配置文件过大");
        File target = new File(directory, FILE);
        File temporary = new File(directory, FILE + ".tmp");
        try (FileOutputStream output = new FileOutputStream(temporary)) {
            output.write(bytes);
            output.getFD().sync();
        }
        try {
            Files.move(temporary.toPath(), target.toPath(),
                StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
        } catch (AtomicMoveNotSupportedException ignored) {
            Files.move(temporary.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING);
        }
    }

    private static void save(Activity activity, Session session) {
        try {
            write(session.dataDir, session.current.toString(2));
            session.before = session.current.toString();
            session.committed = true;
            pending = null;
            Toast.makeText(activity, "字体参数已保存", Toast.LENGTH_SHORT).show();
        } catch (Exception error) {
            HookEntry.applyFontProfiles(session.before);
            pending = null;
            Toast.makeText(activity, "保存失败，已恢复此前参数", Toast.LENGTH_LONG).show();
        }
    }

    private static void reset(Activity activity, Session session) {
        if (!HookEntry.resetFontProfiles()) {
            Toast.makeText(activity, "恢复默认参数失败", Toast.LENGTH_LONG).show();
            return;
        }
        String defaults = HookEntry.getFontProfiles();
        try {
            if (defaults == null) throw new IllegalStateException("默认参数为空");
            write(session.dataDir, defaults);
            session.before = defaults;
            session.current = new JSONObject(defaults);
            session.committed = true;
            pending = null;
            Toast.makeText(activity, "已恢复并保存内置默认参数", Toast.LENGTH_SHORT).show();
        } catch (Exception error) {
            HookEntry.applyFontProfiles(session.before);
            pending = null;
            Toast.makeText(activity, "保存失败，已恢复此前参数", Toast.LENGTH_LONG).show();
        }
    }

    private static void rollback(Activity activity, Session session) {
        if (!session.committed) HookEntry.applyFontProfiles(session.before);
        if (pending == session) pending = null;
    }

    private static void reopen(Activity activity, Session session) {
        try { menu(activity, session); }
        catch (Exception error) { Toast.makeText(activity, "字体参数无法读取", Toast.LENGTH_LONG).show(); }
    }
}
