package io.github.dreamgallery.idoly.localify;

import android.app.Activity;
import android.app.Dialog;
import android.content.Context;
import android.graphics.Typeface;
import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Switch;
import java.io.File;

/** Full-window settings hosted by the game activity. */
final class LocalifySettingsUi {
    private static Dialog opened;
    private static UpdatePanel updatePanel;

    private LocalifySettingsUi() {}

    static void dismiss() {
        if (opened != null) opened.dismiss();
        opened = null;
        updatePanel = null;
    }

    static void refreshUpdateStatus() {
        if (opened != null && opened.isShowing() && updatePanel != null)
            updatePanel.refresh(opened.getContext());
    }

    static void show(Activity activity, Runnable update, Runnable address, Runnable font,
                     Runnable hide, Runnable story, Runnable eventStory, Runnable specialStory, boolean capturing,
                     File directory) {
        dismiss();
        Dialog dialog = new Dialog(activity, android.R.style.Theme_Material_Light_NoActionBar);
        SettingsUiStyle.Page page = new SettingsUiStyle.Page(dialog, "本地化设置",
            "管理文本更新、字体排版与采集工具。", "返回游戏", dialog::dismiss);
        LinearLayout translation = SettingsUiStyle.card(page.body, "翻译开关", "切换后重启游戏生效，包含文本、字体和运行时图片替换。");
        Switch enabled = new Switch(dialog.getContext());
        enabled.setText("启用翻译");
        SettingsUiStyle.styleSwitch(enabled);
        enabled.setChecked(TranslationPreferences.enabled(activity));
        boolean[] resetting = {false};
        enabled.setOnCheckedChangeListener((button, value) -> {
            if (resetting[0]) return;
            try {
                if (LocalDataUi.busy()) throw new IllegalStateException("请等待文件清理完成");
                if (value && TranslationPreferences.deleted(activity) && !new File(directory, "dict.json").isFile())
                    throw new IllegalStateException("请先下载文本，再开启翻译");
                TranslationPreferences.setEnabled(activity, value);
                android.widget.Toast.makeText(activity, "已保存，请重启游戏生效", android.widget.Toast.LENGTH_LONG).show();
            } catch (Exception error) {
                resetting[0] = true; enabled.setChecked(!value); resetting[0] = false;
                android.widget.Toast.makeText(activity, error.getMessage(), android.widget.Toast.LENGTH_LONG).show();
            }
        });
        translation.addView(enabled);
        if (TranslationPreferences.enabled(activity) != TranslationPreferences.active())
            translation.addView(SettingsUiStyle.text(dialog.getContext(), "开关已更改，等待重启。", 13, SettingsUiStyle.MUTED, false));
        UpdatePanel updates = new UpdatePanel(page.body, activity, directory, update, address);
        LinearLayout fonts = SettingsUiStyle.card(page.body, "字体与排版", "按场景调整字号、描边和行距，预览后再保存。");
        SettingsUiStyle.cardRow(fonts, "调整字号与排版", null, () -> { font.run(); dialog.dismiss(); });
        // Keep the visible settings page behind modal story tools; closing them returns here.
        LinearLayout stories = SettingsUiStyle.card(page.body, "剧情播放", "选择实际剧情与同类入口，可分别恢复。");
        SettingsUiStyle.cardRow(stories, "卡片剧情替换", null, story);
        SettingsUiStyle.cardRow(stories, "普通活动剧情替换", null, eventStory);
        SettingsUiStyle.cardRow(stories, "实验性 · 特殊活动剧情替换", null, specialStory);
        LinearLayout extras = SettingsUiStyle.card(page.body, "额外功能", null);
        SettingsUiStyle.cardRow(extras, "实验性 · 内容解锁与预览", null, () -> PreviewAccessUi.show(activity));
        LinearLayout capture = SettingsUiStyle.card(page.body,
            capturing ? "文本采集 · 进行中" : "文本采集 · 未开启",
            "在悬浮窗开始采集，浏览待检查的页面，再停止采集。文件会自动导出到系统下载目录，仅保存在本机。");
        TextView location = SettingsUiStyle.text(dialog.getContext(),
            "下载 / Idoly-localify\n文本、布局与完整性记录", 14, SettingsUiStyle.ACCENT, false);
        location.setLineSpacing(SettingsUiStyle.dp(dialog.getContext(), 4), 1);
        capture.addView(location);
        SettingsUiStyle.cardRow(capture, "删除采集文件", null, () -> LocalDataUi.captures(activity, directory));
        LinearLayout feedback = SettingsUiStyle.card(page.body, "文本与问题反馈", "提交漏翻和界面显示问题，或查看文本更新仓库。");
        SettingsUiStyle.cardRow(feedback, "反馈漏翻或显示问题", "GitHub Issue", () -> FeedbackUi.show(activity));
        SettingsUiStyle.cardRow(feedback, "文本更新仓库", "GitHub", () -> FeedbackUi.open(activity, FeedbackUi.REPOSITORY));
        LinearLayout overlay = SettingsUiStyle.card(page.body, "悬浮工具",
            "收起时吸附边缘并自动变淡。剧情结束或退出后自动恢复；需要在剧情中调试时可关闭下方开关。");
        android.content.SharedPreferences prefs = activity.getSharedPreferences("idoly-localify-overlay", 0);
        Switch autoHide = new Switch(dialog.getContext());
        autoHide.setText("剧情播放时自动隐藏");
        SettingsUiStyle.styleSwitch(autoHide);
        autoHide.setChecked(prefs.getBoolean("hide_during_story", true));
        autoHide.setOnCheckedChangeListener((button, checked) -> {
            prefs.edit().putBoolean("hide_during_story", checked).apply();
            LocalifyOverlayUi.refreshVisibility();
        });
        overlay.addView(autoHide, new LinearLayout.LayoutParams(-1, -2));
        SettingsUiStyle.cardRow(overlay, "临时隐藏悬浮工具", null, () -> { dialog.dismiss(); hide.run(); });
        TextView restoreHint = SettingsUiStyle.text(dialog.getContext(),
            "临时隐藏后，将游戏切到后台再返回即可恢复；剧情播放中的自动隐藏仍然生效。", 13, SettingsUiStyle.MUTED, false);
        restoreHint.setPadding(0, 0, 0, SettingsUiStyle.dp(activity, 8));
        overlay.addView(restoreHint);
        dialog.setContentView(page.root);
        dialog.setOnDismissListener(ignored -> { if (opened == dialog) {
            opened = null; updatePanel = null;
        } });
        opened = dialog;
        updatePanel = updates;
        SettingsUiStyle.configureWindow(dialog);
        dialog.show();
    }

