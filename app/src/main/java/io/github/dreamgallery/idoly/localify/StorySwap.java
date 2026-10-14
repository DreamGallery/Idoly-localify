package io.github.dreamgallery.idoly.localify;

import android.app.Activity;
import android.app.Dialog;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.text.Editable;
import android.text.TextWatcher;
import android.widget.ArrayAdapter;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;

/** Independent, same-format runtime aliases for installed story scripts. */
final class StorySwap {
    private final String STATE, CATALOG, kind, category;
    private StorySwap(String kind, String category) {
        this.kind = kind;
        this.category = category;
        STATE = kind.equals("card") ? "story-swap.json" : "story-swap-" + kind + ".json";
        CATALOG = kind.equals("card") ? "card-stories.json" : kind + "-stories.json";
    }

    private final class StoryItem {
        final String file;
        final String title;
        final String rawTitle;
        final String displayLabel;
        final String groupName;
        final String groupId;
        final int episode;
        final String characterId;
        final String characterName;
        List<Integer> choiceCounts = new ArrayList<>();
        StoryItem(JSONObject row) throws Exception {
            file = row.getString("file");
            rawTitle = row.getString("title");
            title = rawTitle.replace("\\n", " ").replace('\n', ' ');
            groupName = row.getString(kind.equals("card") ? "card_name" : "group_name").replace("\\n", " ").replace('\n', ' ');
            groupId = row.getString(kind.equals("card") ? "card_id" : "group_id");
            episode = row.getInt("episode");
            displayLabel = row.optString("display_label", "第" + episode + "话 · " + title);
            String owner = kind.equals("card") ? row.optString("character_id", "").trim() : groupId;
            String name = kind.equals("card") ? row.optString("character_name", "").trim() : groupName;
            characterId = owner.isEmpty() || name.isEmpty() ? "" : owner;
            characterName = characterId.isEmpty() ? "未分类" : name;
            if (!validName(file) || episode < 1 || episode > 20) throw new Exception("剧情目录无效");
        }
        String choiceSummary() {
            if (choiceCounts.isEmpty()) return "无选项";
            return choiceCounts.size() + " 组选项 · 每组 " +
                android.text.TextUtils.join(" / ", choiceCounts) + " 项";
        }
        String label() { return groupName + " · " + displayLabel +
            (kind.equals("love") ? "\n" + choiceSummary() : ""); }
        String search() { return (label() + " " + characterName + " " + file + " " + groupId).toLowerCase(Locale.ROOT); }
    }

    static void show(Activity activity, File dataDir) {
        new StorySwap("card", "卡片").showMenu(activity, dataDir);
    }
    static void showEvent(Activity activity, File dataDir) {
        new StorySwap("event", "普通活动").showMenu(activity, dataDir);
    }
    static void showSpecial(Activity activity, File dataDir) {
        new StorySwap("love", "特殊活动").showMenu(activity, dataDir);
    }

    private void showMenu(Activity activity, File dataDir) {
        background(activity, dataDir, "正在读取剧情目录…", true, () -> {
            List<StoryItem> available = loadAvailable(activity, dataDir);
            return new CatalogSelection(available, readState(dataDir, available));
        }, selection -> showMenu(activity, dataDir, selection.available, selection.active));
    }

    private final class CatalogSelection {
        final List<StoryItem> available;
        final JSONObject active;
        CatalogSelection(List<StoryItem> available, JSONObject active) {
            this.available = available;
            this.active = active;
        }
    }
    private interface Work<T> { T run() throws Exception; }
    private interface Ready<T> { void run(T value) throws Exception; }

    private <T> void background(Activity activity, File dataDir, String message,
                                boolean reading, Work<T> work, Ready<T> ready) {
        AtomicBoolean canceled = new AtomicBoolean();
        PluginDialog.Builder builder = new PluginDialog.Builder(activity)
            .setTitle(category + "剧情替换").setMessage(message);
        if (reading) builder.setNegativeButton("取消", (dialog, which) -> canceled.set(true))
            .setOnCancelListener(dialog -> canceled.set(true));
        Dialog progress = builder.create();
        progress.setCancelable(reading);
        progress.setCanceledOnTouchOutside(false);
        progress.show();
        new Thread(() -> {
            T value = null;
            Exception failure = null;
            try { value = work.run(); }
            catch (Exception error) { failure = error; }
            final T result = value;
            final Exception error = failure;
            activity.runOnUiThread(() -> {
                if (activity.isFinishing() || activity.isDestroyed()) return;
                progress.dismiss();
                if (canceled.get()) return;
                try {
                    if (error != null) throw error;
                    ready.run(result);
                } catch (Exception problem) {
                    if (reading) showRecovery(activity, dataDir, problem);
                    else showError(activity, problem);
                }
            });
        }, "IdolyStoryCatalog").start();
    }

