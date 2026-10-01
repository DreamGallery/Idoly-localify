package io.github.dreamgallery.idoly.localify;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.widget.Toast;

/** User-controlled feedback; capture contents are never placed in a URL. */
final class FeedbackUi {
    static final String REPOSITORY = "https://github.com/DreamGallery/Idoly-localify-translations";
    private static final String TEMPLATE = "### 出现问题的页面\n\n"
        + "### 操作步骤\n1. \n\n### 实际显示与期望显示\n\n"
        + "### 游戏及插件版本\n\n### 截图与采集文件\n"
        + "请检查并移除账号 ID、昵称等个人信息，再手动附加截图或采集文件。\n";

    private FeedbackUi() {}

    static void show(Activity activity) {
        new PluginDialog.Builder(activity).setTitle("反馈漏翻或显示问题")
            .setMessage("先从悬浮窗开始采集，重现问题后停止并导出。文件保存在下载 / Idoly-localify。\n\n"
                + "打开 GitHub 后填写页面、操作步骤和显示问题，并手动附加截图或采集记录。"
                + "公开提交前请检查并移除个人信息；插件不会自动上传文件。")
            .setPositiveButton("新建 Issue", (dialog, which) -> open(activity,
                Uri.parse(REPOSITORY + "/issues/new").buildUpon()
                    .appendQueryParameter("body", TEMPLATE).build().toString()))
            .setNeutralButton("复制反馈模板", (dialog, which) -> {
                ClipboardManager clipboard = (ClipboardManager) activity.getSystemService(Context.CLIPBOARD_SERVICE);
                if (clipboard != null) {
                    clipboard.setPrimaryClip(ClipData.newPlainText("本地化问题反馈", TEMPLATE));
                    Toast.makeText(activity, "反馈模板已复制", Toast.LENGTH_SHORT).show();
                }
            })
            .setNegativeButton("取消", null).show();
    }

    static void open(Activity activity, String url) {
        try { activity.startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url))); }
        catch (RuntimeException error) {
            Toast.makeText(activity, "无法打开浏览器，请访问 " + REPOSITORY, Toast.LENGTH_LONG).show();
        }
    }
}
