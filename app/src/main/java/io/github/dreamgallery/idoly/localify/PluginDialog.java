package io.github.dreamgallery.idoly.localify;

import android.animation.ValueAnimator;
import android.app.Dialog;
import android.content.Context;
import android.content.DialogInterface;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/** Plugin-owned presentation, independent of the game's theme. */
final class PluginDialog {
    private PluginDialog() {}

    static EditText input(Context context) {
        EditText input = new EditText(new android.view.ContextThemeWrapper(context,
            android.R.style.Theme_Material_Light_NoActionBar));
        input.setTextColor(SettingsUiStyle.INK);
        input.setHintTextColor(SettingsUiStyle.MUTED);
        input.setTextSize(16);
        input.setPadding(SettingsUiStyle.dp(context, 14), SettingsUiStyle.dp(context, 12),
            SettingsUiStyle.dp(context, 14), SettingsUiStyle.dp(context, 12));
        input.setMinHeight(SettingsUiStyle.dp(context, 50));
        input.setBackground(SettingsUiStyle.shape(context, SettingsUiStyle.BACKGROUND, 12));
        input.setBackgroundTintList(null);
        return input;
    }

    static final class Builder {
        private final Dialog dialog;
        private final Context context;
        private String title, message;
        private View view;
        private String[] items;
        private DialogInterface.OnClickListener itemAction;
        private final String[] labels = new String[3];
        private final DialogInterface.OnClickListener[] actions = new DialogInterface.OnClickListener[3];

        Builder(Context owner) {
            dialog = new Dialog(owner, android.R.style.Theme_Material_Light_Dialog_NoActionBar);
            dialog.requestWindowFeature(Window.FEATURE_NO_TITLE);
            context = dialog.getContext();
        }
        Builder setTitle(String value) { title = value; return this; }
        Builder setMessage(String value) { message = value; return this; }
        Builder setView(View value) { view = value; return this; }
        Builder setItems(String[] values, DialogInterface.OnClickListener action) {
            items = values; itemAction = action; return this;
        }
        Builder setPositiveButton(String label, DialogInterface.OnClickListener action) {
            labels[0] = label; actions[0] = action; return this;
        }
        Builder setNegativeButton(String label, DialogInterface.OnClickListener action) {
            labels[1] = label; actions[1] = action; return this;
        }
        Builder setNeutralButton(String label, DialogInterface.OnClickListener action) {
            labels[2] = label; actions[2] = action; return this;
        }
        Builder setOnCancelListener(DialogInterface.OnCancelListener action) {
            dialog.setOnCancelListener(action); return this;
        }
        Dialog create() {
            int pad = SettingsUiStyle.dp(context, 24);
            LinearLayout root = new LinearLayout(context);
            root.setOrientation(LinearLayout.VERTICAL);
            root.setPadding(pad, pad, pad, pad);
            root.setBackground(SettingsUiStyle.shape(context, Color.WHITE, 22));
            TextView heading = SettingsUiStyle.text(context, title, 21, SettingsUiStyle.INK, true);
            heading.setPadding(0, 0, 0, SettingsUiStyle.dp(context, 16));
            root.addView(heading);
            ScrollView scroll = new ScrollView(context);
            scroll.setFillViewport(false);
            LinearLayout body = new LinearLayout(context);
            body.setOrientation(LinearLayout.VERTICAL);
            if (message != null) {
                TextView text = SettingsUiStyle.text(context, message, 15, SettingsUiStyle.MUTED, false);
                text.setLineSpacing(SettingsUiStyle.dp(context, 5), 1);
                text.setPadding(0, 0, 0, SettingsUiStyle.dp(context, 16));
                body.addView(text);
            }
            if (view != null) body.addView(view, new LinearLayout.LayoutParams(-1, -2));
            if (items != null) for (int i = 0; i < items.length; i++) {
                final int index = i;
                SettingsUiStyle.row(body, items[i], null, () -> {
                    try {
                        if (itemAction != null) itemAction.onClick(dialog, index);
                    } finally {
                        dialog.dismiss();
                    }
                });
            }
            scroll.addView(body);
            root.addView(scroll, new LinearLayout.LayoutParams(-1, -2, 1));
            LinearLayout buttons = new LinearLayout(context);
            buttons.setOrientation(LinearLayout.VERTICAL);
            buttons.setPadding(0, SettingsUiStyle.dp(context, 16), 0, 0);
            for (int slot : new int[]{0, 2, 1}) {
                if (labels[slot] == null) continue;
                LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(-1, -2);
                if (buttons.getChildCount() > 0) params.topMargin = SettingsUiStyle.dp(context, 8);
                buttons.addView(SettingsUiStyle.button(context, labels[slot], slot == 0, () -> {
                    try {
                        if (actions[slot] != null) actions[slot].onClick(dialog,
                            slot == 0 ? DialogInterface.BUTTON_POSITIVE : slot == 1 ?
                                DialogInterface.BUTTON_NEGATIVE : DialogInterface.BUTTON_NEUTRAL);
                    } finally {
                        dialog.dismiss();
                    }
                }), params);
            }
            root.addView(buttons);
            root.setFocusableInTouchMode(true);
            dialog.setContentView(root);
            // Apply before show so the first frame already has its final geometry and dim.
            Window window = dialog.getWindow();
            if (window != null) {
                window.setWindowAnimations(0);
                window.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
                int width = Math.min(SettingsUiStyle.dp(context, 480),
                    context.getResources().getDisplayMetrics().widthPixels - SettingsUiStyle.dp(context, 32));
                window.setLayout(width, -2);
                window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE |
                    WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN);
                window.addFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
                WindowManager.LayoutParams attrs = window.getAttributes();
                attrs.dimAmount = .38f;
                window.setAttributes(attrs);
            }
            root.addOnAttachStateChangeListener(new View.OnAttachStateChangeListener() {
                @Override public void onViewAttachedToWindow(View view) {
                    // Animate content only; the window and its modal backdrop stay in place.
                    // ViewPropertyAnimator follows the system duration scale (including zero).
                    if (!ValueAnimator.areAnimatorsEnabled()) return;
                    view.setAlpha(0f);
                    view.setScaleX(.98f);
                    view.setScaleY(.98f);
                    view.animate().alpha(1f).scaleX(1f).scaleY(1f).setDuration(150).start();
                }
                @Override public void onViewDetachedFromWindow(View view) {
                    view.animate().cancel();
                    view.setAlpha(1f);
                    view.setScaleX(1f);
                    view.setScaleY(1f);
                }
            });
            dialog.setOnShowListener(ignored -> root.requestFocus());
            return dialog;
        }
        Dialog show() { Dialog result = create(); result.show(); return result; }
    }
}