    private void showMenu(Activity activity, File dataDir, List<StoryItem> available,
                          JSONObject active) {
        try {
            String message = active == null ? (kind.equals("love") ? "选择想看的特殊活动剧情，再选择活动期间已开放的剧情入口。" :
                "选择想看的" + category + "剧情，再选择用于播放的" + category + "剧情入口。") :
                "当前入口：" + labelFor(available, active.getString("entry")) + "\n播放：" +
                labelFor(available, active.getString("source")) +
                "\n下次打开该入口时生效。";
            if (kind.equals("love")) message += "\n选项组数、每组选项数须一致。\n进度与条件分支仍由原入口决定。";
            PluginDialog.Builder menu = new PluginDialog.Builder(activity)
                .setTitle((kind.equals("love") ? "实验性 · " : "") + category + "剧情替换").setMessage(message)
                .setPositiveButton("设置替换", (dialog, which) ->
                    pick(activity, dataDir, available, "选择想看的剧情", null));
            if (active != null) menu.setNeutralButton("一键恢复", (dialog, which) -> {
                try {
                    clearState(dataDir);
                    Toast.makeText(activity, "已恢复入口原有剧情；下次打开时生效", Toast.LENGTH_LONG).show();
                } catch (Exception error) { showError(activity, error); }
            });
            menu.setNegativeButton("关闭", null).show();
        } catch (Exception error) { showRecovery(activity, dataDir, error); }
    }

