package io.github.dreamgallery.idoly.localify;
import java.io.*;
import java.nio.file.*;
import java.util.*;

public final class AccountArchiveTest {
    private static final char[] PASSWORD="synthetic-test-password".toCharArray();
    private static int serial;
    private static void rejected(byte[] data,Path root,char[] password) throws Exception {
        File stage=root.resolve("bad-"+(serial++)).toFile();boolean rejected=false;
        try{AccountArchive.decrypt(new ByteArrayInputStream(data),stage,password);}catch(Exception expected){rejected=true;}
        if(!rejected || stage.exists())throw new AssertionError("Invalid archive accepted or partial data retained");
    }
    public static void main(String[] args) throws Exception {
        Path root=Paths.get(args[0]);Map<String,File> files=new LinkedHashMap<>();
        for(String name:new String[]{"metadata.json","login.json","snapshot.json","snapshot.pb"}){
            Path path=root.resolve(name);Files.write(path,("fixture-"+name).getBytes(java.nio.charset.StandardCharsets.UTF_8));files.put(name,path.toFile());
        }
        byte[] photo=new byte[2*1024*1024+47];new Random(11).nextBytes(photo);
        Path large=root.resolve("picture");Files.write(large,photo);String photoName="photos/"+"a".repeat(64)+".png";files.put(photoName,large.toFile());
        ByteArrayOutputStream output=new ByteArrayOutputStream();AccountArchive.encrypt(files,output,PASSWORD);byte[] encrypted=output.toByteArray();
        Map<String,File> restored=AccountArchive.decrypt(new ByteArrayInputStream(encrypted),root.resolve("restored").toFile(),PASSWORD);
        if(restored.size()!=files.size() || !Arrays.equals(Files.readAllBytes(restored.get(photoName).toPath()),photo))throw new AssertionError("Round trip changed content");
        ByteArrayOutputStream portable=new ByteArrayOutputStream();AccountArchive.writeSnapshotZip(root.resolve("restored").toFile(),portable);
        Set<String> exported=new HashSet<>();
        try(java.util.zip.ZipInputStream zip=new java.util.zip.ZipInputStream(new ByteArrayInputStream(portable.toByteArray()))){
            for(java.util.zip.ZipEntry item;(item=zip.getNextEntry())!=null;){
                if(item.getName().equals("login.json"))throw new AssertionError("Server package contains official credentials");
                exported.add(item.getName());byte[] bytes=zip.readAllBytes();
                if(!Arrays.equals(bytes,Files.readAllBytes(restored.get(item.getName()).toPath())))throw new AssertionError("Server package changed snapshot");
            }
        }
        if(exported.size()!=files.size()-1 || !exported.contains("snapshot.pb"))throw new AssertionError("Incomplete server package");
        rejected(encrypted,root,"incorrect-password".toCharArray());
        for(int position:new int[]{0,9,20,100,1024*1024+500,encrypted.length-1}){
            byte[] damaged=encrypted.clone();damaged[position]^=1;rejected(damaged,root,PASSWORD);
        }
        for(int size:new int[]{0,20,encrypted.length-1,encrypted.length-50,1024*1024+30})rejected(Arrays.copyOf(encrypted,size),root,PASSWORD);
        rejected(Arrays.copyOf(encrypted,encrypted.length+1),root,PASSWORD);
        files.put("../escape",large.toFile());boolean refused=false;
        try{AccountArchive.encrypt(files,new ByteArrayOutputStream(),PASSWORD);}catch(IOException expected){refused=true;}
        if(!refused)throw new AssertionError("Path traversal accepted");
        System.out.println("Account archive: round trip, segment authentication, wrong password, tamper, truncation, extra data and traversal passed");
    }
}
