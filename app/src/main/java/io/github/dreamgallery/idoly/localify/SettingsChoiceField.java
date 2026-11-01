package io.github.dreamgallery.idoly.localify;

import android.content.Context;
import android.content.res.ColorStateList;
import android.animation.ValueAnimator;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.graphics.drawable.ColorDrawable;
import android.text.TextUtils;
import android.transition.Fade;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.BaseAdapter;
import android.widget.ImageView;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.PopupWindow;
import android.widget.TextView;
import java.util.ArrayList;
import java.util.List;

/** Settings-styled selection field, with optional grouped rows and thumbnails. */
final class SettingsChoiceField extends LinearLayout {
    static final class Choice {
        final String id, label, group, thumbnail;
        Choice(String id, String label) { this(id, label, "", ""); }
        Choice(String id, String label, String group, String thumbnail) {
            this.id = id;this.label = label;this.group = group;this.thumbnail = thumbnail;
        }
    }
    private final ArrayList<Choice> choices = new ArrayList<>();
    private final PhotoThumbnailLoader thumbnails;
    private final LinearLayout field;
    private final TextView value;
    private final ImageView icon;
    private Runnable changed = () -> {};
    private PopupWindow popup;
    private int selected = -1;
    private final String title;

    SettingsChoiceField(LinearLayout parent, String title, PhotoThumbnailLoader thumbnails) {
        super(parent.getContext());this.title = title;this.thumbnails = thumbnails;
        setOrientation(VERTICAL);
        Context context = getContext();
        TextView caption = SettingsUiStyle.text(context, title, 13, SettingsUiStyle.MUTED, false);
        caption.setPadding(0, dp(12), 0, dp(7));addView(caption);
        field = new LinearLayout(context);field.setGravity(Gravity.CENTER_VERTICAL);
        field.setPadding(dp(12), dp(7), dp(12), dp(7));field.setMinimumHeight(dp(54));
        GradientDrawable surface = SettingsUiStyle.shape(context, 0xfff6f8ff, 12);
        surface.setStroke(dp(1), 0xffe1e6f4);
        field.setBackground(new RippleDrawable(ColorStateList.valueOf(0x224169df), surface, null));
        field.setFocusable(true);field.setOnClickListener(v -> open());
        icon = new ImageView(context);icon.setScaleType(ImageView.ScaleType.FIT_CENTER);
        LayoutParams imageLayout = new LayoutParams(dp(44), dp(44));imageLayout.rightMargin = dp(10);
        field.addView(icon, imageLayout);
        value = SettingsUiStyle.text(context, "", 15, SettingsUiStyle.INK, false);
        value.setMaxLines(2);value.setEllipsize(TextUtils.TruncateAt.END);
        field.addView(value, new LayoutParams(0, -2, 1));
        LayoutParams arrowLayout = new LayoutParams(dp(24), dp(24));arrowLayout.leftMargin = dp(8);
        field.addView(SettingsUiStyle.arrow(context, true), arrowLayout);
        addView(field, new LayoutParams(-1, -2));
        LayoutParams layout = new LayoutParams(-1, -2);layout.bottomMargin = dp(4);parent.addView(this, layout);
    }
    private int dp(int value) { return SettingsUiStyle.dp(getContext(), value); }
    void onChanged(Runnable action) { changed = action; }
    String selectedId() { return selected < 0 ? "" : choices.get(selected).id; }
    boolean hasSelection() { return selected >= 0; }
    void setChoices(List<Choice> options, String preferred, String emptyLabel) {
        if (popup != null) popup.dismiss();
        choices.clear();choices.addAll(options);selected = choices.isEmpty() ? -1 : 0;
        for (int i = 0; i < choices.size(); i++) if (choices.get(i).id.equals(preferred)) { selected = i; break; }
        update(emptyLabel);
    }
    private void update(String emptyLabel) {
        Choice choice = selected < 0 ? null : choices.get(selected);
        value.setText(choice == null ? emptyLabel : choice.label);
        field.setEnabled(choices.size() > 1);field.setAlpha(choice == null ? .65f : 1f);
        field.setContentDescription(title + "，" + value.getText());
        boolean hasImage = choice != null && !choice.thumbnail.isEmpty() && thumbnails != null;
        icon.setVisibility(hasImage ? VISIBLE : GONE);
        if (hasImage) thumbnails.bind(icon, choice.thumbnail);
    }
    private void open() {
        if (choices.size() < 2 || (popup != null && popup.isShowing())) return;
        ArrayList<Choice> rows = new ArrayList<>();ArrayList<Integer> indexes = new ArrayList<>();
        String group = "";
        for (int i = 0; i < choices.size(); i++) {
            Choice choice = choices.get(i);
            if (!choice.group.isEmpty() && !choice.group.equals(group)) {
                rows.add(new Choice("", choice.group));indexes.add(-1);group = choice.group;
            }
            rows.add(choice);indexes.add(i);
        }
        FrameLayout surface = new FrameLayout(getContext());
        surface.setBackground(SettingsUiStyle.shape(getContext(), 0xffffffff, 14));
        surface.setClipToOutline(true);
        ListView list = new ListView(getContext());
        list.setDivider(null);list.setSelector(new ColorDrawable(0x184169df));
        list.setVerticalScrollBarEnabled(false);list.setOverScrollMode(OVER_SCROLL_NEVER);
        surface.addView(list, new FrameLayout.LayoutParams(-1, -1));
        popup = new PopupWindow(surface, field.getWidth(), -2, true);
        popup.setBackgroundDrawable(SettingsUiStyle.shape(getContext(), 0xffffffff, 14));
        popup.setElevation(dp(3));popup.setOutsideTouchable(true);
        popup.setInputMethodMode(PopupWindow.INPUT_METHOD_NOT_NEEDED);
        popup.setAnimationStyle(0);
        if (ValueAnimator.areAnimatorsEnabled()) {
            popup.setEnterTransition(new Fade().setDuration(130));
            popup.setExitTransition(new Fade().setDuration(90));
        }
        int available = popup.getMaxAvailableHeight(field, dp(4));
        BaseAdapter adapter = new BaseAdapter() {
            public int getCount() { return rows.size(); }
            public Object getItem(int position) { return rows.get(position); }
            public long getItemId(int position) { return position; }
            public boolean areAllItemsEnabled() { return false; }
            public boolean isEnabled(int position) { return indexes.get(position) >= 0; }
            public int getViewTypeCount() { return 2; }
            public int getItemViewType(int position) { return isEnabled(position) ? 1 : 0; }
            public View getView(int position, View recycled, ViewGroup parent) {
                Choice choice = rows.get(position);int index = indexes.get(position);
                if (index < 0) {
                    TextView heading = SettingsUiStyle.text(getContext(), choice.label, 14, SettingsUiStyle.MUTED, true);
                    heading.setGravity(Gravity.CENTER_HORIZONTAL);
                    heading.setPadding(dp(14), dp(12), dp(14), dp(6));return heading;
                }
                LinearLayout row;
                if (recycled instanceof LinearLayout) row = (LinearLayout) recycled;
                else {
                    row = new LinearLayout(getContext());row.setGravity(Gravity.CENTER_VERTICAL);
                    row.setPadding(dp(12), dp(7), dp(12), dp(7));row.setMinimumHeight(dp(50));
                    ImageView image = new ImageView(getContext());image.setScaleType(ImageView.ScaleType.FIT_CENTER);
                    LayoutParams layout = new LayoutParams(dp(46), dp(46));layout.rightMargin = dp(10);row.addView(image, layout);
                    TextView text = SettingsUiStyle.text(getContext(), "", 15, SettingsUiStyle.INK, false);
                    text.setMaxLines(2);text.setEllipsize(TextUtils.TruncateAt.END);row.addView(text, new LayoutParams(0, -2, 1));
                }
                row.setBackgroundColor(index == selected ? 0xffdde5f5 : 0xffffffff);
                row.setSelected(index == selected);
                ImageView image = (ImageView) row.getChildAt(0);
                boolean hasImage = !choice.thumbnail.isEmpty() && thumbnails != null;
                image.setVisibility(hasImage ? VISIBLE : GONE);
                if (hasImage) thumbnails.bind(image, choice.thumbnail);
                TextView label = (TextView) row.getChildAt(1);label.setText(choice.label);
                label.setTextColor(SettingsUiStyle.INK);
                label.setTypeface(Typeface.DEFAULT, index == selected ? Typeface.BOLD : Typeface.NORMAL);
                return row;
            }
        };
        list.setAdapter(adapter);
        // Measure actual labels, headers and thumbnails. Short text-only lists
        // must not inherit the larger row height used by appearance previews.
        int limit = Math.max(1, Math.min(dp(350), available));
        int height = list.getPaddingTop() + list.getPaddingBottom();
        View measured = null;int previousType = -1;
        for (int i = 0; i < adapter.getCount() && height < limit; i++) {
            int type = adapter.getItemViewType(i);
            measured = adapter.getView(i, type == previousType ? measured : null, list);
            measured.measure(MeasureSpec.makeMeasureSpec(field.getWidth(), MeasureSpec.EXACTLY),
                MeasureSpec.makeMeasureSpec(0, MeasureSpec.UNSPECIFIED));
            height += measured.getMeasuredHeight();previousType = type;
        }
        popup.setHeight(Math.min(height, limit));
        list.setOnItemClickListener((parent, view, position, id) -> {
            int index = indexes.get(position);if (index < 0) return;
            selected = index;popup.dismiss();update("");changed.run();
        });
        popup.showAsDropDown(field, 0, dp(4), Gravity.START);
        int row = indexes.indexOf(selected);
        if (row > 0 && indexes.get(row - 1) < 0) row--;
        if (row >= 0) list.setSelectionFromTop(row, 0);
    }
    @Override protected void onDetachedFromWindow() {
        if (popup != null) popup.dismiss();super.onDetachedFromWindow();
    }
}
