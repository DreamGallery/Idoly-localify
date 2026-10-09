package io.github.dreamgallery.idoly.localify;

import android.app.Dialog;
import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;

/** Shared presentation for full-window plugin settings. */
final class SettingsUiStyle {
    static final int BACKGROUND = 0xfff4f5fb;
    static final int INK = 0xff24243b;
    static final int MUTED = 0xff696b80;
    static final int ACCENT = 0xff4169df;
    private static final int TINT = 0xffedf1ff;

    private SettingsUiStyle() {}

    static int dp(Context context, int value) {
        return Math.round(value * context.getResources().getDisplayMetrics().density);
    }

    static TextView text(Context context, String label, int size, int color, boolean bold) {
        TextView view = new TextView(context);
        view.setText(label);
        view.setTextSize(size);
        view.setTextColor(color);
        if (bold) view.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        return view;
    }

    static GradientDrawable shape(Context context, int color, int radius) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(color);
        drawable.setCornerRadius(dp(context, radius));
        return drawable;
    }

    static final class Page {
        final FrameLayout root;
        final LinearLayout body;

        Page(Dialog dialog, String title, String description, String backLabel, Runnable back) {
            this(dialog, title, description, backLabel, back, true);
        }

        Page(Dialog dialog, String title, String description, String backLabel, Runnable back, boolean scrolling) {
            Context context = dialog.getContext();
            // Keep system insets separate from design padding.
            root = new FrameLayout(context);
            root.setFitsSystemWindows(true);
            root.setBackgroundColor(BACKGROUND);
            LinearLayout page = new LinearLayout(context);
            page.setOrientation(LinearLayout.VERTICAL);
            root.addView(page, new FrameLayout.LayoutParams(-1, -1));
            int pad = dp(context, 20);
            LinearLayout header = new LinearLayout(context);
            header.setOrientation(LinearLayout.VERTICAL);
            header.setPadding(pad, dp(context, 12), pad, dp(context, 18));
            LinearLayout bar = new LinearLayout(context);
            bar.setGravity(Gravity.CENTER_VERTICAL);
            TextView brand = text(context, "IDOLY · LOCALIFY", 12, ACCENT, true);
            brand.setLetterSpacing(.08f);
            bar.addView(brand, new LinearLayout.LayoutParams(0, -2, 1));
            bar.addView(button(context, backLabel, false, back), new LinearLayout.LayoutParams(-2, -2));
            header.addView(bar);
            TextView heading = text(context, title, 27, INK, true);
            heading.setPadding(0, dp(context, 14), 0, dp(context, 6));
            header.addView(heading);
            TextView subtitle = text(context, description, 14, MUTED, false);
            subtitle.setLineSpacing(dp(context, 3), 1);
            header.addView(subtitle);
            page.addView(header);
            ScrollView scroll = new ScrollView(context);
            scroll.setClipToPadding(false);
            scroll.setPadding(pad, 0, pad, dp(context, 24));
            scroll.setVerticalScrollBarEnabled(false);
            body = new LinearLayout(context);
            body.setOrientation(LinearLayout.VERTICAL);
            if (scrolling) {
                scroll.addView(body, new ScrollView.LayoutParams(-1, -2));
                page.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
            } else {
                body.setPadding(pad, 0, pad, dp(context, 16));
                page.addView(body, new LinearLayout.LayoutParams(-1, 0, 1));
            }
        }
    }

    static LinearLayout card(LinearLayout parent, String title, String description) {
        Context context = parent.getContext();
        LinearLayout card = new LinearLayout(context);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(context, 16), dp(context, 16), dp(context, 16), dp(context, 12));
        card.setBackground(shape(context, 0xffffffff, 18));
        LinearLayout.LayoutParams layout = new LinearLayout.LayoutParams(-1, -2);
        layout.bottomMargin = dp(context, 14);
        parent.addView(card, layout);
        card.addView(text(context, title, 18, INK, true));
        if (description != null && !description.isEmpty()) {
            TextView detail = text(context, description, 13, MUTED, false);
            detail.setPadding(0, dp(context, 6), 0, dp(context, 10));
            detail.setLineSpacing(dp(context, 3), 1);
            card.addView(detail);
        }
        return card;
    }

    static void row(LinearLayout parent, String label, String value, Runnable action) {
        row(parent, label, value, action, 10);
    }

    static void cardRow(LinearLayout parent, String label, String value, Runnable action) {
        row(parent, label, value, action, 0);
    }

    private static void row(LinearLayout parent, String label, String value, Runnable action, int inset) {
        Context context = parent.getContext();
        LinearLayout row = new LinearLayout(context);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setMinimumHeight(dp(context, 56));
        row.setPadding(dp(context, inset), dp(context, 12), dp(context, inset), dp(context, 12));
        row.setBackground(new RippleDrawable(ColorStateList.valueOf(TINT), shape(context, 0xffffffff, 10), null));
        row.setFocusable(true);
        row.setOnClickListener(view -> action.run());
        row.addView(text(context, label, 16, INK, false), new LinearLayout.LayoutParams(0, -2, 1));
        if (value != null) {
            TextView current = text(context, value, 15, ACCENT, true);
            current.setMaxWidth(dp(context, 150));
            current.setGravity(Gravity.END);
            current.setPadding(dp(context, 10), 0, dp(context, 10), 0);
            row.addView(current, new LinearLayout.LayoutParams(-2, -2));
        }
        TextView arrow = text(context, "›", 24, MUTED, false);
        arrow.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        row.addView(arrow);
        row.setContentDescription(label + (value == null ? "" : "，当前值 " + value));
        parent.addView(row, new LinearLayout.LayoutParams(-1, -2));
    }

    static void styleSwitch(Switch view) {
        Context context = view.getContext();
        view.setTextSize(16);
        view.setTextColor(INK);
        view.setMinHeight(dp(context, 56));
        view.setPadding(0, dp(context, 10), 0, dp(context, 10));
        view.setThumbTintList(new ColorStateList(
            new int[][]{new int[]{android.R.attr.state_checked}, new int[]{}},
            new int[]{ACCENT, 0xff9699aa}));
        view.setTrackTintList(new ColorStateList(
            new int[][]{new int[]{android.R.attr.state_checked}, new int[]{}},
            new int[]{0x664169df, 0x337a7d90}));
    }

    static void divider(LinearLayout parent) {
        Context context = parent.getContext();
        View line = new View(context);
        line.setBackgroundColor(0xffeef0f6);
        LinearLayout.LayoutParams layout = new LinearLayout.LayoutParams(-1, dp(context, 1));
        layout.topMargin = dp(context, 8);
        layout.bottomMargin = dp(context, 8);
        parent.addView(line, layout);
    }

    static Button button(Context context, String label, boolean primary, Runnable action) {
        Button button = new Button(context);
        button.setText(label);
        button.setAllCaps(false);
        button.setTextSize(14);
        button.setTextColor(primary ? 0xffffffff : ACCENT);
        button.setMinHeight(dp(context, 44));
        button.setMinimumHeight(dp(context, 44));
        button.setPadding(dp(context, 16), dp(context, 8), dp(context, 16), dp(context, 8));
        button.setStateListAnimator(null);
        button.setBackground(new RippleDrawable(ColorStateList.valueOf(0x224169df),
            shape(context, primary ? ACCENT : TINT, 12), null));
        button.setOnClickListener(view -> action.run());
        return button;
    }

    static void configureWindow(Dialog dialog) {
        if (dialog.getWindow() == null) return;
        // Configure before show: avoid the theme's translucent enter/exit frames.
        dialog.getWindow().setWindowAnimations(0);
        dialog.getWindow().setLayout(-1, -1);
        dialog.getWindow().setBackgroundDrawable(shape(dialog.getContext(), BACKGROUND, 0));
        dialog.getWindow().setStatusBarColor(BACKGROUND);
        dialog.getWindow().setNavigationBarColor(BACKGROUND);
        dialog.getWindow().getDecorView().setSystemUiVisibility(
            View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR | View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR);
    }
}
