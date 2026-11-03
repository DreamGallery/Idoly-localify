package io.github.dreamgallery.idoly.localify;

import android.content.*;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.MediaStore;
import org.json.*;
import java.io.*;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.*;
import java.text.SimpleDateFormat;
import java.util.*;
import java.util.function.Consumer;

/** Coordinates current-account snapshots, encrypted files and Downloads publication. */
final class AccountFiles {
    private AccountFiles() {}
    static File export(Context context,char[] password,boolean photos,Consumer<String> progress) throws Exception {
        if(ServerSettingsUi.active()){Arrays.fill(password,'\0');throw new IOException("请恢复官方连接并登录后再导出官方账号备份");}
        File work=new File(AccountCredentials.directory(context),"export-"+UUID.randomUUID());
        if(!work.mkdir())throw new IOException("无法创建导出目录");
        File encrypted=new File(AccountCredentials.directory(context),"account-"+
            new SimpleDateFormat("yyyyMMdd-HHmmss",Locale.ROOT).format(new Date())+"-"+UUID.randomUUID().toString().substring(0,8)+".idolyaccount");
        boolean complete=false;
        try {
            JSONObject login=AccountCredentials.read(context);
            progress.accept("读取账号数据快照…");
            if(!HookEntry.requestAccountSnapshot(photos))throw new IOException("账号快照接口尚未就绪或正在导出");
            long deadline=System.nanoTime()+30L*60*1000000000;
            for(;;){
                JSONObject state=new JSONObject(HookEntry.accountSnapshotStatus());
                if(!state.optString("error").isEmpty())throw new IOException(state.getString("error"));
                if(state.optBoolean("complete"))break;
                if(System.nanoTime()>deadline)throw new IOException("账号快照超时，请重启游戏后再试");
                if(state.optInt("photo_count")>0)progress.accept("读取照片地址："+state.optInt("photos_resolved")+" / "+state.optInt("photo_count"));
                Thread.sleep(150);
            }
            JSONObject snapshot=new JSONObject(HookEntry.accountSnapshotDocument());
            byte[] protobuf=HookEntry.accountSnapshotProtobuf();
            if(protobuf.length==0)throw new IOException("账号快照为空");
            if(!login.getString("firebase_uid").equals(AccountCredentials.read(context).getString("firebase_uid")))throw new IOException("导出期间账号已切换，请重试");
            JSONObject identity=snapshot.getJSONObject("data").getJSONObject("userPublic");
            JSONObject metadata=new JSONObject().put("schema_version",1).put("package_name",context.getPackageName())
                .put("game_version",context.getPackageManager().getPackageInfo(context.getPackageName(),0).versionName)
                .put("public_user_id",identity.getString("publicUserId"))
                .put("server_user_id",identity.getString("serverUserId"))
                .put("captured_at_unix",snapshot.getLong("captured_at_unix"))
                .put("photos_included",photos).put("snapshot_scope","current_UserGetResponse");
            LinkedHashMap<String,File> entries=new LinkedHashMap<>();
            JSONArray download=snapshot.optJSONArray("photo_downloads");snapshot.remove("photo_downloads");
            writeJson(entries,work,"metadata.json",metadata);writeJson(entries,work,"login.json",login);
            writeJson(entries,work,"snapshot.json",snapshot);
            File binary=new File(work,"snapshot.pb");Files.write(binary.toPath(),protobuf);entries.put("snapshot.pb",binary);
            if(photos){
                File directory=new File(work,"photos");if(!directory.mkdir())throw new IOException("无法创建照片目录");
                JSONArray index=new JSONArray();long total=protobuf.length;
                for(int i=0;download!=null && i<download.length();i++){
                    progress.accept("下载照片："+(i+1)+" / "+download.length());
                    JSONObject photo=download.getJSONObject(i);File file=new File(directory,"download");
                    String extension=downloadPhoto(photo.getString("url"),file);
                    total+=file.length();if(total>AccountArchive.MAX_TOTAL-64*1024*1024)throw new IOException("照片总大小超过备份上限");
                    String hash=hash(file);String name="photos/"+hash+extension;File target=new File(work,name);
                    Files.move(file.toPath(),target.toPath(),StandardCopyOption.REPLACE_EXISTING);entries.put(name,target);
                    index.put(new JSONObject().put("photo_id",photo.getString("photo_id"))
                        .put("original_file_name",photo.getString("file_name")).put("file",name).put("sha256",hash));
                }
                writeJson(entries,work,"photos/index.json",new JSONObject().put("schema_version",1).put("photos",index));
            }
            progress.accept("加密账号备份…");
            try(FileOutputStream out=new FileOutputStream(encrypted)){AccountArchive.encrypt(entries,out,password);}
            complete=true;return encrypted;
        }finally{
            Arrays.fill(password,'\0');
            try{AccountArchive.deleteStaging(work);}finally{if(!complete)Files.deleteIfExists(encrypted.toPath());}
        }
    }
    static String publish(Context context,File encrypted) throws Exception {
        if(Build.VERSION.SDK_INT<29)throw new IOException("Android 9 请选择保存位置");
        ContentResolver resolver=context.getContentResolver();ContentValues values=new ContentValues();
        values.put(MediaStore.MediaColumns.DISPLAY_NAME,encrypted.getName());
        values.put(MediaStore.MediaColumns.MIME_TYPE,"application/octet-stream");
        values.put(MediaStore.MediaColumns.RELATIVE_PATH,Environment.DIRECTORY_DOWNLOADS+"/Idoly-localify/accounts/");
        values.put(MediaStore.MediaColumns.IS_PENDING,1);
        Uri uri=resolver.insert(MediaStore.Downloads.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY),values);
        if(uri==null)throw new IOException("无法在下载目录创建备份");
        try{
            copyVerified(resolver,encrypted,uri);
            ContentValues published=new ContentValues();published.put(MediaStore.MediaColumns.IS_PENDING,0);
            if(resolver.update(uri,published,null,null)!=1)throw new IOException("无法发布下载目录中的备份");
            return "Download/Idoly-localify/accounts/"+encrypted.getName();
        }catch(Exception ex){resolver.delete(uri,null,null);throw ex;}
        finally{Files.deleteIfExists(encrypted.toPath());}
    }
    static void copyVerified(ContentResolver resolver,File source,Uri destination) throws Exception {
        try(InputStream input=new FileInputStream(source);OutputStream output=resolver.openOutputStream(destination,"w")){
            if(output==null)throw new IOException("无法写入目标文件");copy(input,output,AccountArchive.MAX_TOTAL+16*1024*1024);
        }
        try(InputStream input=resolver.openInputStream(destination)){
            if(input==null || !hash(source).equals(hash(input)))throw new IOException("下载目录中的文件校验失败");
        }
    }
    static File readBackup(Context context,Uri uri,char[] password) throws Exception {
        File stage=new File(AccountCredentials.directory(context),"import-"+UUID.randomUUID());
        try(InputStream input=context.getContentResolver().openInputStream(uri)){
            if(input==null)throw new IOException("无法读取所选备份");AccountArchive.decrypt(input,stage,password);
            JSONObject metadata=AccountCredentials.readJson(new File(stage,"metadata.json"),256*1024);
            JSONObject snapshot=AccountCredentials.readJson(new File(stage,"snapshot.json"),AccountArchive.MAX_ENTRY);
            // Snapshot imports remain usable without a live Firebase installation.
            // Official-login restoration performs the app/project binding separately.
            AccountCredentials.validateStructure(AccountCredentials.readJson(new File(stage,"login.json"),256*1024));
            JSONObject identity=snapshot.getJSONObject("data").getJSONObject("userPublic");
            if(metadata.getInt("schema_version")!=1 || !metadata.getString("package_name").equals(context.getPackageName()) ||
                !identity.getString("serverUserId").equals(metadata.getString("server_user_id")) ||
                !identity.getString("publicUserId").equals(metadata.getString("public_user_id")))throw new IOException("账号快照身份不一致");
            return stage;
        }catch(Exception error){AccountArchive.deleteStaging(stage);throw error;}
        finally{Arrays.fill(password,'\0');}
    }
    static File restoreSnapshot(Context context,File stage) throws Exception {
        // A compatible server may import this snapshot. Official account tokens never go here.
        File root=new File(AccountCredentials.directory(context),"snapshots");if(!root.isDirectory() && !root.mkdirs())throw new IOException("无法创建快照目录");
        File destination=new File(root,UUID.randomUUID().toString());
        if(!destination.mkdir())throw new IOException("无法创建快照目录");
        try {
            for(String name:new String[]{"metadata.json","snapshot.json","snapshot.pb"})
                Files.copy(new File(stage,name).toPath(),new File(destination,name).toPath());
            File photos=new File(stage,"photos");if(photos.isDirectory())Files.move(photos.toPath(),new File(destination,"photos").toPath());
            return destination;
        }catch(Exception ex){AccountArchive.deleteStaging(destination);throw ex;}
    }
    static File snapshotPackage(Context context,File stage) throws Exception {
        File output=new File(AccountCredentials.directory(context),"account-data-"+UUID.randomUUID()+".zip");
        try(FileOutputStream stream=new FileOutputStream(output)){AccountArchive.writeSnapshotZip(stage,stream);return output;}
        catch(Exception error){Files.deleteIfExists(output.toPath());throw error;}
    }
    private static void writeJson(Map<String,File> entries,File root,String name,JSONObject value) throws Exception {
        File file=new File(root,name);AccountCredentials.write(file,value.toString());entries.put(name,file);
    }
    private static String downloadPhoto(String address,File file) throws Exception {
        URL url=new URL(address);
        if(!"https".equals(url.getProtocol()) || !(url.getHost().equals("firebasestorage.googleapis.com") || url.getHost().equals("storage.googleapis.com")) || url.getUserInfo()!=null)
            throw new IOException("照片下载地址不是受支持的官方存储服务");
        HttpURLConnection connection=(HttpURLConnection)url.openConnection();connection.setInstanceFollowRedirects(false);
        connection.setConnectTimeout(20000);connection.setReadTimeout(30000);
        try {
            if(connection.getResponseCode()!=200)throw new IOException("照片下载失败，请稍后重试");
            try(InputStream input=connection.getInputStream();OutputStream output=new FileOutputStream(file)){copy(input,output,AccountArchive.MAX_ENTRY);}
            byte[] header=new byte[12];try(InputStream in=new FileInputStream(file)){if(in.read(header)<12)throw new IOException("照片内容无效");}
            if((header[0]&255)==255 && (header[1]&255)==216)return ".jpg";
            if((header[0]&255)==137 && header[1]==80 && header[2]==78 && header[3]==71)return ".png";
            if(new String(header,0,4,StandardCharsets.US_ASCII).equals("RIFF") && new String(header,8,4,StandardCharsets.US_ASCII).equals("WEBP"))return ".webp";
            throw new IOException("照片格式无法识别");
        }finally{connection.disconnect();}
    }
    private static void copy(InputStream in,OutputStream out,long limit) throws IOException {
        long length=0;byte[] buffer=new byte[64*1024];int n;
        while((n=in.read(buffer))!=-1){length+=n;if(length>limit)throw new IOException("文件超出大小限制");out.write(buffer,0,n);}
    }
    static String hash(File file) throws Exception {try(InputStream in=new FileInputStream(file)){return hash(in);}}
    private static String hash(InputStream input) throws Exception {
        MessageDigest digest=MessageDigest.getInstance("SHA-256");long length=0;byte[] b=new byte[64*1024];int n;
        while((n=input.read(b))!=-1){length+=n;if(length>AccountArchive.MAX_TOTAL+16*1024*1024)throw new IOException("文件过大");digest.update(b,0,n);}
        StringBuilder hex=new StringBuilder();for(byte value:digest.digest())hex.append(String.format(Locale.ROOT,"%02x",value&255));return hex.toString();
    }
}