    private void pick(Activity activity, File dataDir, List<StoryItem> available,
                             String title, StoryItem source) {
        Dialog dialog = new Dialog(activity, android.R.style.Theme_Material_Light_NoActionBar);
        android.content.Context context = dialog.getContext();
        Runnable back = () -> {
            if (source == null) showMenu(activity, dataDir);
            else pick(activity, dataDir, available, "选择想看的剧情", null);
            dialog.dismiss();
        };
        SettingsUiStyle.Page page = new SettingsUiStyle.Page(dialog,
            source == null ? "实际播放剧情" : "打开哪个入口",
            source == null ? "第 1 步 / 2 · 选择想看的内容，仅显示本机已有兼容译文的剧情。" :
                "第 2 步 / 2 · 选择你在游戏中已经能打开的" + category + "剧情入口。" +
                    (kind.equals("love") ? "仅显示选项数量一致的入口。" : ""),
            "返回", back, false);
        if (source != null) {
            TextView selection = SettingsUiStyle.text(context, "实际播放：" + source.label(),
                14, SettingsUiStyle.ACCENT, true);
            selection.setMaxLines(3);
            selection.setEllipsize(android.text.TextUtils.TruncateAt.END);
            selection.setPadding(0, 0, 0, SettingsUiStyle.dp(context, 12));
            page.body.addView(selection);
        }
        EditText search = PluginDialog.input(context);
        search.setSingleLine(true);
        search.setHint("搜索" + category + "名、剧情名或文件名");
        search.setImeOptions(android.view.inputmethod.EditorInfo.IME_ACTION_SEARCH);
        page.body.addView(search, new LinearLayout.LayoutParams(-1, -2));
        TextView count = SettingsUiStyle.text(context, "", 13, SettingsUiStyle.MUTED, false);
        count.setPadding(0, SettingsUiStyle.dp(context, 10), 0, SettingsUiStyle.dp(context, 10));
        LinearLayout filters = new LinearLayout(context);
        filters.setGravity(android.view.Gravity.CENTER_VERTICAL);
        filters.setPadding(0, SettingsUiStyle.dp(context, 8), 0, SettingsUiStyle.dp(context, 8));
        filters.addView(count, new LinearLayout.LayoutParams(0, -2, 1));
        android.widget.Button characterFilter = SettingsUiStyle.button(context,
            (kind.equals("card") ? "角色" : "活动") + "：全部 ▾", false, () -> {});
        filters.addView(characterFilter, new LinearLayout.LayoutParams(-2, -2));
        page.body.addView(filters);
        // null means all characters; an empty ID means the explicit unclassified group.
        String[] selectedCharacter = {null};
        java.util.Map<String, String> characters = new java.util.TreeMap<>();
        for (StoryItem item : available) {
            if (source == null || !item.file.equals(source.file))
                characters.put(item.characterId, item.characterName);
        }
        ListView list = new ListView(context);
        list.setDivider(new android.graphics.drawable.ColorDrawable(SettingsUiStyle.BACKGROUND));
        list.setDividerHeight(SettingsUiStyle.dp(context, 8));
        list.setClipToPadding(false);
        list.setVerticalScrollBarEnabled(false);
        ArrayAdapter<String> adapter = new ArrayAdapter<String>(context,
            android.R.layout.simple_list_item_1, new ArrayList<>()) {
            @Override public View getView(int position, View convertView, ViewGroup parent) {
                TextView row = (TextView) super.getView(position, convertView, parent);
                row.setTextColor(SettingsUiStyle.INK);
                row.setTextSize(16);
                row.setSingleLine(false);
                row.setMinHeight(SettingsUiStyle.dp(context, 72));
                row.setPadding(SettingsUiStyle.dp(context, 16), SettingsUiStyle.dp(context, 16),
                    SettingsUiStyle.dp(context, 16), SettingsUiStyle.dp(context, 16));
                row.setLineSpacing(SettingsUiStyle.dp(context, 4), 1);
                row.setBackground(new android.graphics.drawable.RippleDrawable(
                    android.content.res.ColorStateList.valueOf(0x224169df),
                    SettingsUiStyle.shape(context, 0xffffffff, 14), null));
                return row;
            }
        };
        list.setAdapter(adapter);
        page.body.addView(list, new LinearLayout.LayoutParams(-1, 0, 1));
        List<StoryItem> shown = new ArrayList<>();
        Runnable filter = () -> {
            String query = search.getText().toString().trim().toLowerCase(Locale.ROOT);
            shown.clear(); adapter.clear();
            for (StoryItem item : available) {
                if (source != null && item.file.equals(source.file)) continue;
                if (source != null && kind.equals("love") && !source.choiceCounts.equals(item.choiceCounts)) continue;
                if (selectedCharacter[0] != null &&
                    !selectedCharacter[0].equals(item.characterId)) continue;
                if (!query.isEmpty() && !item.search().contains(query)) continue;
                shown.add(item); adapter.add((kind.equals("card") ? item.characterName + " · " : "") + item.groupName +
                    "\n" + item.displayLabel + (kind.equals("love") ? "\n" + item.choiceSummary() : ""));
            }
            adapter.notifyDataSetChanged();
            count.setText(shown.isEmpty() ? "无匹配结果，可更换筛选或关键词" :
                "共 " + shown.size() + " 段剧情 · 点击选择");
        };
        characterFilter.setOnClickListener(ignored -> {
            List<String> ids = new ArrayList<>();
            List<String> labels = new ArrayList<>();
            ids.add(null);
            labels.add(selectedCharacter[0] == null ? "全部 · 当前" : "全部");
            for (java.util.Map.Entry<String, String> character : characters.entrySet()) {
                ids.add(character.getKey());
                labels.add(character.getValue() + (character.getKey().equals(selectedCharacter[0]) ?
                    " · 当前" : ""));
            }
            new PluginDialog.Builder(activity).setTitle(kind.equals("card") ? "按角色筛选" : "按活动筛选")
                .setItems(labels.toArray(new String[0]), (choice, index) -> {
                    selectedCharacter[0] = ids.get(index);
                    characterFilter.setText((kind.equals("card") ? "角色：" : "活动：") + (index == 0 ? "全部" :
                        characters.get(selectedCharacter[0])) + " ▾");
                    filter.run();
                }).setNegativeButton("取消", null).show();
        });
        search.addTextChangedListener(new TextWatcher() {
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
            public void onTextChanged(CharSequence s, int start, int before, int count) { filter.run(); }
            public void afterTextChanged(Editable s) {}
        });
        search.setOnEditorActionListener((view, action, event) -> {
            android.view.inputmethod.InputMethodManager keyboard =
                (android.view.inputmethod.InputMethodManager) context.getSystemService(
                    android.content.Context.INPUT_METHOD_SERVICE);
            if (keyboard != null) keyboard.hideSoftInputFromWindow(search.getWindowToken(), 0);
            search.clearFocus();
            return true;
        });
        list.setOnItemClickListener((parent, view, position, id) -> {
            StoryItem chosen = shown.get(position);
            if (source == null) pick(activity, dataDir, available, "选择容易打开的入口", chosen);
            else confirm(activity, dataDir, available, chosen, source);
            dialog.dismiss();
        });
        filter.run();
        dialog.setContentView(page.root);
        dialog.setCanceledOnTouchOutside(false);
        dialog.setOnCancelListener(ignored -> back.run());
        page.root.setFocusableInTouchMode(true);
        page.root.requestFocus();
        SettingsUiStyle.configureWindow(dialog);
        if (dialog.getWindow() != null) dialog.getWindow().setSoftInputMode(
            WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE |
            WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN);
        dialog.show();
    }

