package io.github.dreamgallery.idoly.localify;

import android.app.Activity;
import android.app.Application;
import android.os.Bundle;
import android.graphics.Color;
import android.graphics.Outline;
import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.LayerDrawable;
import android.graphics.drawable.GradientDrawable;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewOutlineProvider;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.Toast;

import java.io.File;
import java.lang.ref.WeakReference;


/** Movable in-game tool, tucked into the screen edge when collapsed. */
final class LocalifyOverlayUi {
    private static final Handler MAIN = new Handler(Looper.getMainLooper());
    private static WeakReference<Activity> current = new WeakReference<>(null);
    private static File dataDir;
    private static LinearLayout overlay;
    private static LinearLayout panel;
    private static Button bubble;
    private static Button captureButton;
    private static boolean expanded;
    private static boolean capturing;
    private static boolean exporting;
    private static boolean lifecycleInstalled;
    private static boolean storyVisible;
    private static boolean manuallyHidden;
    private static boolean dockRight = true;
    private static boolean dragging;
    private static final float EXPOSED_FRACTION = 1f / 3f;
    private static final Runnable FADE = () -> {
        if (!expanded && bubble != null) bubble.setAlpha(0.45f);
    };

    private LocalifyOverlayUi() {}

    static void configure(File directory) {
        dataDir = directory;
        MAIN.post(LocalifyOverlayUi::sync);
    }

    static void onStoryVisibilityChanged(boolean visible) {
        MAIN.post(() -> {
            storyVisible = visible;
            if (visible) expanded = false;
            sync();
        });
    }

    static synchronized void install(Application application) {
        if (lifecycleInstalled) return;
        // The Android lifecycle dispatcher also covers compiled/inlined Activity
        // overrides. Hooking Activity.onResume itself is not reliable on LSPosed.
        application.registerActivityLifecycleCallbacks(new Application.ActivityLifecycleCallbacks() {
            @Override public void onActivityResumed(Activity activity) {
                MAIN.post(() -> {
                    manuallyHidden = false;
                    current = new WeakReference<>(activity);
                    sync();
                });
            }
            @Override public void onActivityPaused(Activity activity) {
                MAIN.post(() -> {
                    FontProfileEditor.close(activity, activity.isFinishing() || activity.isDestroyed());
                    if (current.get() == activity) remove();
                });
            }
            @Override public void onActivityDestroyed(Activity activity) {
                MAIN.post(() -> {
                    FontProfileEditor.close(activity, true);
                    if (current.get() == activity) {
                        remove();
                        current.clear();
                    }
                });
            }
            @Override public void onActivityCreated(Activity activity, Bundle state) {}
            @Override public void onActivityStarted(Activity activity) {}
            @Override public void onActivityStopped(Activity activity) {}
            @Override public void onActivitySaveInstanceState(Activity activity, Bundle state) {}
        });
        lifecycleInstalled = true;
    }

    private static int dp(Activity activity, float value) {
        return (int) (value * activity.getResources().getDisplayMetrics().density + 0.5f);
    }

    private static GradientDrawable bg(Activity activity, int color, int radius) {
        GradientDrawable shape = new GradientDrawable();
        shape.setColor(color);
        shape.setCornerRadius(dp(activity, radius));
        shape.setStroke(dp(activity, 1), 0xffc8c8ff);
        return shape;
    }

    private static Button action(Activity activity, String text, Runnable click) {
        Button item = new Button(activity);
        item.setText(text);
        item.setTextColor(Color.WHITE);
        item.setTextSize(13);
        item.setAllCaps(false);
        item.setMinHeight(0);
        item.setMinimumHeight(0);
        item.setBackground(bg(activity, 0xff484779, 10));
        LinearLayout.LayoutParams row = new LinearLayout.LayoutParams(-1, dp(activity, 39));
        row.topMargin = dp(activity, 5);
        item.setLayoutParams(row);
        item.setOnClickListener(v -> click.run());
        return item;
    }

