package io.github.dreamgallery.idoly.localify;

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
        private String[] itemGroups;
        private DialogInterface.OnClickListener itemAction;
        private final String[] labels = new String[3];
        private final DialogInterface.OnClickListener[] actions = new DialogInterface.OnClickListener[3];

        Builder(Context owner) { this(owner, null); }
        Builder(Context owner, Dialog reuse) {
            dialog = reuse == null ? new SettingsDialog(owner, true) : reuse;
            if (reuse == null) dialog.requestWindowFeature(Window.FEATURE_NO_TITLE);
            else {
                dialog.setOnCancelListener(null);dialog.setCancelable(true);
                dialog.setCanceledOnTouchOutside(true);
            }
            context = dialog.getContext();
        }
        Builder setTitle(String value) { title = value; return this; }
        Builder setMessage(String value) { message = value; return this; }
        Builder setView(View value) { view = value; return this; }
        Builder setItems(String[] values, DialogInterface.OnClickListener action) {
            items = values; itemAction = action; return this;
        }
        Builder setItemGroups(String[] groups) { itemGroups = groups; return this; }
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
            String previousGroup = "";
            if (items != null) for (int i = 0; i < items.length; i++) {
                final int index = i;
                String group = itemGroups != null && i < itemGroups.length ? itemGroups[i] : "";
                if (!group.isEmpty() && !group.equals(previousGroup)) {
                    TextView label = SettingsUiStyle.text(context, group, 14, SettingsUiStyle.MUTED, true);
                    label.setGravity(android.view.Gravity.CENTER_HORIZONTAL);
                    label.setPadding(SettingsUiStyle.dp(context, 16), SettingsUiStyle.dp(context, 12),
                        SettingsUiStyle.dp(context, 16), SettingsUiStyle.dp(context, 4));
                    body.addView(label);
                }
                previousGroup = group;
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
                View button = SettingsUiStyle.button(context, labels[slot], slot == 0, () -> {
                    try {
                        if (actions[slot] != null) actions[slot].onClick(dialog,
                            slot == 0 ? DialogInterface.BUTTON_POSITIVE : slot == 1 ?
                                DialogInterface.BUTTON_NEGATIVE : DialogInterface.BUTTON_NEUTRAL);
                    } finally {
                        dialog.dismiss();
                    }
                });
                button.setId(slot == 0 ? android.R.id.button1 : slot == 1 ? android.R.id.button2 : android.R.id.button3);
                buttons.addView(button, params);
            }
            root.addView(buttons);
            root.setFocusableInTouchMode(true);
            // Measure the whole panel at its final width, including wrapped
            // description text, before showing or replacing the window content.
            // Otherwise a reused loading window first constrains the weighted
            // scroll area to its old height and resizes after the first frame.
            int width = Math.min(SettingsUiStyle.dp(context, 480),
                context.getResources().getDisplayMetrics().widthPixels - SettingsUiStyle.dp(context, 32));
            int maxHeight = Math.max(1, context.getResources().getDisplayMetrics().heightPixels
                - SettingsUiStyle.dp(context, 64));
            root.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
                View.MeasureSpec.makeMeasureSpec(maxHeight, View.MeasureSpec.AT_MOST));
            int height = root.getMeasuredHeight();
            dialog.setContentView(root);
            Window window = dialog.getWindow();
            if (window != null) window.setLayout(width, height);
            if (window != null && !dialog.isShowing()) {
                window.setWindowAnimations(0);
                window.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
                window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE |
                    WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN);
                window.addFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
                window.setDimAmount(0f); // SettingsDialog fades dim together with its content.
            }
            ((SettingsDialog) dialog).animateContent(root);
            dialog.setOnShowListener(ignored -> root.requestFocus());
            if (dialog.isShowing()) root.requestFocus();
            return dialog;
        }
        Dialog show() { Dialog result = create(); result.show(); return result; }
    }
}