    private static final class VersionRow {
        final TextView value, state;

        VersionRow(LinearLayout parent, String label) {
            Context context = parent.getContext();
            LinearLayout row = new LinearLayout(context);
            row.setOrientation(LinearLayout.VERTICAL);
            row.setPadding(0, SettingsUiStyle.dp(context, 12), 0, SettingsUiStyle.dp(context, 8));
            LinearLayout heading = new LinearLayout(context);
            heading.setGravity(Gravity.CENTER_VERTICAL);
            heading.addView(SettingsUiStyle.text(context, label, 15, SettingsUiStyle.INK, false),
                new LinearLayout.LayoutParams(0, -2, 1));
            value = SettingsUiStyle.text(context, "", 13, SettingsUiStyle.ACCENT, false);
            value.setTypeface(Typeface.MONOSPACE);
            value.setGravity(Gravity.END);
            value.setMaxWidth(SettingsUiStyle.dp(context, 210));
            value.setPadding(SettingsUiStyle.dp(context, 12), 0, 0, 0);
            heading.addView(value, new LinearLayout.LayoutParams(-2, -2));
            row.addView(heading);
            state = SettingsUiStyle.text(context, "", 12, SettingsUiStyle.MUTED, false);
            state.setPadding(0, SettingsUiStyle.dp(context, 4), 0, 0);
            row.addView(state);
            parent.addView(row, new LinearLayout.LayoutParams(-1, -2));
        }
    }