    private static void sync() {
        Activity activity = current.get();
        if (activity == null || activity.isFinishing() || activity.isDestroyed() || dataDir == null) {
            remove();
            return;
        }
        View decor = activity.getWindow().getDecorView();
        if (!(decor instanceof FrameLayout)) return;
        boolean autoHide = activity.getSharedPreferences("idoly-localify-overlay", 0)
            .getBoolean("hide_during_story", true);
        if (manuallyHidden || (autoHide && storyVisible)) {
            removeOverlay();
            return;
        }
        if (overlay != null && overlay.getParent() == decor) return;
        removeOverlay();
        android.content.SharedPreferences prefs = activity.getSharedPreferences("idoly-localify-overlay", 0);
        dockRight = prefs.getBoolean("dock_right", prefs.getFloat("x", 1f) >= 0.5f);

        LinearLayout container = new LinearLayout(activity);
        container.setOrientation(LinearLayout.VERTICAL);
        container.setElevation(dp(activity, 8));
        Button handle = new Button(activity);
        handle.setText("");
        handle.setTextColor(Color.WHITE);
        handle.setTextSize(17);
        handle.setAllCaps(false);
        handle.setMinHeight(0);
        handle.setMinimumHeight(0);
        handle.setPadding(0, 0, 0, 0);
        handle.setOutlineProvider(new ViewOutlineProvider() {
            @Override public void getOutline(View view, Outline outline) {
                outline.setOval(0, 0, view.getWidth(), view.getHeight());
            }
        });
        handle.setClipToOutline(true);
        styleBubble(activity, handle);
        handle.setContentDescription("本地化工具，点击边缘露出部分展开，拖动可移动");
        container.addView(handle, new LinearLayout.LayoutParams(dp(activity, 48), dp(activity, 48)));

        LinearLayout actions = new LinearLayout(activity);
        actions.setOrientation(LinearLayout.VERTICAL);
        actions.setPadding(dp(activity, 8), dp(activity, 4), dp(activity, 8), dp(activity, 9));
        actions.setBackground(bg(activity, 0xee25253d, 12));
        LinearLayout.LayoutParams panelLayout = new LinearLayout.LayoutParams(dp(activity, 210), -2);
        panelLayout.topMargin = dp(activity, 4);
        container.addView(actions, panelLayout);
        actions.addView(action(activity, "重新载入本地文本", () -> runUpdate(activity, false)));
        Button capture = action(activity, "开始采集未翻译文本", () -> toggleCapture(activity));
        actions.addView(capture);
        actions.addView(action(activity, "详细设置", () -> {
            expanded = false;
            actions.setVisibility(View.GONE);
            container.post(() -> {
                placeAtEdge(activity, container);
                scheduleFade();
            });
            LocalifySettingsUi.show(activity, () -> runUpdate(activity, true),
                () -> editManifestUrl(activity), () -> FontProfileEditor.show(activity, dataDir),
                () -> hide(activity), () -> StorySwap.show(activity, dataDir),
                () -> StorySwap.showEvent(activity, dataDir),
                () -> StorySwap.showSpecial(activity, dataDir), capturing, dataDir);
        }));
        actions.setVisibility(expanded ? View.VISIBLE : View.GONE);
        ((FrameLayout) decor).addView(container, new FrameLayout.LayoutParams(-2, -2,
            Gravity.TOP | Gravity.LEFT));
        overlay = container;
        panel = actions;
        bubble = handle;
        captureButton = capture;
        updateCaptureLabel();
        handle.setOnClickListener(v -> {
            expanded = !expanded;
            actions.setVisibility(expanded ? View.VISIBLE : View.GONE);
            handle.setAlpha(1f);
            MAIN.removeCallbacks(FADE);
            container.post(() -> {
                placeAtEdge(activity, container);
                if (!expanded) scheduleFade();
            });
        });
        handle.setOnTouchListener(new Drag(activity, container));
        container.post(() -> {
            float y = activity.getSharedPreferences("idoly-localify-overlay", 0).getFloat("y", -1);
            container.setY(y < 0 ? decor.getHeight() / 2f : y * decor.getHeight());
            placeAtEdge(activity, container);
            scheduleFade();
        });
        container.addOnLayoutChangeListener((v, l, t, r, b, ol, ot, or, ob) -> {
            if (!dragging && (r - l != or - ol || b - t != ob - ot))
                placeAtEdge(activity, container);
        });
    }

