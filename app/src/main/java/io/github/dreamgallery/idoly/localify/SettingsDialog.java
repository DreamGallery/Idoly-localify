package io.github.dreamgallery.idoly.localify;

import android.animation.Animator;
import android.animation.AnimatorListenerAdapter;
import android.animation.ValueAnimator;
import android.app.Activity;
import android.app.Dialog;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewTreeObserver;
import android.view.animation.PathInterpolator;
import android.view.animation.DecelerateInterpolator;
import android.widget.FrameLayout;
import android.widget.ImageView;
import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.List;

/** Page navigation stays inside opaque plugin windows, never exposing Unity. */
final class SettingsDialog extends Dialog {
    private static final List<WeakReference<SettingsDialog>> opened = new ArrayList<>();
    private static final PathInterpolator EASING = new PathInterpolator(.28f, 0f, .16f, 1f);
    private static final DecelerateInterpolator MODAL_EASING = new DecelerateInterpolator(1.5f);
    private static final float PARALLAX = .22f;
    private static final float MODAL_DIM = .38f;
    private final boolean modal;
    private final Context owner;
    private View content;
    private FrameLayout stage;
    private ImageView backdrop;
    private ValueAnimator motion;
    private ViewTreeObserver.OnPreDrawListener pendingEnter;
    private float currentDim;
    private boolean closing;

    SettingsDialog(Context owner) { this(owner, false); }

    SettingsDialog(Context owner, boolean modal) {
        super(owner, modal ? android.R.style.Theme_Material_Light_Dialog_NoActionBar
            : android.R.style.Theme_Material_Light_NoActionBar);
        this.owner = owner;this.modal = modal;
        if (getWindow() != null) getWindow().setWindowAnimations(0);
    }

    void animateContent(View view) {
        if (!isShowing()) { content = view;return; }
        // Loading and result panels can share one modal window. Preserve its
        // current opacity instead of dismissing and dimming the same page twice.
        float alpha = content == null ? 1f : content.getAlpha();
        stopMotion();content = view;
        content.setAlpha(alpha);
        if (alpha < 1f || currentDim < MODAL_DIM) scheduleEntrance();
    }

    boolean isClosing() { return closing; }

    void animatePage(FrameLayout container, View view) { stage = container;content = view; }

    private boolean animationsEnabled() {
        return ValueAnimator.areAnimatorsEnabled() && !(owner instanceof Activity
            && (((Activity) owner).isFinishing() || ((Activity) owner).isDestroyed()));
    }

    private SettingsDialog underneath(boolean pageOnly) {
        for (int i = opened.size() - 1; i >= 0; i--) {
            SettingsDialog dialog = opened.get(i).get();
            if (dialog == null) { opened.remove(i);continue; }
            if (dialog != this && dialog.owner == owner && dialog.isShowing() && !dialog.closing
                    && (!pageOnly || (!dialog.modal && dialog.stage != null))) return dialog;
        }
        return null;
    }

    private SettingsDialog coveringDialog() {
        for (int i = opened.size() - 1; i >= 0; i--) {
            SettingsDialog dialog = opened.get(i).get();
            if (dialog == this) return null;
            if (dialog != null && dialog.owner == owner && dialog.isShowing()) return dialog;
        }
        return null;
    }

    private void captureBackdrop(SettingsDialog previous) {
        clearBackdrop();
        if (stage == null || previous == null || previous.content == null) return;
        View view = previous.content;
        int width = view.getWidth(), height = view.getHeight();
        if (!view.isAttachedToWindow() || width < 1 || height < 1) return;
        try {
            // One bounded, transient snapshot of plugin views only. This also
            // keeps navigation stable when the caller closes the previous dialog.
            float scale = Math.min(1f, 1600f / Math.max(width, height));
            Bitmap image = Bitmap.createBitmap(Math.max(1, Math.round(width * scale)),
                Math.max(1, Math.round(height * scale)), Bitmap.Config.ARGB_8888);
            Canvas canvas = new Canvas(image);canvas.scale(scale, scale);view.draw(canvas);
            backdrop = new ImageView(getContext());backdrop.setScaleType(ImageView.ScaleType.FIT_XY);
            backdrop.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
            backdrop.setImageBitmap(image);
            stage.addView(backdrop, 0, new FrameLayout.LayoutParams(-1, -1));
        } catch (RuntimeException | OutOfMemoryError unavailable) {
            clearBackdrop(); // A cosmetic transition must not prevent settings from opening.
        }
    }

    private void clearBackdrop() {
        if (backdrop != null) {
            backdrop.setImageDrawable(null);stage.removeView(backdrop);backdrop = null;
        }
    }

    @Override protected void onStart() {
        super.onStart();closing = false;resetContent();
        if (!modal && content != null && animationsEnabled()) captureBackdrop(underneath(true));
        opened.add(new WeakReference<>(this));
        if (content == null) return;
        if (modal) {
            boolean entering = animationsEnabled();
            content.setAlpha(entering ? 0f : 1f);
            setModalDim(entering ? 0f : MODAL_DIM);
        }
        if (!animationsEnabled()) return;
        scheduleEntrance();
    }

