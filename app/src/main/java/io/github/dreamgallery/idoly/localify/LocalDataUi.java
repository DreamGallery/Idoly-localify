package io.github.dreamgallery.idoly.localify;

import android.app.Activity;
import android.content.ContentUris;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.MediaStore;
import android.widget.Toast;
import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;

final class LocalDataUi {
    private static final AtomicBoolean BUSY = new AtomicBoolean();
    static boolean busy() { return BUSY.get(); }

    static void translations(Activity activity, File directory) {
        new PluginDialog.Builder(activity).setTitle("删除本地翻译文件")
            .setMessage("删除已下载的界面、剧情、公告和 MasterDB 翻译，并关闭翻译。重启游戏后生效。\n\n需要恢复时，先下载文本，再开启翻译。账号、字体设置和采集记录会保留。")
            .setNegativeButton("取消", null)
            .setPositiveButton("删除", (d, w) -> clean(activity, directory, true)).show();
    }

    static void captures(Activity activity, File directory) {
        new PluginDialog.Builder(activity).setTitle("删除采集文件")
            .setMessage("删除本地采集原始记录，以及“下载 / Idoly-localify”中由游戏导出的采集文件。")
            .setNegativeButton("取消", null)
            .setPositiveButton("删除", (d, w) -> clean(activity, directory, false)).show();
    }

    private static void clean(Activity activity, File directory, boolean translation) {
        if (LocalifyOverlayUi.captureBusy() || !BUSY.compareAndSet(false, true)) {
            Toast.makeText(activity, "请先停止采集并等待导出或清理完成", Toast.LENGTH_LONG).show();
            return;
        }
        if (!TextHotUpdater.beginMaintenance()) {
            BUSY.set(false);
            Toast.makeText(activity, "请等待文本更新完成", Toast.LENGTH_LONG).show();
            return;
        }
        Toast.makeText(activity, "正在清理文件", Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            String message;
            try {
                if (translation) {
                    TextHotUpdater.recover(directory);
                    List<File> files = LocalDataFiles.translations(directory);
                    TranslationPreferences.markDeleted(activity);
                    int count = LocalDataFiles.delete(directory, files);
                    message = "已删除 " + count + " 个本地翻译文件。请重启游戏。";
                } else {
                    HookEntry.pauseCaptureWrites(true);
                    int count = LocalDataFiles.delete(directory, LocalDataFiles.captures(directory));
                    int[] exported = deleteExports(activity);
                    message = "已删除 " + count + " 个原始文件、" + exported[0] + " 个下载目录文件。";
                    if (exported[1] > 0) message += "\n另有 " + exported[1] + " 个文件无删除权限，请在系统文件管理器中删除。";
                }
            } catch (Exception error) {
                message = "清理未完成，已删除的文件不会恢复。请检查存储权限或剩余文件。";
                android.util.Log.w("IdolyLocalify", "Local cleanup failed: " + error.getClass().getSimpleName());
            } finally {
                HookEntry.pauseCaptureWrites(false);
                TextHotUpdater.endMaintenance();
                BUSY.set(false);
            }
            String result = message;
            activity.runOnUiThread(() -> {
                LocalifySettingsUi.dismiss();
                if (!activity.isFinishing() && !activity.isDestroyed())
                    new PluginDialog.Builder(activity).setTitle("文件清理").setMessage(result)
                        .setPositiveButton("确定", null).show();
            });
        }, "IdolyDataCleanup").start();
    }

    private static int[] deleteExports(Activity activity) throws Exception {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return new int[]{0, 0};
        Uri collection = MediaStore.Downloads.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY);
        String folder = Environment.DIRECTORY_DOWNLOADS + "/Idoly-localify/";
        List<Uri> selected = new ArrayList<>();
        try (Cursor cursor = activity.getContentResolver().query(collection,
                new String[]{MediaStore.MediaColumns._ID, MediaStore.MediaColumns.DISPLAY_NAME},
                MediaStore.MediaColumns.RELATIVE_PATH + "=? AND " + MediaStore.MediaColumns.OWNER_PACKAGE_NAME + "=?",
                new String[]{folder, activity.getPackageName()}, null)) {
            if (cursor == null) throw new IllegalStateException("Cannot query exported captures");
            while (cursor.moveToNext()) {
                String name = cursor.getString(1);
                if (name != null && name.matches("capture-[0-9]+(?: \\([0-9]+\\))?(?:\\.(?:layout|status))?(?: \\([0-9]+\\))?\\.(?:jsonl|json)"))
                    selected.add(ContentUris.withAppendedId(collection, cursor.getLong(0)));
            }
        }
        int removed = 0, denied = 0;
        for (Uri item : selected) {
            try { removed += activity.getContentResolver().delete(item, null, null); }
            catch (SecurityException error) { denied++; }
        }
        return new int[]{removed, denied};
    }
}