    private static void clamp(Activity activity, View view) {
        View parent = (View) view.getParent();
        if (parent == null) return;
        view.setX(Math.max(0, Math.min(view.getX(), parent.getWidth() - view.getWidth())));
        view.setY(Math.max(0, Math.min(view.getY(), parent.getHeight() - view.getHeight())));
    }

    private static void snap(Activity activity, View view) {
        View parent = (View) view.getParent();
        if (parent == null) return;
        dockRight = view.getX() + view.getWidth() / 2f >= parent.getWidth() / 2f;
        placeAtEdge(activity, view);
    }

    private static void placeAtEdge(Activity activity, View view) {
        View parent = (View) view.getParent();
        if (parent == null) return;
        float tucked = expanded ? 0 : dp(activity, 48) * (1f - EXPOSED_FRACTION);
        view.setX(dockRight ? parent.getWidth() - view.getWidth() + tucked : -tucked);
        view.setY(Math.max(0, Math.min(view.getY(), parent.getHeight() - view.getHeight())));
        if (bubble != null) {
            LinearLayout.LayoutParams params = (LinearLayout.LayoutParams) bubble.getLayoutParams();
            int gravity = dockRight ? Gravity.RIGHT : Gravity.LEFT;
            if (params.gravity != gravity) {
                params.gravity = gravity;
                bubble.setLayoutParams(params);
            }
        }
    }

    private static void scheduleFade() {
        MAIN.removeCallbacks(FADE);
        if (bubble != null && !expanded) MAIN.postDelayed(FADE, 3000);
    }