    private void confirm(Activity activity, File dataDir, List<StoryItem> available,
                                StoryItem entry, StoryItem source) {
        new PluginDialog.Builder(activity).setTitle("确认替换方向")
            .setMessage("打开这个入口\n" + entry.label() + "\n\n实际播放剧情\n" + source.label() +
                "\n\n下次进入时生效，可随时恢复。" +
                (kind.equals("love") ? "\n实验性：选项组数、每组选项数须一致。\n仅限活动开放期间，进度与条件分支仍由原入口决定。" : ""))
            .setPositiveButton("启用替换", (dialog, which) ->
                background(activity, dataDir, "正在校验并保存替换设置…", false, () -> {
                    List<StoryItem> current = loadAvailable(activity, dataDir);
                    readState(dataDir, current);
                    StoryItem currentEntry = itemFor(current, entry.file);
                    StoryItem currentSource = itemFor(current, source.file);
                    requireInstalled(dataDir, currentEntry.file);
                    requireInstalled(dataDir, currentSource.file);
                    if (!compatibleChoices(dataDir, currentEntry.file, currentSource.file))
                        throw new Exception("两段剧情的选项结构不同，请选择其他入口");
                    writeState(dataDir, currentEntry, currentSource);
                    return null;
                }, ignored -> Toast.makeText(activity, "替换已保存；下次打开入口时生效", Toast.LENGTH_LONG).show()))
            .setNegativeButton("取消", null).show();
    }

    private List<StoryItem> loadAvailable(Activity context, File dataDir) throws Exception {
        if (TextHotUpdater.busy()) throw new Exception("文本正在更新，请完成后重新打开剧情替换");
        JSONArray rows = StoryCatalog.load(dataDir, CATALOG,
            name -> HookEntry.readBuiltInAsset(context, name));
        List<StoryItem> result = new ArrayList<>();
        for (int i = 0; i < rows.length(); i++) {
            StoryItem item = new StoryItem(rows.getJSONObject(i));
            if (installed(dataDir, item.file)) {
                if (kind.equals("love")) item.choiceCounts = choiceShape(dataDir, item.file);
                result.add(item);
            }
        }
        if (result.isEmpty()) throw new Exception("未找到已安装的" + category + "剧情译文");
        return result;
    }

