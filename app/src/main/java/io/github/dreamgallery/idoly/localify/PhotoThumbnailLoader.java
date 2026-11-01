package io.github.dreamgallery.idoly.localify;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.os.Handler;
import android.os.Looper;
import android.util.Base64;
import android.util.LruCache;
import android.widget.ImageView;
import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Dialog-owned, bounded preview cache. Only attached rows request game assets. */
final class PhotoThumbnailLoader implements AutoCloseable {
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final ArrayList<WeakReference<ImageView>> views = new ArrayList<>();
    private final Set<String> unavailable = new HashSet<>();
    private final LruCache<String, Bitmap> cache = new LruCache<String, Bitmap>(2 * 1024 * 1024) {
        @Override protected int sizeOf(String key, Bitmap bitmap) { return bitmap.getByteCount(); }
    };
    private volatile boolean closed;
    private boolean loading;
    private final Runnable poll = this::loadVisible;

    void bind(ImageView image, String asset) {
        image.setTag(asset);
        image.setImageBitmap(cache.get(asset));
        boolean known = false;
        for (WeakReference<ImageView> reference : views) if (reference.get() == image) { known = true; break; }
        if (!known) views.add(new WeakReference<>(image));
        ui.removeCallbacks(poll);
        if (!closed) ui.postDelayed(poll, 100);
    }

    private void loadVisible() {
        if (closed) return;
        if (loading) { ui.postDelayed(poll, 350); return; }
        LinkedHashSet<String> requests = new LinkedHashSet<>();
        views.removeIf(reference -> reference.get() == null);
        for (WeakReference<ImageView> reference : views) {
            ImageView view = reference.get();
            if (view == null || !view.isAttachedToWindow() || !view.isShown()) continue;
            String asset = (String) view.getTag();
            Bitmap ready = cache.get(asset);
            if (ready != null) view.setImageBitmap(ready);
            else if (!asset.isEmpty() && !unavailable.contains(asset) && requests.size() < 16) requests.add(asset);
        }
        if (requests.isEmpty()) { ui.postDelayed(poll, 350); return; }
        loading = true;
        worker.execute(() -> {
            java.util.Map<String, Bitmap> ready = new java.util.HashMap<>();
            Set<String> failed = new HashSet<>();
            for (String asset : requests) {
                if (closed) break;
                try {
                    String png = HookEntry.photoThumbnail(asset);
                    if (png.equals("!")) { failed.add(asset); continue; }
                    if (png.isEmpty()) continue;
                    byte[] bytes = Base64.decode(png, Base64.DEFAULT);
                    Bitmap bitmap = BitmapFactory.decodeByteArray(bytes, 0, bytes.length);
                    if (bitmap != null) ready.put(asset, bitmap); else failed.add(asset);
                } catch (RuntimeException e) { failed.add(asset); }
            }
            ui.post(() -> {
                loading = false;
                if (closed) return;
                unavailable.addAll(failed);
                for (java.util.Map.Entry<String, Bitmap> item : ready.entrySet()) cache.put(item.getKey(), item.getValue());
                ui.removeCallbacks(poll);ui.post(poll);
            });
        });
    }

    @Override public void close() {
        if (closed) return;
        closed = true;ui.removeCallbacks(poll);
        worker.execute(HookEntry::cancelPhotoThumbnails);worker.shutdown();views.clear();cache.evictAll();
    }
}