    private static final class Drag implements View.OnTouchListener {
        final Activity activity;
        final View view;
        float downX, downY, startX, startY;
        boolean moved;
        Drag(Activity activity, View view) { this.activity = activity; this.view = view; }
        @Override public boolean onTouch(View handle, MotionEvent event) {
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    dragging = true;
                    downX = event.getRawX(); downY = event.getRawY();
                    startX = view.getX(); startY = view.getY(); moved = false;
                    handle.setAlpha(1f);
                    MAIN.removeCallbacks(FADE);
                    return true;
                case MotionEvent.ACTION_MOVE:
                    float dx = event.getRawX() - downX, dy = event.getRawY() - downY;
                    if (Math.abs(dx) > dp(activity, 7) || Math.abs(dy) > dp(activity, 7)) moved = true;
                    if (moved) {
                        view.setX(startX + dx); view.setY(startY + dy); clamp(activity, view);
                    }
                    return true;
                case MotionEvent.ACTION_UP:
                    dragging = false;
                    if (!moved) handle.performClick();
                    else {
                        snap(activity, view);
                        View parent = (View) view.getParent();
                        if (parent != null && parent.getWidth() > 0 && parent.getHeight() > 0)
                            activity.getSharedPreferences("idoly-localify-overlay", 0).edit()
                                .putBoolean("dock_right", dockRight)
                                .putFloat("y", view.getY() / parent.getHeight()).apply();
                    }
                    scheduleFade();
                    return true;
                case MotionEvent.ACTION_CANCEL:
                    dragging = false;
                    snap(activity, view);
                    scheduleFade();
                    return true;
                default: return false;
            }
        }
    }

    private static void remove() {
        LocalifySettingsUi.dismiss();
        removeOverlay();
    }

    private static void removeOverlay() {
        MAIN.removeCallbacks(FADE);
        dragging = false;
        if (overlay != null && overlay.getParent() instanceof ViewGroup)
            ((ViewGroup) overlay.getParent()).removeView(overlay);
        overlay = null; panel = null; bubble = null; captureButton = null;
    }

    static void refreshVisibility() {
        MAIN.post(LocalifyOverlayUi::sync);
    }

    private static void hide(Activity activity) {
        if (exporting) return;
        if (capturing) {
            stopCapture(activity, true);
            return;
        }
        manuallyHidden = true;
        expanded = false;
        sync();
    }

    private static void styleBubble(Activity activity, Button handle) {
        Drawable background = new IpIconDrawable();
        if (capturing) {
            GradientDrawable border = new GradientDrawable();
            border.setShape(GradientDrawable.OVAL);
            border.setColor(Color.TRANSPARENT);
            border.setStroke(dp(activity, 3), 0xffff527a);
            background = new LayerDrawable(new Drawable[]{background, border});
        }
        handle.setText("");
        handle.setBackground(background);
        handle.setBackgroundTintList(null);
    }

    /** Hand-traced IP artwork; coordinates match config/overlay-icon.svg. */
    private static final class IpIconDrawable extends Drawable {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Path letters = new Path();
        private int alpha = 255;

        IpIconDrawable() {
            letters.addRect(10, 9, 15, 39, Path.Direction.CW);
            // The P has an open upper-left corner, as in the official favicon.
            letters.moveTo(20, 9);
            letters.lineTo(31.5f, 9);
            letters.cubicTo(36.5f, 9, 40, 12.5f, 40, 17.5f);
            letters.cubicTo(40, 22.5f, 36.5f, 26, 31.5f, 26);
            letters.lineTo(25, 26);
            letters.lineTo(25, 39);
            letters.lineTo(20, 39);
            letters.lineTo(20, 21);
            letters.lineTo(31.5f, 21);
            letters.cubicTo(33.6f, 21, 35, 19.6f, 35, 17.5f);
            letters.cubicTo(35, 15.4f, 33.6f, 14, 31.5f, 14);
            letters.lineTo(20, 14);
            letters.close();
            // Bounds were x=10..40 (center 25); center the lettering on x=24.
            letters.offset(-1f, 0f);
        }

        @Override public void draw(Canvas canvas) {
            Rect bounds = getBounds();
            float size = Math.min(bounds.width(), bounds.height());
            if (size <= 0) return;
            int saved = canvas.save();
            canvas.translate(bounds.exactCenterX() - size / 2,
                bounds.exactCenterY() - size / 2);
            canvas.scale(size / 48f, size / 48f);
            // Translucent outer ring stays inside the existing circular hit area.
            paint.setColor(Color.WHITE);
            paint.setAlpha(Math.round(alpha * 0.4f));
            canvas.drawCircle(24, 24, 24, paint);
            paint.setColor(0xff3034ff);
            paint.setAlpha(alpha);
            canvas.drawCircle(24, 24, 21, paint);
            paint.setColor(Color.WHITE);
            paint.setAlpha(alpha);
            canvas.drawPath(letters, paint);
            canvas.restoreToCount(saved);
        }

        @Override public void setAlpha(int value) {
            alpha = value;
            invalidateSelf();
        }

        @Override public int getAlpha() { return alpha; }

        @Override public void setColorFilter(ColorFilter filter) {
            paint.setColorFilter(filter);
            invalidateSelf();
        }

        @Override public int getOpacity() { return PixelFormat.TRANSLUCENT; }
    }

    private static void updateCaptureLabel() {
        if (captureButton != null) captureButton.setText(capturing ?
            "停止采集未翻译文本" : "开始采集未翻译文本");
        Activity activity = current.get();
        if (bubble != null && activity != null) {
            styleBubble(activity, bubble);
            bubble.setContentDescription(capturing ?
                "正在本机采集未翻译文本，点击展开或拖动" :
                "本地化工具，点击展开或折叠，拖动可移动");
        }
    }

    private static void toggleCapture(Activity activity) {
        if (exporting) return;
        if (capturing) {
            stopCapture(activity, false);
            return;
        }
        if (!HookEntry.setTextCaptureEnabled(true)) {
            Toast.makeText(activity, "采集开关操作失败", Toast.LENGTH_LONG).show();
            return;
        }
        capturing = true;
        updateCaptureLabel();
        new PluginDialog.Builder(activity).setTitle("文本采集")
            .setMessage("只在本机记录未翻译文本；再次点击可停止并导出本次记录。")
            .setPositiveButton("确定", null).show();
    }

    private static void stopCapture(Activity activity, boolean hideAfter) {
        if (!HookEntry.setTextCaptureEnabled(false)) {
            Toast.makeText(activity, "无法停止采集", Toast.LENGTH_LONG).show();
            return;
        }
        capturing = false;
        exporting = true;
        updateCaptureLabel();
        if (captureButton != null) captureButton.setEnabled(false);
        String path = HookEntry.getTextCapturePath();
        String status = HookEntry.getTextCaptureStatus();
        File directory = dataDir;
        android.content.Context appContext = activity.getApplicationContext();
        Toast.makeText(activity, "正在导出本次采集", Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            CaptureExporter.Result result = null;
            String error = null;
            try { result = CaptureExporter.exportCurrent(appContext, directory, path, status); }
            catch (Exception failure) { error = failure.getMessage(); }
            CaptureExporter.Result exported = result;
            String failureMessage = error;
            MAIN.post(() -> {
                exporting = false;
                if (captureButton != null) captureButton.setEnabled(true);
                if (activity.isFinishing() || activity.isDestroyed()) return;
                if (exported == null) {
                    new PluginDialog.Builder(activity).setTitle("采集导出失败")
                        .setMessage(failureMessage == null ? "文件操作失败" : failureMessage)
                        .setPositiveButton("确定", null).show();
                    return;
                }
                if (hideAfter) {
                    manuallyHidden = true;
                    expanded = false;
                    sync();
                }
                new PluginDialog.Builder(activity).setTitle(exported.complete ?
                        "本次采集已完整导出" : "已导出部分采集")
                    .setMessage((exported.complete ? "" : exported.warning +
                        "。后续记录可能缺失，请开始新会话继续采集。\n\n") +
                        "保存在系统下载目录 Idoly-localify 文件夹：\n" +
                        "文本：" + exported.textName + "\n" +
                        "布局：" + exported.layoutName + "\n" +
                        "状态：" + exported.statusName)
                    .setNeutralButton("反馈问题", (dialog, which) -> FeedbackUi.show(activity))
                    .setPositiveButton("确定", null).show();
            });
        }, "IdolyCaptureExport").start();
    }

    private static void editManifestUrl(Activity activity) {
        EditText input = PluginDialog.input(activity);
        input.setSingleLine(true);
        input.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        input.setHint("https://example.org/idoly/manifest.json");
        input.setText(TextHotUpdater.readManifestUrl(dataDir));
        new PluginDialog.Builder(activity).setTitle("文本更新地址")
            .setMessage("填写可直接访问的 HTTPS manifest.json 地址。留空可停用在线更新。")
            .setView(input)
            .setPositiveButton("保存", (dialog, which) -> {
                try {
                    TextHotUpdater.writeManifestUrl(dataDir, input.getText().toString().trim());
                    LocalifySettingsUi.refreshUpdateStatus();
                    Toast.makeText(activity, "更新地址已保存", Toast.LENGTH_SHORT).show();
                } catch (Exception error) {
                    Toast.makeText(activity, error.getMessage(), Toast.LENGTH_LONG).show();
                }
            }).setNegativeButton("取消", null).show();
    }

    private static void runUpdate(Activity activity, boolean remote) {
        Toast.makeText(activity, remote ? "正在检查文本更新" : "正在载入本地文本",
            Toast.LENGTH_SHORT).show();
        TextHotUpdater.run(dataDir, remote, result -> MAIN.post(() -> {
            UpdateChecker.textInstalled(activity.getApplicationContext(), result);
            LocalifySettingsUi.refreshUpdateStatus();
            if (!activity.isFinishing() && !activity.isDestroyed() && current.get() == activity)
                new PluginDialog.Builder(activity)
                    .setTitle(result.success ? "文本更新完成" : "文本更新失败")
                    .setMessage(result.message).setPositiveButton("确定", null).show();
        }));
    }
}
