package io.github.dreamgallery.idoly.localify;

import android.app.Activity;
import android.app.Dialog;
import android.os.Build;
import android.text.InputType;
import android.widget.*;
import org.json.JSONObject;
import java.io.File;
import java.nio.file.Files;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

/** Local account files. Official login and compatible-server snapshots are distinct operations. */
final class AccountTransferUi {
    private static final ExecutorService WORKER=Executors.newSingleThreadExecutor();
    private static final AtomicBoolean BUSY=new AtomicBoolean();
    private AccountTransferUi() {}
    static void show(Activity activity){
        Dialog dialog=new SettingsDialog(activity);
        SettingsUiStyle.Page page=new SettingsUiStyle.Page(dialog,"账号备份与导入",
            "备份包含登录凭据和当前已同步的账号数据，使用密码加密；请妥善保管文件与密码。", "返回设置",dialog::dismiss);
        LinearLayout backup=SettingsUiStyle.card(page.body,"导出账号","默认保存到 Download / Idoly-localify / accounts。");
        EditText password=password(dialog,"备份密码（至少 8 位）");backup.addView(password);
        CheckBox photos=new CheckBox(dialog.getContext());photos.setText("包含自己拍摄的照片原图");photos.setTextColor(SettingsUiStyle.INK);backup.addView(photos);
        TextView state=SettingsUiStyle.text(dialog.getContext(),AccountCredentials.status(),13,SettingsUiStyle.MUTED,false);
        backup.addView(state);
        addButton(backup,"加密导出账号",true,()->{
            char[] secret=password.getText().toString().toCharArray();password.setText("");
            if(secret.length<8 || secret.length>1024){java.util.Arrays.fill(secret,'\0');state.setText("备份密码需为 8 至 1024 个字符");return;}
            if(BUSY.get()){java.util.Arrays.fill(secret,'\0');state.setText("正在处理另一项账号操作");return;}
            boolean includePhotos=photos.isChecked();
            run(activity,state,()->{
                File file=AccountFiles.export(activity.getApplicationContext(),secret,includePhotos,message->update(activity,state,message));
                publish(activity,state,file);
            });
        });
        LinearLayout restore=SettingsUiStyle.card(page.body,"导入账号",
            "恢复登录会在重启后切换官方账号。完整快照仅导入本机，供兼容服务器读取，不会覆盖官方服务器进度。");
        EditText unlock=password(dialog,"所选备份的密码");restore.addView(unlock);
        addButton(restore,"选择备份文件",false,()->{
            if(BUSY.get()){state.setText("正在处理另一项账号操作");return;}
            try{AccountFilePicker.open(activity,uri->{
                if(uri==null)return;
                char[] secret=unlock.getText().toString().toCharArray();unlock.setText("");
                run(activity,state,()->{
                    File stage=AccountFiles.readBackup(activity,uri,secret);
                    activity.runOnUiThread(()->review(activity,stage,state));
                });
            });}catch(Exception error){state.setText("无法打开文件选择器");}
        });
        LinearLayout details=SettingsUiStyle.card(page.body,"快照范围",
            "包含游戏当前已同步的卡牌、服装、道具、剧情和其他账号数据。照片原图可选；排行榜、服务器内部记录不在客户端快照内。停服后使用仍需兼容的本地或自建服务器。");
        TextView runtime=SettingsUiStyle.text(dialog.getContext(),"",12,SettingsUiStyle.MUTED,false);details.addView(runtime);
        Runnable poll=new Runnable(){public void run(){
            if(!dialog.isShowing())return;
            try{JSONObject value=new JSONObject(HookEntry.accountSnapshotStatus());
                runtime.setText(value.optBoolean("available")?"账号数据接口已就绪 · "+value.optInt("field_count")+" 类数据":
                    "接口尚未就绪："+value.optString("error"));
            }catch(Exception ignored){runtime.setText("等待游戏初始化");}
            runtime.postDelayed(this,1000);
        }};
        dialog.setOnDismissListener(d->runtime.removeCallbacks(poll));
        dialog.setContentView(page.root);SettingsUiStyle.configureWindow(dialog);dialog.show();runtime.post(poll);
    }
    private static void review(Activity activity,File stage,TextView status){
        if(activity.isDestroyed() || activity.isFinishing()){cleanup(stage);return;}
        Dialog dialog=new SettingsDialog(activity);final boolean[] used={false};
        try{
            JSONObject meta=AccountCredentials.readJson(new File(stage,"metadata.json"),256*1024);
            SettingsUiStyle.Page page=new SettingsUiStyle.Page(dialog,"导入已校验的备份",
                "账号 ID："+meta.getString("public_user_id")+"\n游戏版本："+meta.getString("game_version"),"取消",dialog::dismiss);
            LinearLayout operations=SettingsUiStyle.card(page.body,"恢复方式",null);
            addButton(operations,"恢复官方账号登录（重启生效）",true,()->{
                used[0]=true;dialog.dismiss();run(activity,status,()->{
                    try{AccountCredentials.stage(activity,AccountCredentials.readJson(new File(stage,"login.json"),256*1024));
                        update(activity,status,"登录备份已准备，请重启并进入游戏。标题页的账号 ID 会在登录后刷新。");}
                    finally{AccountArchive.deleteStaging(stage);}
                });
            });
            addButton(operations,"导入完整快照到本机",false,()->{
                used[0]=true;dialog.dismiss();run(activity,status,()->{
                    try{AccountFiles.restoreSnapshot(activity,stage);update(activity,status,"账号数据与所含照片已导入本机；尚未连接兼容服务器。");}
                    finally{AccountArchive.deleteStaging(stage);}
                });
            });
            TextView exportHint=SettingsUiStyle.text(dialog.getContext(),"服务器数据包是未加密的 ZIP，只含进度与所选照片，不含官方登录凭据。仅交给自己信任的兼容服务器。",12,SettingsUiStyle.MUTED,false);
            exportHint.setPadding(0,SettingsUiStyle.dp(dialog.getContext(),12),0,0);operations.addView(exportHint);
            addButton(operations,"导出服务器数据包",false,()->{
                used[0]=true;dialog.dismiss();run(activity,status,()->{
                    try{publish(activity,status,AccountFiles.snapshotPackage(activity,stage));}
                    finally{AccountArchive.deleteStaging(stage);}
                });
            });
            dialog.setOnDismissListener(d->{if(!used[0])WORKER.execute(()->cleanup(stage));});
            dialog.setContentView(page.root);SettingsUiStyle.configureWindow(dialog);dialog.show();
        }catch(Exception error){cleanup(stage);status.setText("备份信息无法读取");}
    }
    private interface Job {void run() throws Exception;}
    private static void publish(Activity activity,TextView state,File file)throws Exception{
        if(Build.VERSION.SDK_INT>=29)update(activity,state,"已保存："+AccountFiles.publish(activity,file));
        else activity.runOnUiThread(()->{
            try{AccountFilePicker.save(activity,file.getName(),uri->{
                if(uri==null){WORKER.execute(()->cleanup(file));return;}
                run(activity,state,()->{try{AccountFiles.copyVerified(activity.getContentResolver(),file,uri);update(activity,state,"文件已保存");}finally{cleanup(file);}});
            });}catch(Exception e){cleanup(file);state.setText("无法打开保存位置选择器");}
        });
    }
    private static void run(Activity activity,TextView state,Job job){
        if(!BUSY.compareAndSet(false,true)){state.setText("正在处理另一项账号操作");return;}
        state.setText("处理中…");WORKER.execute(()->{
            try{job.run();}catch(Exception error){update(activity,state,"操作未完成："+safeError(error));}
            finally{BUSY.set(false);}
        });
    }
    private static String safeError(Exception error){
        String message=error.getMessage();
        // Network exceptions may embed photo URLs and their access tokens.
        if(message==null || message.contains("http") || message.contains("token") || message.length()>300)return "请检查网络、密码或备份文件后重试";
        return message;
    }
    private static void update(Activity activity,TextView state,String text){activity.runOnUiThread(()->{if(!activity.isDestroyed())state.setText(text);});}
    private static EditText password(Dialog dialog,String hint){
        EditText field=new EditText(dialog.getContext());field.setHint(hint);field.setTextColor(SettingsUiStyle.INK);
        field.setSingleLine(true);field.setInputType(InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_VARIATION_PASSWORD);
        field.setImportantForAutofill(android.view.View.IMPORTANT_FOR_AUTOFILL_NO_EXCLUDE_DESCENDANTS);return field;
    }
    private static void addButton(LinearLayout parent,String title,boolean primary,Runnable action){
        LinearLayout.LayoutParams layout=new LinearLayout.LayoutParams(-1,-2);layout.topMargin=SettingsUiStyle.dp(parent.getContext(),12);
        parent.addView(SettingsUiStyle.button(parent.getContext(),title,primary,action),layout);
    }
    private static void cleanup(File file){try{if(file.isDirectory())AccountArchive.deleteStaging(file);else Files.deleteIfExists(file.toPath());}catch(Exception ignored){}}
}
