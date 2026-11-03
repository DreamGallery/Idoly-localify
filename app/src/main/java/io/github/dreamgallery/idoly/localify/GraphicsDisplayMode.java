package io.github.dreamgallery.idoly.localify;

import android.app.Activity;
import android.app.Application;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Display;
import android.view.WindowManager;
import java.lang.ref.WeakReference;
import java.util.WeakHashMap;
import org.json.JSONObject;

/** Requests an existing high-refresh display mode on the game window only. */
final class GraphicsDisplayMode {
    private static final Handler MAIN=new Handler(Looper.getMainLooper());
    private static final WeakHashMap<Activity,Original> ORIGINAL=new WeakHashMap<>();
    private static boolean installed;
    private static final class Original {
        final int mode;final float rate;
        Original(WindowManager.LayoutParams p){mode=p.preferredDisplayModeId;rate=p.preferredRefreshRate;}
    }
    static void install(Application app) {
        if(installed)return;installed=true;
        app.registerActivityLifecycleCallbacks(new Application.ActivityLifecycleCallbacks(){
            public void onActivityResumed(Activity a){schedule(a,0);}
            public void onActivityCreated(Activity a,Bundle b){}
            public void onActivityStarted(Activity a){}
            public void onActivityPaused(Activity a){}
            public void onActivityStopped(Activity a){}
            public void onActivitySaveInstanceState(Activity a,Bundle b){}
            public void onActivityDestroyed(Activity a){ORIGINAL.remove(a);}
        });
    }
    static void schedule(Activity activity,int attempt) {
        if(!isGameActivity(activity))return;
        WeakReference<Activity> weak=new WeakReference<>(activity);
        MAIN.postDelayed(()->{
            Activity a=weak.get();if(a==null || a.isDestroyed() || a.isFinishing())return;
            try{
                JSONObject status=new JSONObject(HookEntry.graphicsSettingsStatus());
                if(status.optBoolean("available") && !status.optBoolean("pending"))apply(a,status.optJSONObject("active"));
                else if(attempt<20 && status.optString("error").isEmpty())schedule(a,attempt+1);
            }catch(Exception ignored){}
        },attempt==0?100:1000);
    }
    static void apply(Activity activity,JSONObject config) {
        if(config==null || activity.isDestroyed() || !isGameActivity(activity))return;
        Display display=activity.getWindowManager().getDefaultDisplay();
        WindowManager.LayoutParams p=activity.getWindow().getAttributes();
        int fps=config.optBoolean("enabled")?config.optInt("fps"):0;
        if(fps==0){
            Original previous=ORIGINAL.remove(activity);if(previous==null)return;
            p.preferredDisplayModeId=previous.mode;p.preferredRefreshRate=previous.rate;
            activity.getWindow().setAttributes(p);return;
        }
        Display.Mode current=display.getMode(),best=null;
        for(Display.Mode mode:display.getSupportedModes()){
            if(mode.getPhysicalWidth()!=current.getPhysicalWidth() || mode.getPhysicalHeight()!=current.getPhysicalHeight())continue;
            if(best==null || (best.getRefreshRate()<fps-.5f && mode.getRefreshRate()>best.getRefreshRate()) ||
                (mode.getRefreshRate()>=fps-.5f && mode.getRefreshRate()<best.getRefreshRate()))best=mode;
        }
        if(best==null)return;
        if(!ORIGINAL.containsKey(activity))ORIGINAL.put(activity,new Original(p));
        if(p.preferredDisplayModeId!=best.getModeId() || p.preferredRefreshRate!=best.getRefreshRate()){
            p.preferredDisplayModeId=best.getModeId();p.preferredRefreshRate=best.getRefreshRate();
            activity.getWindow().setAttributes(p);
        }
    }
    private static boolean isGameActivity(Activity activity){
        for(Class<?> type=activity.getClass();type!=null;type=type.getSuperclass())
            if(type.getName().equals("com.unity3d.player.UnityPlayerActivity"))return true;
        return false;
    }
}
