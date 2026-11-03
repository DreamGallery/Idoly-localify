package io.github.dreamgallery.idoly.localify;

import android.app.Activity;
import android.app.Dialog;
import android.content.Context;
import android.text.InputType;
import android.widget.*;
import org.json.JSONObject;
import java.io.File;
import java.net.URI;
import java.security.SecureRandom;
import android.util.Base64;

/** A restart-only server profile. Official Firebase credentials never enter this file. */
final class ServerSettingsUi {
    private ServerSettingsUi(){}
    private static File file(Context context){return new File(context.getNoBackupFilesDir(),"idoly-server.json");}
    static String savedForStartup(Context context){
        if(!file(context).exists())return defaults().toString();
        try{return AccountCredentials.readJson(file(context),40000).toString();}
        catch(Exception error){return "{}";} // Native adapter blocks an invalid saved profile.
    }
    private static JSONObject defaults(){
        try{return new JSONObject().put("schema_version",1).put("enabled",false).put("endpoint","").put("login_token","").put("ca_pem","");}
        catch(Exception impossible){throw new IllegalStateException(impossible);}
    }
    static boolean active(){try{return new JSONObject(HookEntry.serverStatus()).optBoolean("enabled");}catch(Exception e){return false;}}
    static void show(Activity activity){
        Dialog dialog=new SettingsDialog(activity);
        SettingsUiStyle.Page page=new SettingsUiStyle.Page(dialog,"实验性 · 自定义服务器",
            "需要兼容当前游戏协议的服务器。仅接管游戏 gRPC；资源下载、网页及 Firebase 存储仍使用原来的服务。", "返回设置",dialog::dismiss);
        JSONObject saved;try{saved=new JSONObject(savedForStartup(activity));}catch(Exception e){saved=defaults();}
        LinearLayout options=SettingsUiStyle.card(page.body,"连接设置","修改后重启生效。连接失败不会自动切回官方服务器。");
        Switch enabled=new Switch(dialog.getContext());enabled.setText("使用自定义服务器");SettingsUiStyle.styleSwitch(enabled);enabled.setChecked(saved.optBoolean("enabled"));options.addView(enabled);
        EditText endpoint=field(options,"服务器地址","https://server.example",false);endpoint.setText(saved.optString("endpoint"));
        EditText token=field(options,"服务器登录凭据","填写服务器提供的独立凭据，或使用下方生成值",true);
        String stored=saved.optString("login_token");
        if(stored.isEmpty()){byte[] bytes=new byte[32];new SecureRandom().nextBytes(bytes);stored=Base64.encodeToString(bytes,Base64.URL_SAFE|Base64.NO_WRAP|Base64.NO_PADDING);}
        token.setText(stored);
        EditText certificate=field(options,"自定义 CA 证书（可留空）","PEM 格式；留空使用网络库信任的证书",false);
        certificate.setSingleLine(false);certificate.setMaxLines(4);certificate.setText(saved.optString("ca_pem"));
        TextView state=SettingsUiStyle.text(dialog.getContext(),"",13,SettingsUiStyle.MUTED,false);options.addView(state);
        addButton(options,"保存连接设置",true,()->{
            try{
                if(!enabled.isChecked()){save(activity,defaults());state.setText("已保存官方连接，请重启游戏生效");return;}
                URI address=new URI(endpoint.getText().toString().trim());
                if(address.getHost()==null || address.getUserInfo()!=null || address.getQuery()!=null || address.getFragment()!=null ||
                    (address.getPath()!=null && !address.getPath().isEmpty() && !address.getPath().equals("/")))throw new IllegalArgumentException();
                String normalized=address.getScheme().toLowerCase(java.util.Locale.ROOT)+"://"+address.getRawAuthority().toLowerCase(java.util.Locale.ROOT);
                String pem=certificate.getText().toString().trim();
                if(!pem.isEmpty())try(java.io.ByteArrayInputStream input=new java.io.ByteArrayInputStream(pem.getBytes(java.nio.charset.StandardCharsets.US_ASCII))){
                    if(java.security.cert.CertificateFactory.getInstance("X.509").generateCertificates(input).isEmpty())throw new IllegalArgumentException();
                }
                JSONObject value=new JSONObject().put("schema_version",1).put("enabled",true).put("endpoint",normalized)
                    .put("login_token",token.getText().toString().trim()).put("ca_pem",pem);
                if(!new JSONObject(HookEntry.serverStatus()).optBoolean("available"))throw new IllegalStateException("当前版本的网络接口尚未就绪");
                save(activity,value);state.setText("已保存，请重启游戏生效；官方账号的登录信息不会发送到此服务器");
            }catch(Exception e){state.setText("未保存：请检查地址、独立凭据与 CA 证书。HTTP 仅允许本机回环地址；其他地址使用 HTTPS。");}
        });
        addButton(options,"恢复官方连接",false,()->{
            try{save(activity,defaults());enabled.setChecked(false);state.setText("已保存官方连接，请重启游戏生效");}
            catch(Exception e){state.setText("未能保存设置");}
        });
        LinearLayout live=SettingsUiStyle.card(page.body,"当前连接",null);
        TextView runtime=SettingsUiStyle.text(dialog.getContext(),"",13,SettingsUiStyle.INK,false);live.addView(runtime);
        Runnable poll=new Runnable(){public void run(){
            if(!dialog.isShowing())return;
            try{JSONObject value=new JSONObject(HookEntry.serverStatus());
                runtime.setText((value.optBoolean("enabled")?value.optString("endpoint"):"官方服务器")+"\n"+
                    (value.optBoolean("available")?"网络接口已就绪":"网络接口不可用")+
                    (value.optBoolean("enabled")?" · 请求 "+value.optInt("requests"):"")+
                    (value.optString("error").isEmpty()?"":"\n"+value.optString("error")));
            }catch(Exception e){runtime.setText("等待游戏初始化");}
            runtime.postDelayed(this,1000);
        }};
        dialog.setOnDismissListener(d->runtime.removeCallbacks(poll));dialog.setContentView(page.root);SettingsUiStyle.configureWindow(dialog);dialog.show();runtime.post(poll);
    }
    private static void save(Context context,JSONObject value)throws Exception{
        if(!HookEntry.validateServer(value.toString()))throw new IllegalArgumentException();
        AccountCredentials.write(file(context),value.toString());
    }
    private static EditText field(LinearLayout parent,String label,String hint,boolean secret){
        TextView caption=SettingsUiStyle.text(parent.getContext(),label,13,SettingsUiStyle.MUTED,false);caption.setPadding(0,SettingsUiStyle.dp(parent.getContext(),12),0,0);parent.addView(caption);
        EditText field=new EditText(parent.getContext());field.setTextColor(SettingsUiStyle.INK);field.setTextSize(14);field.setHint(hint);field.setSingleLine(true);
        field.setInputType(InputType.TYPE_CLASS_TEXT|(secret?InputType.TYPE_TEXT_VARIATION_PASSWORD:InputType.TYPE_TEXT_VARIATION_URI));
        field.setImportantForAutofill(android.view.View.IMPORTANT_FOR_AUTOFILL_NO_EXCLUDE_DESCENDANTS);parent.addView(field);return field;
    }
    private static void addButton(LinearLayout parent,String text,boolean primary,Runnable action){
        LinearLayout.LayoutParams layout=new LinearLayout.LayoutParams(-1,-2);layout.topMargin=SettingsUiStyle.dp(parent.getContext(),12);
        parent.addView(SettingsUiStyle.button(parent.getContext(),text,primary,action),layout);
    }
}
