package io.github.dreamgallery.idoly.localify;

import android.app.Activity;
import android.app.Dialog;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Switch;
import android.content.res.ColorStateList;
import java.io.File;

/** Full-window settings hosted by the game activity. */
final class LocalifySettingsUi {
    private static Dialog opened;
    private static TextView updateStatus;
    private static File updateDirectory;

    private LocalifySettingsUi() {}

    static void dismiss() {
        if (opened != null) opened.dismiss();
        opened = null;
        updateStatus = null;
        updateDirectory = null;
    }

    static void refreshUpdateStatus() {
        if (opened == null || !opened.isShowing() || updateStatus == null) return;
        updateStatus.setText(UpdateChecker.status(opened.getContext(), updateDirectory));
    }

    static void show(Activity activity, Runnable update, Runnable address, Runnable font,
                     Runnable hide, Runnable story, Runnable eventStory, Runnable specialStory, boolean capturing,
                     File directory) {
        dismiss();
        Dialog dialog = new Dialog(activity, android.R.style.Theme_Material_Light_NoActionBar);
        SettingsUiStyle.Page page = new SettingsUiStyle.Page(dialog, "本地化设置",
            "管理文本更新、字体排版与采集工具。", "返回游戏", dialog::dismiss);
        LinearLayout updates = SettingsUiStyle.card(page.body, "版本与更新", "检查版本不会自动下载或安装。");
        TextView status = SettingsUiStyle.text(dialog.getContext(), UpdateChecker.status(activity, directory),
            14, SettingsUiStyle.MUTED, false);
        status.setLineSpacing(SettingsUiStyle.dp(activity, 4), 1);
        updates.addView(status);
        SettingsUiStyle.row(updates, "检查插件与文本版本", null,
            () -> UpdateChecker.check(activity, directory, true));
        updates.addView(SettingsUiStyle.button(dialog.getContext(), "下载并更新文本", true, update),
            new LinearLayout.LayoutParams(-1, -2));
        SettingsUiStyle.row(updates, "更新地址", null, address);
        SettingsUiStyle.row(updates, "插件下载", "GitHub Release",
            () -> FeedbackUi.open(activity, UpdateChecker.downloadPage(activity)));
        TextView pluginHint = SettingsUiStyle.text(dialog.getContext(),
            "LSPatch：下载后重新嵌入游戏。LSPosed：安装模块后重启游戏。", 13, SettingsUiStyle.MUTED, false);
        updates.addView(pluginHint);
        Switch checkStartup = new Switch(dialog.getContext());
        checkStartup.setText("启动时检查更新");
        checkStartup.setTextSize(16);
        checkStartup.setTextColor(SettingsUiStyle.INK);
        checkStartup.setMinHeight(SettingsUiStyle.dp(activity, 56));
        checkStartup.setChecked(UpdateChecker.enabled(activity));
        checkStartup.setOnCheckedChangeListener((button, enabled) -> UpdateChecker.setEnabled(activity, enabled));
        updates.addView(checkStartup);
        LinearLayout fonts = SettingsUiStyle.card(page.body, "字体与排版", "按场景调整字号、描边和行距，预览后再保存。");
        SettingsUiStyle.row(fonts, "调整字号与排版", null, () -> { font.run(); dialog.dismiss(); });
        // Keep the visible settings page behind modal story tools; closing them returns here.
        LinearLayout stories = SettingsUiStyle.card(page.body, "剧情播放", "选择实际剧情与同类入口，可分别恢复。");
        SettingsUiStyle.row(stories, "卡片剧情替换", null, story);
        SettingsUiStyle.row(stories, "普通活动剧情替换", null, eventStory);
        SettingsUiStyle.row(stories, "实验性 · 特殊活动剧情替换", null, specialStory);
        LinearLayout capture = SettingsUiStyle.card(page.body,
            capturing ? "文本采集 · 进行中" : "文本采集 · 未开启",
            "在悬浮窗开始采集，浏览待检查的页面，再停止采集。文件会自动导出到系统下载目录，仅保存在本机。");
        TextView location = SettingsUiStyle.text(dialog.getContext(),
            "下载 / Idoly-localify\n文本、布局与完整性记录", 14, SettingsUiStyle.ACCENT, false);
        location.setLineSpacing(SettingsUiStyle.dp(dialog.getContext(), 4), 1);
        capture.addView(location);
        LinearLayout feedback = SettingsUiStyle.card(page.body, "文本与问题反馈", "提交漏翻和界面显示问题，或查看文本更新仓库。");
        SettingsUiStyle.row(feedback, "反馈漏翻或显示问题", "GitHub Issue", () -> FeedbackUi.show(activity));
        SettingsUiStyle.row(feedback, "文本更新仓库", "GitHub", () -> FeedbackUi.open(activity, FeedbackUi.REPOSITORY));
        LinearLayout overlay = SettingsUiStyle.card(page.body, "悬浮工具",
            "收起时吸附边缘并自动变淡。剧情结束或退出后自动恢复；需要在剧情中调试时可关闭下方开关。");
        android.content.SharedPreferences prefs = activity.getSharedPreferences("idoly-localify-overlay", 0);
        Switch autoHide = new Switch(dialog.getContext());
        autoHide.setText("剧情播放时自动隐藏");
        autoHide.setTextSize(16);
        autoHide.setTextColor(SettingsUiStyle.INK);
        autoHide.setPadding(SettingsUiStyle.dp(activity, 10), SettingsUiStyle.dp(activity, 12),
            SettingsUiStyle.dp(activity, 10), SettingsUiStyle.dp(activity, 12));
        autoHide.setMinHeight(SettingsUiStyle.dp(activity, 56));
        autoHide.setThumbTintList(new ColorStateList(new int[][]{new int[]{android.R.attr.state_checked}, new int[]{}},
            new int[]{SettingsUiStyle.ACCENT, 0xff9699aa}));
        autoHide.setChecked(prefs.getBoolean("hide_during_story", true));
        autoHide.setOnCheckedChangeListener((button, enabled) -> {
            prefs.edit().putBoolean("hide_during_story", enabled).apply();
            LocalifyOverlayUi.refreshVisibility();
        });
        overlay.addView(autoHide, new LinearLayout.LayoutParams(-1, -2));
        SettingsUiStyle.row(overlay, "临时隐藏悬浮工具", null, () -> { dialog.dismiss(); hide.run(); });
        TextView restoreHint = SettingsUiStyle.text(dialog.getContext(),
            "临时隐藏后，将游戏切到后台再返回即可恢复；剧情播放中的自动隐藏仍然生效。", 13, SettingsUiStyle.MUTED, false);
        restoreHint.setPadding(SettingsUiStyle.dp(activity, 10), 0, SettingsUiStyle.dp(activity, 10), SettingsUiStyle.dp(activity, 8));
        overlay.addView(restoreHint);
        dialog.setContentView(page.root);
        dialog.setOnDismissListener(ignored -> { if (opened == dialog) {
            opened = null; updateStatus = null; updateDirectory = null;
        } });
        opened = dialog;
        updateStatus = status;
        updateDirectory = directory;
        SettingsUiStyle.configureWindow(dialog);
        dialog.show();
    }
}
