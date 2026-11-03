package io.github.dreamgallery.idoly.localify;

import com.google.crypto.tink.subtle.AesGcmHkdfStreaming;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.GeneralSecurityException;
import java.security.SecureRandom;
import java.util.*;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;
import javax.crypto.SecretKeyFactory;
import javax.crypto.spec.PBEKeySpec;

/** Portable, password-encrypted account container. No Android, network or account mutation. */
final class AccountArchive {
    static final long MAX_TOTAL=8L*1024*1024*1024;
    static final long MAX_ENTRY=32L*1024*1024;
    static final int MAX_ENTRIES=5005;
    private static final byte[] MAGIC="IDOLYAC1".getBytes(StandardCharsets.US_ASCII);
    private static final int ITERATIONS=600000, END=0x49454e44;
    private AccountArchive() {}

    static void encrypt(Map<String,File> entries,OutputStream output,char[] password) throws Exception {
        validatePassword(password);
        validateEntries(entries.keySet());
        byte[] salt=new byte[16];new SecureRandom().nextBytes(salt);
        byte[] header=header(salt);output.write(header);
        try(DataOutputStream encrypted=new DataOutputStream(primitive(password,salt).newEncryptingStream(output,header))){
            encrypted.writeInt(entries.size());long total=0;
            for(Map.Entry<String,File> entry:entries.entrySet()){
                File file=entry.getValue();long size=file.length();
                if(Files.isSymbolicLink(file.toPath()) || !file.isFile() || size>MAX_ENTRY || total>MAX_TOTAL-size)
                    throw new IOException("备份文件无效或过大");
                total+=size;encrypted.writeUTF(entry.getKey());encrypted.writeLong(size);
                try(InputStream input=new FileInputStream(file)){
                    copyExactly(input,encrypted,size);
                    if(input.read()!=-1)throw new IOException("备份期间文件发生变化");
                }
            }
            encrypted.writeInt(END);
        }
    }

