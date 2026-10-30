package io.github.dreamgallery.idoly.localify;

import android.app.Activity;
import android.app.Dialog;
import android.widget.LinearLayout;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;
import org.json.JSONArray;
import org.json.JSONObject;

/** Settings for temporary resource views, independent of user inventory files. */
final class PreviewAccessUi {
    private PreviewAccessUi() {}

    static void show(Activity activity) {
        Dialog dialog = new Dialog(activity, android.R.style.Theme_Material_Light_NoActionBar);
        SettingsUiStyle.Page page = new SettingsUiStyle.Page(dialog, "实验性 · 内容预览",
            "仅当前进程生效；切换后重新打开对应页面。退出游戏后全部关闭。", "返回设置", dialog::dismiss);
        LinearLayout options = SettingsUiStyle.card(page.body, "预览类别",
            "扩展列表不改变账号持有数据。正式拍摄会向服务器提交所选内容；资源由游戏按需联网下载。");
        try {
            JSONObject status = new JSONObject(HookEntry.previewAccessStatus());
            TextView appearanceState = SettingsUiStyle.text(dialog.getContext(), formatAppearanceState(status),
                12, SettingsUiStyle.MUTED, false);
            JSONArray categories = status.getJSONArray("categories");
            for (int i = 0; i < categories.length(); ++i) {
                JSONObject category = categories.getJSONObject(i);
                int id = category.getInt("id");
                String title = id == 1 ? "服装" : id == 2 ? "发型与发饰" : id == 4 ? "拍摄姿势" : "名片装饰";
                Switch toggle = new Switch(dialog.getContext());
                toggle.setText(title);
                SettingsUiStyle.styleSwitch(toggle);
                toggle.setChecked(category.optBoolean("enabled"));
                toggle.setEnabled(category.optBoolean("available"));
                TextView state = SettingsUiStyle.text(dialog.getContext(), categoryState(category),
                    12, SettingsUiStyle.MUTED, false);
                boolean[] resetting = {false};
                toggle.setOnCheckedChangeListener((button, checked) -> {
                    if (resetting[0]) return;
                    if (!HookEntry.setPreviewAccess(id, checked)) {
                        resetting[0] = true;
                        toggle.setChecked(!checked);
                        resetting[0] = false;
                        Toast.makeText(activity, "当前游戏版本暂不支持此类别", Toast.LENGTH_LONG).show();
                    } else {
                        state.setText(checked ? "已开启，请重新打开对应页面" : "已关闭，请重新打开对应页面");
                        try {
                            appearanceState.setText(formatAppearanceState(new JSONObject(HookEntry.previewAccessStatus())));
                        } catch (Exception ignored) {
                            appearanceState.setText("请重新打开设置查看本机外观状态");
                        }
                        Toast.makeText(activity, "已切换，请重新打开对应页面", Toast.LENGTH_SHORT).show();
                    }
                });
                options.addView(toggle, new LinearLayout.LayoutParams(-1, -2));
                options.addView(state);
            }
            if (categories.length() == 0)
                options.addView(SettingsUiStyle.text(dialog.getContext(), "请在游戏加载完成后重试。", 13, SettingsUiStyle.MUTED, false));
            LinearLayout appearance = SettingsUiStyle.card(page.body, "本机外观",
                "普通换装页确认后，外观在本次进程内保留，不改账号持有或穿戴记录。日常和演唱会分别保存；关闭开关、恢复或退出游戏可还原。重新打开页面后生效，剧情指定服装保持原样。");
            appearance.addView(appearanceState);
            LinearLayout.LayoutParams restoreLayout = new LinearLayout.LayoutParams(-1, -2);
            restoreLayout.topMargin = SettingsUiStyle.dp(dialog.getContext(), 12);
            restoreLayout.bottomMargin = SettingsUiStyle.dp(dialog.getContext(), 8);
            appearance.addView(SettingsUiStyle.button(dialog.getContext(), "恢复原穿戴外观", false, () -> {
                HookEntry.clearPreviewAppearances();
                appearanceState.setText("本机外观已清除");
                Toast.makeText(activity, "已恢复，请重新打开对应页面", Toast.LENGTH_SHORT).show();
            }), restoreLayout);
            SettingsUiStyle.card(page.body, "名片装饰",
                "开启后重新进入名片背景编辑页。确认只保留本机装饰，不提交名片修改；关闭开关或重启后恢复，重新打开页面生效。");
        } catch (Exception error) {
            options.addView(SettingsUiStyle.text(dialog.getContext(), "暂时无法读取功能状态。", 13, SettingsUiStyle.MUTED, false));
        }
        SettingsUiStyle.card(page.body, "离线版",
            "离线版 7.0.0 尚待验证。周边和称号暂未开放；卡片插画、剧情和消息按官方公告将由离线版直接开放。");
        dialog.setContentView(page.root);
        SettingsUiStyle.configureWindow(dialog);
        dialog.show();
    }

    private static String formatAppearanceState(JSONObject status) {
        return status.optBoolean("wardrobe_available") ? "已保留 " + status.optInt("appearances") + " 组外观"
            : "当前版本的普通换装接口不可用，仅开放拍摄列表";
    }

    private static String categoryState(JSONObject category) {
        if (!category.optBoolean("available")) return "读取接口不可用";
        if (!category.optBoolean("enabled")) return "已关闭";
        if (!category.optString("error").isEmpty()) return "临时读取失败，已保留原持有列表";
        int rows = category.optInt("rows");
        return rows > 0 ? "可预览 " + rows + " 项" : "已开启，等待打开对应页面";
    }
}
