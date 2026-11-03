package io.github.dreamgallery.idoly.localify;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.provider.DocumentsContract;
import java.lang.reflect.Method;
import java.util.HashSet;
import java.util.Set;
import java.util.function.Consumer;
import de.robv.android.xposed.XC_MethodHook;
import de.robv.android.xposed.XposedBridge;

/** Uses the game's Activity result delivery without adding a host-package component. */
final class AccountFilePicker {
    private static final int REQUEST=0x4c69;
    private static final Set<Method> HOOKED=new HashSet<>();
    private static Consumer<Uri> pending;
    private static Activity owner;
    private AccountFilePicker() {}
    static void open(Activity activity,Consumer<Uri> result) throws Exception {
        launch(activity,new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*")
            .addCategory(Intent.CATEGORY_OPENABLE),result);
    }
    static void save(Activity activity,String name,Consumer<Uri> result) throws Exception {
        launch(activity,new Intent(Intent.ACTION_CREATE_DOCUMENT).setType("application/octet-stream")
            .addCategory(Intent.CATEGORY_OPENABLE).putExtra(Intent.EXTRA_TITLE,name),result);
    }
    private static void launch(Activity activity,Intent intent,Consumer<Uri> result) throws Exception {
        if(pending!=null)throw new IllegalStateException("请先完成当前文件选择");
        Method target=null;
        for(Class<?> type=activity.getClass();type!=null;type=type.getSuperclass()){
            try{target=type.getDeclaredMethod("onActivityResult",int.class,int.class,Intent.class);break;}
            catch(NoSuchMethodException ignored){}
        }
        if(target==null)throw new IllegalStateException("无法接入系统文件选择器");
        if(!HOOKED.contains(target)){XposedBridge.hookMethod(target,new XC_MethodHook(){
            @Override protected void afterHookedMethod(MethodHookParam param){
                if((Integer)param.args[0]!=REQUEST || param.thisObject!=owner || pending==null)return;
                Consumer<Uri> callback=pending;pending=null;owner=null;
                Intent data=(Intent)param.args[2];
                callback.accept((Integer)param.args[1]==Activity.RESULT_OK && data!=null?data.getData():null);
            }
        });HOOKED.add(target);}
        intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI,Uri.parse("content://com.android.externalstorage.documents/document/primary%3ADownload"));
        pending=result;owner=activity;
        try{activity.startActivityForResult(intent,REQUEST);}catch(Exception e){pending=null;owner=null;throw e;}
    }
}
