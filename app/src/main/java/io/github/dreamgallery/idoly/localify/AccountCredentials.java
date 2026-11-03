package io.github.dreamgallery.idoly.localify;

import android.content.Context;
import android.content.SharedPreferences;
import android.util.Base64;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;

/** Only the Firebase account record is restored, before Firebase providers initialize. */
final class AccountCredentials {
    private static final String STORE="com.google.firebase.auth.api.Store.";
    private static final String USER="com.google.firebase.auth.FIREBASE_USER";
    private static final String TOKEN="com.google.firebase.auth.GET_TOKEN_RESPONSE.";
    private static volatile String restoreStatus="";
    private AccountCredentials() {}

    static File directory(Context context) throws IOException {
        File dir=new File(context.getNoBackupFilesDir(),"idoly-account");
        if(!dir.isDirectory() && !dir.mkdirs())throw new IOException("无法创建账号暂存目录");
        return dir;
    }
    static JSONObject read(Context context) throws Exception {
        JSONObject selected=find(context);
        if(selected==null)throw new IOException("尚未找到登录凭据，请先登录账号");
        return selected;
    }
    private static JSONObject find(Context context) throws Exception {
        File prefs=new File(context.getApplicationInfo().dataDir,"shared_prefs");
        File[] candidates=prefs.listFiles((d,n)->n.startsWith(STORE) && n.endsWith(".xml"));
        JSONObject selected=null;
        if(candidates!=null)for(File candidate:candidates){
            String name=candidate.getName().substring(0,candidate.getName().length()-4);
            SharedPreferences store=context.getSharedPreferences(name,Context.MODE_PRIVATE);
            String raw=store.getString(USER,null);if(raw==null)continue;
            JSONObject user=new JSONObject(raw);if(!"[DEFAULT]".equals(user.optString("applicationName")))continue;
            String uid=userId(user);String token=store.getString(TOKEN+uid,null);
            if(token==null)throw new IOException("账号登录记录不完整");
            JSONObject value=new JSONObject().put("schema_version",1).put("store",name)
                .put("firebase_uid",uid).put("user",user).put("token",new JSONObject(token));
            validate(context,value);
            if(selected!=null)throw new IOException("发现多个默认账号记录，无法确定导出对象");
            selected=value;
        }
        return selected;
    }
    static void validate(Context context,JSONObject data) throws Exception {
        validateStructure(data);
        // Bind the destination to the installed game's Firebase app, not an arbitrary preference file.
        int resource=context.getResources().getIdentifier("google_app_id","string",context.getPackageName());
        if(resource==0)throw new IOException("无法核对游戏的 Firebase 应用配置");
        String expected=STORE+encoded("[DEFAULT]")+"+"+encoded(context.getString(resource));
        if(!data.getString("store").equals(expected))throw new IOException("备份与当前游戏的 Firebase 应用不匹配");
    }
    static void validateStructure(JSONObject data) throws Exception {
        if(data.getInt("schema_version")!=1 || data.toString().length()>256*1024)throw new IOException("登录备份格式不支持");
        String store=data.getString("store");
        if(!store.matches("com\\.google\\.firebase\\.auth\\.api\\.Store\\.[A-Za-z0-9_+=-]{1,512}"))throw new IOException("登录存储名称无效");
        JSONObject user=data.getJSONObject("user"),token=data.getJSONObject("token");
        if(!"[DEFAULT]".equals(user.getString("applicationName")) ||
            !"com.google.firebase.auth.internal.DefaultFirebaseUser".equals(user.getString("type")))throw new IOException("不支持此账号提供方");
        String uid=userId(user);
        if(!uid.matches("[A-Za-z0-9_-]{1,128}") || !uid.equals(data.getString("firebase_uid")))throw new IOException("登录身份不一致");
        String refresh=token.getString("refresh_token");
        if(refresh.isEmpty() || refresh.length()>16384 || !refresh.equals(new JSONObject(user.getString("cachedTokenState")).getString("refresh_token")))
            throw new IOException("登录凭据不完整或不一致");
    }
    private static String userId(JSONObject user) throws Exception {
        JSONArray users=user.getJSONArray("userInfos");String uid=null;
        for(int i=0;i<users.length();i++){
            JSONObject info=new JSONObject(users.getString(i));
            if("firebase".equals(info.optString("providerId")))uid=info.getString("userId");
        }
        if(uid==null)throw new IOException("登录备份缺少 Firebase 身份");return uid;
    }
    private static String encoded(String value){return Base64.encodeToString(value.getBytes(StandardCharsets.UTF_8),Base64.URL_SAFE|Base64.NO_WRAP|Base64.NO_PADDING);}
    static void stage(Context context,JSONObject credentials) throws Exception {
        if(ServerSettingsUi.active())throw new IOException("请先恢复官方连接并重启游戏，再导入官方账号登录");
        validate(context,credentials);
        File pending=new File(directory(context),"pending-login.json");
        if(pending.exists())throw new IOException("已有等待重启的登录导入，请先重启游戏");
        // Current account recovery point stays in the application's private no-backup directory.
        JSONObject previous=find(context);
        if(previous==null)previous=new JSONObject().put("schema_version",1).put("empty",true).put("store",credentials.getString("store"));
        write(new File(directory(context),"before-import.json"),previous.toString());
        write(pending,credentials.toString());
        restoreStatus="登录备份已准备，重启游戏后恢复";
    }
    static void earlyRestore(Context context) {
        try {
            File pending=new File(directory(context),"pending-login.json");
            if(!pending.isFile())return;
            JSONObject data=readJson(pending,256*1024);validate(context,data);
            SharedPreferences prefs=context.getSharedPreferences(data.getString("store"),Context.MODE_PRIVATE);
            SharedPreferences.Editor edit=prefs.edit();
            for(String key:prefs.getAll().keySet())if(key.equals(USER) || key.startsWith(TOKEN))edit.remove(key);
            edit.putString(USER,data.getJSONObject("user").toString());
            edit.putString(TOKEN+data.getString("firebase_uid"),data.getJSONObject("token").toString());
            if(!edit.commit())throw new IOException("无法恢复登录记录");
            Files.delete(pending.toPath());restoreStatus="登录备份已载入；进入游戏后刷新账号信息，标题页可能暂时显示旧 ID";
        }catch(Exception error){restoreStatus="登录备份未恢复；原备份仍保留，请重新导入";}
    }
    static String status(){return restoreStatus;}
    static JSONObject readJson(File file,long limit) throws Exception {
        if(!file.isFile() || file.length()<2 || file.length()>limit || Files.isSymbolicLink(file.toPath()))throw new IOException("备份文件无效");
        return new JSONObject(new String(Files.readAllBytes(file.toPath()),StandardCharsets.UTF_8));
    }
    static void write(File destination,String data) throws IOException {
        File temp=File.createTempFile(".account-",".tmp",destination.getParentFile());
        try(FileOutputStream out=new FileOutputStream(temp)){out.write(data.getBytes(StandardCharsets.UTF_8));out.getFD().sync();}
        try{Files.move(temp.toPath(),destination.toPath(),StandardCopyOption.ATOMIC_MOVE,StandardCopyOption.REPLACE_EXISTING);}
        finally{Files.deleteIfExists(temp.toPath());}
    }
}