    private static final class UpdatePanel {
        final File directory;
        final VersionRow plugin, text;
        final TextView progress, checkButton, downloadButton;

        UpdatePanel(LinearLayout parent, Activity activity, File directory, Runnable update, Runnable address) {
            this.directory = directory;
            Context context = parent.getContext();
            LinearLayout card = SettingsUiStyle.card(parent, "版本与更新", null);
            plugin = new VersionRow(card, "插件版本");
            plugin.value.setText(BuildConfig.VERSION_NAME);
            text = new VersionRow(card, "文本版本");
            LinearLayout actions = new LinearLayout(context);
            checkButton = SettingsUiStyle.button(context, "检查更新", false,
                () -> UpdateChecker.check(activity, directory, true));
            downloadButton = SettingsUiStyle.button(context, "更新文本", true, update);
            LinearLayout.LayoutParams first = new LinearLayout.LayoutParams(0, -2, 1);
            first.setMarginEnd(SettingsUiStyle.dp(context, 10));
            actions.addView(checkButton, first);
            actions.addView(downloadButton, new LinearLayout.LayoutParams(0, -2, 1));
            LinearLayout.LayoutParams actionLayout = new LinearLayout.LayoutParams(-1, -2);
            actionLayout.topMargin = SettingsUiStyle.dp(context, 12);
            actionLayout.bottomMargin = SettingsUiStyle.dp(context, 8);
            card.addView(actions, actionLayout);
            progress = SettingsUiStyle.text(context, "", 13, SettingsUiStyle.MUTED, false);
            progress.setPadding(0, SettingsUiStyle.dp(context, 4), 0, SettingsUiStyle.dp(context, 8));
            progress.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
            card.addView(progress);
            SettingsUiStyle.divider(card);
            SettingsUiStyle.cardRow(card, "更新地址", null, address);
            SettingsUiStyle.cardRow(card, "插件下载", "GitHub Release",
                () -> FeedbackUi.open(activity, UpdateChecker.downloadPage(activity)));
            TextView hint = SettingsUiStyle.text(context,
                "LSPatch 需重新修补游戏；LSPosed 更新模块后重启。", 12, SettingsUiStyle.MUTED, false);
            hint.setLineSpacing(SettingsUiStyle.dp(context, 3), 1);
            hint.setPadding(0, 0, 0, SettingsUiStyle.dp(context, 8));
            card.addView(hint);
            Switch startup = new Switch(context);
            startup.setText("启动时检查更新");
            SettingsUiStyle.styleSwitch(startup);
            startup.setChecked(UpdateChecker.enabled(activity));
            startup.setOnCheckedChangeListener((button, checked) -> UpdateChecker.setEnabled(activity, checked));
            card.addView(startup, new LinearLayout.LayoutParams(-1, -2));
            SettingsUiStyle.divider(card);
            SettingsUiStyle.cardRow(card, "删除本地翻译文件", null,
                () -> LocalDataUi.translations(activity, directory));
            refresh(context);
        }

        void refresh(Context context) {
            UpdateChecker.Status status = UpdateChecker.status(context, directory);
            text.value.setText(status.textVersion);
            plugin.state.setText("插件已是最新".equals(status.pluginState) ? "已是最新" : status.pluginState);
            text.state.setText("文本已是最新".equals(status.textState) ? "已是最新" : status.textState);
            progress.setText(status.progress);
            progress.setVisibility(status.progress.isEmpty() ? View.GONE : View.VISIBLE);
            boolean busy = TextHotUpdater.busy();
            checkButton.setText(status.checking ? "正在检查" : "检查更新");
            checkButton.setEnabled(!status.checking && !busy);
            checkButton.setAlpha(checkButton.isEnabled() ? 1 : .55f);
            downloadButton.setEnabled(!busy);
            downloadButton.setAlpha(downloadButton.isEnabled() ? 1 : .55f);
        }
    }
}