    private JSONObject readState(File dataDir, List<StoryItem> available) throws Exception {
        File file = new File(dataDir, STATE);
        if (!file.exists()) return null;
        if (!file.isFile() || file.length() < 1 || file.length() > 4096)
            throw new Exception("现有替换设置无效，请恢复后重新设置");
        JSONObject value = new JSONObject(new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8));
        String entry = value.getString("entry"), source = value.getString("source");
        int version = value.getInt("schema_version");
        if (!((version == 1 && value.length() == 3) || (version == 2 && value.length() == 7)) ||
            !validName(entry) || !validName(source) || entry.equals(source) ||
            !contains(available, entry) || !contains(available, source))
            throw new Exception("现有替换设置与剧情目录或本地资源不一致，未作修改");
        if (version == 2) {
            for (StoryItem story : available) {
                String prefix = story.file.equals(entry) ? "entry" : story.file.equals(source) ? "source" : null;
                if (prefix != null && (!story.rawTitle.equals(value.getString(prefix + "_title")) ||
                        story.episode != value.getInt(prefix + "_episode")))
                    throw new Exception("现有替换标题与剧情目录不一致，请恢复后重新设置");
            }
        }
        return value;
    }

    private boolean validName(String name) {
        return name.matches("adv_" + kind + "_[a-z0-9_]+\\.txt") && !name.endsWith("_short.txt");
    }
    private boolean contains(List<StoryItem> stories, String file) {
        for (StoryItem story : stories) if (story.file.equals(file)) return true;
        return false;
    }
    private StoryItem itemFor(List<StoryItem> stories, String file) throws Exception {
        for (StoryItem item : stories) if (item.file.equals(file)) return item;
        throw new Exception("剧情目录已更新，请重新选择剧情");
    }
    private String labelFor(List<StoryItem> stories, String file) {
        for (StoryItem story : stories) if (story.file.equals(file)) return story.label();
        return file;
    }
    private boolean installed(File dataDir, String file) {
        if (!validName(file)) return false;
        File target = new File(new File(dataDir, "resource"), file);
        try {
            if (Files.isSymbolicLink(target.toPath()) || !target.isFile() ||
                target.length() <= 0 || target.length() > 8 * 1024 * 1024) return false;
            if (kind.equals("card")) return true;
            String script = new String(Files.readAllBytes(target.toPath()), StandardCharsets.UTF_8);
            return script.startsWith("[") && script.trim().endsWith("[timeline]") &&
                script.indexOf('\0') < 0 &&
                (!kind.equals("event") || (!script.contains("[choicegroup") && !script.contains("[branchgroup")));
        } catch (Exception error) { return false; }
    }
    private void requireInstalled(File dataDir, String file) throws Exception {
        if (!installed(dataDir, file)) throw new Exception("剧情文件已不存在：" + file);
    }
    private List<Integer> choiceShape(File dataDir, String file) throws Exception {
        String script = new String(Files.readAllBytes(new File(new File(dataDir, "resource"),
            file).toPath()), StandardCharsets.UTF_8);
        List<Integer> result = new ArrayList<>();
        for (String line : script.split("\n")) {
            if (!line.startsWith("[choicegroup ")) continue;
            int count = 0, offset = 0;
            while ((offset = line.indexOf("choices=[choice ", offset)) >= 0) {
                count++;
                offset += 16;
            }
            result.add(count);
        }
        return result;
    }
    private boolean compatibleChoices(File dataDir, String entry, String source) throws Exception {
        return !kind.equals("love") || choiceShape(dataDir, entry).equals(choiceShape(dataDir, source));
    }
    private void writeState(File dataDir, StoryItem entry, StoryItem source) throws Exception {
        if (!validName(entry.file) || !validName(source.file) || entry.file.equals(source.file))
            throw new Exception("替换方向无效");
        byte[] bytes = new JSONObject().put("schema_version", 2).put("entry", entry.file)
            .put("source", source.file).put("entry_title", entry.rawTitle).put("source_title", source.rawTitle)
            .put("entry_episode", entry.episode).put("source_episode", source.episode).toString().getBytes(StandardCharsets.UTF_8);
        Path target = new File(dataDir, STATE).toPath();
        Path temp = new File(dataDir, STATE + ".tmp").toPath();
        try {
            try (FileOutputStream output = new FileOutputStream(temp.toFile())) {
                output.write(bytes);
                output.getFD().sync();
            }
            Files.move(temp, target, StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
        } finally { Files.deleteIfExists(temp); }
    }
    private void clearState(File dataDir) throws Exception {
        if (!Files.deleteIfExists(new File(dataDir, STATE).toPath()))
            throw new Exception("替换设置已不存在");
    }
    private void showRecovery(Activity activity, File dataDir, Exception error) {
        File state = new File(dataDir, STATE);
        PluginDialog.Builder dialog = new PluginDialog.Builder(activity)
            .setTitle("无法读取剧情替换")
            .setMessage((error.getMessage() == null ? "剧情目录或本地资源无效。" : error.getMessage()) +
                (state.exists() ? "\n可清除替换设置以恢复入口原有剧情。" : ""))
            .setNegativeButton("关闭", null);
        if (state.exists()) dialog.setPositiveButton("恢复入口原有剧情", (ignored, which) -> {
            try {
                clearState(dataDir);
                Toast.makeText(activity, "已清除替换设置；下次打开入口时生效", Toast.LENGTH_LONG).show();
            } catch (Exception failure) { showError(activity, failure); }
        });
        dialog.show();
    }
    private void showError(Activity activity, Exception error) {
        new PluginDialog.Builder(activity).setTitle("无法设置剧情替换")
            .setMessage(error.getMessage() == null ? "请检查本地剧情文件。" : error.getMessage())
            .setPositiveButton("确定", null).show();
    }
}