    /** Destination must be a new private staging directory; nothing is restored until EOF authenticates. */
    static Map<String,File> decrypt(InputStream source,File destination,char[] password) throws Exception {
        validatePassword(password);
        if(destination.exists() || !destination.mkdirs())throw new IOException("无法创建独立导入目录");
        destination.setReadable(false,false);destination.setWritable(false,false);destination.setExecutable(false,false);
        destination.setReadable(true,true);destination.setWritable(true,true);destination.setExecutable(true,true);
        boolean complete=false;
        try {
            DataInputStream input=new DataInputStream(new LimitedInput(source,MAX_TOTAL+MAX_ENTRIES*1024L));
            byte[] magic=new byte[MAGIC.length];input.readFully(magic);
            if(!Arrays.equals(magic,MAGIC))throw new IOException("不是受支持的账号备份文件");
            if(input.readInt()!=ITERATIONS)throw new IOException("不支持此备份加密参数");
            byte[] salt=new byte[16];input.readFully(salt);byte[] aad=header(salt);
            Map<String,File> files=new LinkedHashMap<>();long total=0;
            try(DataInputStream decrypted=new DataInputStream(primitive(password,salt).newDecryptingStream(input,aad))){
                int count=decrypted.readInt();if(count<4 || count>MAX_ENTRIES)throw new IOException("备份文件数量无效");
                for(int i=0;i<count;i++){
                    String name=decrypted.readUTF();checkName(name);
                    if(files.containsKey(name))throw new IOException("备份包含重复文件");
                    long size=decrypted.readLong();
                    if(size<0 || size>MAX_ENTRY || total>MAX_TOTAL-size)throw new IOException("备份内容超出限制");
                    total+=size;File file=new File(destination,name);
                    if(!file.getCanonicalPath().startsWith(destination.getCanonicalPath()+File.separator))throw new IOException("备份路径无效");
                    File parent=file.getParentFile();if(!parent.isDirectory() && !parent.mkdirs())throw new IOException("无法写入导入目录");
                    if(!file.createNewFile())throw new IOException("导入文件已存在");
                    file.setReadable(false,false);file.setWritable(false,false);file.setReadable(true,true);file.setWritable(true,true);
                    try(FileOutputStream out=new FileOutputStream(file)){copyExactly(decrypted,out,size);out.getFD().sync();}
                    files.put(name,file);
                }
                validateEntries(files.keySet());
                if(decrypted.readInt()!=END || decrypted.read()!=-1)throw new IOException("备份不完整或存在额外内容");
            }
            complete=true;return files;
        }catch(GeneralSecurityException ex){throw new IOException("密码不正确或备份校验失败",ex);}
        finally {if(!complete)deleteStaging(destination);}
    }
    /** Explicit server handoff: data and optional photos only, never the login record. */
    static void writeSnapshotZip(File source,OutputStream output) throws IOException {
        Map<String,File> entries=new LinkedHashMap<>();
        for(String name:new String[]{"metadata.json","snapshot.json","snapshot.pb"})entries.put(name,new File(source,name));
        File photos=new File(source,"photos");
        if(Files.isSymbolicLink(photos.toPath()))throw new IOException("照片目录不能是链接");
        if(photos.exists()){
            File[] files=photos.listFiles();if(files==null)throw new IOException("无法读取照片目录");
            Arrays.sort(files,Comparator.comparing(File::getName));
            for(File file:files){String name="photos/"+file.getName();checkName(name);entries.put(name,file);}
        }
        if(entries.size()>MAX_ENTRIES)throw new IOException("数据包文件过多");
        try(ZipOutputStream zip=new ZipOutputStream(output)){
            zip.setLevel(1);long total=0;
            for(Map.Entry<String,File> entry:entries.entrySet()){
                File file=entry.getValue();long size=file.length();
                if(Files.isSymbolicLink(file.toPath()) || !file.isFile() || size>MAX_ENTRY || total>MAX_TOTAL-size)
                    throw new IOException("快照文件无效或过大");
                total+=size;zip.putNextEntry(new ZipEntry(entry.getKey()));
                try(InputStream input=new FileInputStream(file)){copyExactly(input,zip,size);if(input.read()!=-1)throw new IOException("数据包导出期间文件发生变化");}
                zip.closeEntry();
            }
        }
    }
    private static byte[] header(byte[] salt) throws IOException {
        ByteArrayOutputStream bytes=new ByteArrayOutputStream();DataOutputStream out=new DataOutputStream(bytes);
        out.write(MAGIC);out.writeInt(ITERATIONS);out.write(salt);return bytes.toByteArray();
    }
    private static AesGcmHkdfStreaming primitive(char[] password,byte[] salt) throws GeneralSecurityException {
        PBEKeySpec spec=new PBEKeySpec(password,salt,ITERATIONS,256);byte[] key=null;
        try {
            key=SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256").generateSecret(spec).getEncoded();
            return new AesGcmHkdfStreaming(key,"HmacSha256",32,1024*1024,0);
        }finally{spec.clearPassword();if(key!=null)Arrays.fill(key,(byte)0);}
    }
    private static void validatePassword(char[] password) throws IOException {
        if(password==null || password.length<8 || password.length>1024)throw new IOException("备份密码需为 8 至 1024 个字符");
    }
    private static void validateEntries(Set<String> names) throws IOException {
        if(names.size()>MAX_ENTRIES || !names.containsAll(Arrays.asList("metadata.json","login.json","snapshot.json","snapshot.pb")))
            throw new IOException("备份缺少账号或快照文件");
        for(String name:names)checkName(name);
    }
    private static void checkName(String name) throws IOException {
        if(!name.matches("(?:metadata|login|snapshot)\\.json|snapshot\\.pb|photos/index\\.json|photos/[a-f0-9]{64}\\.(?:jpg|png|webp)"))
            throw new IOException("备份文件名无效");
    }
    private static void copyExactly(InputStream input,OutputStream output,long size) throws IOException {
        byte[] buffer=new byte[64*1024];long remaining=size;
        while(remaining>0){int count=input.read(buffer,0,(int)Math.min(buffer.length,remaining));
            if(count<0)throw new EOFException("备份内容不完整");if(count==0)continue;output.write(buffer,0,count);remaining-=count;}
    }
    static void deleteStaging(File directory) throws IOException {
        if(!directory.exists())return;
        if(Files.isSymbolicLink(directory.toPath()))throw new IOException("暂存目录不能是链接");
        File[] children=directory.listFiles();if(children!=null)for(File file:children){
            if(file.isDirectory() && !Files.isSymbolicLink(file.toPath()))deleteStaging(file);
            else Files.deleteIfExists(file.toPath());
        }
        Files.deleteIfExists(directory.toPath());
    }
    private static final class LimitedInput extends FilterInputStream {
        private long remaining;
        LimitedInput(InputStream in,long limit){super(in);remaining=limit;}
        public int read() throws IOException {int value=super.read();if(value>=0 && --remaining<0)throw new IOException("备份过大");return value;}
        public int read(byte[] b,int off,int len) throws IOException {int count=in.read(b,off,len);if(count>0 && (remaining-=count)<0)throw new IOException("备份过大");return count;}
    }
}