    private void setModalDim(float value) {
        currentDim = value;
        if (modal && getWindow() != null) getWindow().setDimAmount(value);
    }

    private void scheduleEntrance() {
        // Width is reliable only after layout. Set the first animation frame
        // before drawing, rather than briefly showing the finished page first.
        pendingEnter = () -> {
            removePendingEnter();
            if (!closing) enter();
            return true;
        };
        content.getViewTreeObserver().addOnPreDrawListener(pendingEnter);
    }

    private void enter() {
        if (!animationsEnabled()) { resetContent();clearBackdrop();if (modal) setModalDim(MODAL_DIM);return; }
        final boolean paired = backdrop != null;
        final float width = content.getWidth();
        final float distance = modal ? 0f : paired ? width : width * .12f;
        final float alpha = modal ? content.getAlpha() : paired ? 1f : 0f;
        final float dim = currentDim;
        content.setTranslationX(distance);content.setAlpha(alpha);
        runMotion(modal ? Math.max(80, Math.round(320 * (1f - alpha))) : paired ? 400 : 300, animation -> {
            float progress = (float) animation.getAnimatedValue();
            content.setTranslationX(distance * (1f - progress));
            content.setAlpha(alpha + (1f - alpha) * progress);
            if (modal) setModalDim(dim + (MODAL_DIM - dim) * progress);
            if (backdrop != null) backdrop.setTranslationX(-width * PARALLAX * progress);
        }, () -> { resetContent();clearBackdrop(); });
    }

    private void runMotion(long duration, ValueAnimator.AnimatorUpdateListener update, Runnable finished) {
        motion = ValueAnimator.ofFloat(0f, 1f);
        motion.setDuration(duration);motion.setInterpolator(modal ? MODAL_EASING : EASING);motion.addUpdateListener(update);
        motion.addListener(new AnimatorListenerAdapter() {
            @Override public void onAnimationEnd(Animator animation) {
                if (motion != animation) return;
                motion = null;finished.run();
            }
        });
        motion.start();
    }

    @Override public void dismiss() {
        if (closing) return;
        if (content == null || !isShowing() || !content.isAttachedToWindow()
                || !animationsEnabled() || coveringDialog() != null || (modal && content.getAlpha() == 0f)) {
            dismissImmediately();return;
        }
        closing = true;stopMotion();
        if (!modal) captureBackdrop(underneath(true));
        final boolean paired = backdrop != null;
        final float width = content.getWidth(), start = content.getTranslationX();
        final float end = modal ? 0f : paired ? width : width * .12f;
        final float alpha = content.getAlpha(), dim = currentDim;
        final float behind = -PARALLAX * Math.max(0f, width - start);
        if (backdrop != null) backdrop.setTranslationX(behind);
        runMotion(modal ? 260 : paired ? 360 : 240, animation -> {
            float progress = (float) animation.getAnimatedValue();
            content.setTranslationX(start + (end - start) * progress);
            content.setAlpha(paired ? 1f : alpha * (1f - progress));
            if (modal) setModalDim(dim * (1f - progress));
            if (backdrop != null) backdrop.setTranslationX(behind * (1f - progress));
        }, this::dismissImmediately);
    }

    @Override public boolean dispatchTouchEvent(MotionEvent event) {
        // Avoid opening another page from controls still moving into position.
        return closing || motion != null || pendingEnter != null
            || coveringDialog() != null || super.dispatchTouchEvent(event);
    }

    @Override public void onBackPressed() {
        // Window focus can lag behind show(). Do not let a fast Back dismiss
        // the settings page while its newly opened loading dialog is on top.
        SettingsDialog foreground = coveringDialog();
        if (foreground != null) {
            if (!foreground.closing) foreground.onBackPressed();
            return;
        }
        super.onBackPressed();
    }

    private void removePendingEnter() {
        if (pendingEnter != null && content != null) {
            if (content.getViewTreeObserver().isAlive())
                content.getViewTreeObserver().removeOnPreDrawListener(pendingEnter);
            pendingEnter = null;
        }
    }

    private void stopMotion() {
        removePendingEnter();
        if (motion != null) {
            ValueAnimator old = motion;motion = null;
            old.removeAllListeners();old.removeAllUpdateListeners();old.cancel();
        }
    }

    private void resetContent() {
        if (content != null) {
            content.setAlpha(1f);content.setTranslationX(0);content.setScaleX(1f);content.setScaleY(1f);
        }
    }

    void dismissImmediately() {
        stopMotion();clearBackdrop();
        super.dismiss();closing = false;
    }

    @Override protected void onStop() {
        stopMotion();clearBackdrop();resetContent();
        opened.removeIf(reference -> reference.get() == null || reference.get() == this);
        super.onStop();
    }
}
