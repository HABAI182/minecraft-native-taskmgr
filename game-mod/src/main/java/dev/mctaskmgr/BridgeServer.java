package dev.mctaskmgr;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.SecureRandom;
import java.security.MessageDigest;
import java.util.HexFormat;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.atomic.AtomicReference;

/** 仅监听 localhost；随机端口、256 位口令；队列溢出即断开并释放按键。 */
public final class BridgeServer implements AutoCloseable{
    public record Frame(int sequence,int sourceWidth,int sourceHeight,int flags,byte[] rgb,String diagnostic){}
    public record Input(int type,int a,int b,int c,int d,double x,double y,double z,double w){}
    private final ArrayBlockingQueue<Input> inputs=new ArrayBlockingQueue<>(2048);
    private final AtomicReference<Frame> latest=new AtomicReference<>();
    private volatile ServerSocket server;private volatile Socket client;
    private volatile boolean authenticated,closed;private Path discovery;private String token;
    public void start(){try{
        byte[] random=new byte[32];new SecureRandom().nextBytes(random);token=HexFormat.of().formatHex(random);
        server=new ServerSocket(0,1,InetAddress.getByName("127.0.0.1"));
        String local=System.getenv("LOCALAPPDATA");
        Path directory=(local==null?Path.of(System.getProperty("user.home")):Path.of(local)).resolve("MCNativeTaskmgr");
        Files.createDirectories(directory);discovery=directory.resolve("session.json");
        Path staging=directory.resolve("session-"+ProcessHandle.current().pid()+".tmp");
        Files.writeString(staging,"{\"port\":"+server.getLocalPort()+",\"pid\":"+ProcessHandle.current().pid()+",\"token\":\""+token+"\",\"protocol\":1}");
        Files.move(staging,discovery,StandardCopyOption.REPLACE_EXISTING);
        Thread accept=new Thread(this::acceptLoop,"mc-taskmgr-accept");accept.setDaemon(true);accept.start();
        NativeTaskmgrClient.LOG.info("Taskmgr bridge listening on localhost port {}",server.getLocalPort());
    }catch(IOException e){NativeTaskmgrClient.LOG.error("Cannot start bridge",e);close();}}
    public boolean connected(){return authenticated;}
    public Input poll(){return inputs.poll();}
    public void offer(Frame frame){if(authenticated)latest.set(frame);}
    private void acceptLoop(){while(!closed){try{
        Socket socket=server.accept();socket.setTcpNoDelay(true);socket.setSoTimeout(3000);
        StringBuilder line=new StringBuilder();InputStream raw=socket.getInputStream();
        for(int i=0;i<128;i++){int c=raw.read();if(c<0||c=='\n')break;line.append((char)c);}
        if(!MessageDigest.isEqual(("MCNTM1 "+token).getBytes(StandardCharsets.US_ASCII),line.toString().getBytes(StandardCharsets.US_ASCII))){socket.close();continue;}
        inputs.clear();latest.set(null);client=socket;authenticated=true;
        Thread sender=new Thread(()->sendLoop(socket),"mc-taskmgr-frames");sender.setDaemon(true);sender.start();
        try{receiveLoop(socket,new DataInputStream(raw));}
        finally{authenticated=false;socket.close();sender.join(3000);client=null;inputs.clear();latest.set(null);}
    }catch(Exception e){if(!closed)NativeTaskmgrClient.LOG.debug("Bridge disconnected",e);}}}
    private void receiveLoop(Socket socket,DataInputStream in)throws IOException{
        while(!closed&&client==socket){
            int length=in.readInt();if(length<4||length>128)throw new IOException("Invalid input length");
            byte[] payload=in.readNBytes(length);if(payload.length!=length)throw new EOFException();
            DataInputStream data=new DataInputStream(new ByteArrayInputStream(payload));int type=data.readInt();
            int a=0,b=0,c=0,d=0;double x=0,y=0,z=0,w=0;
            switch(type){
                case 0->{if(length!=4)throw new IOException("Heartbeat length");continue;}
                case 10->{if(length!=20)throw new IOException("Key length");a=data.readInt();b=data.readInt();c=data.readInt();d=data.readInt();if(a<0||a>348||c<0||c>2)throw new IOException("Key range");}
                case 11->{if(length!=16)throw new IOException("Button length");a=data.readInt();b=data.readInt();c=data.readInt();if(a<0||a>7||b<0||b>1)throw new IOException("Button range");}
                case 12->{if(length!=36)throw new IOException("Motion length");x=data.readDouble();y=data.readDouble();z=data.readDouble();w=data.readDouble();if(!Double.isFinite(x+y+z+w)||Math.abs(x)>10000||Math.abs(y)>10000||z<0||z>1||w<0||w>1)throw new IOException("Motion range");}
                case 13->{if(length!=20)throw new IOException("Scroll length");x=data.readDouble();y=data.readDouble();if(!Double.isFinite(x+y)||Math.abs(x)+Math.abs(y)>100)throw new IOException("Scroll range");}
                case 14->{if(length!=12)throw new IOException("Char length");a=data.readInt();b=data.readInt();if(!Character.isValidCodePoint(a))throw new IOException("Char range");}
                case 15->{if(length!=8)throw new IOException("Focus length");a=data.readInt();}
                case 16->{if(length!=4)throw new IOException("Reset length");}
                default->throw new IOException("Unknown input type");
            }
            if(!inputs.offer(new Input(type,a,b,c,d,x,y,z,w)))throw new IOException("Input queue overflow");
        }
    }
    private void sendLoop(Socket socket){try{
        DataOutputStream out=new DataOutputStream(socket.getOutputStream());
        while(!closed&&authenticated&&client==socket){
            Frame f=latest.getAndSet(null);if(f==null){Thread.sleep(5);continue;}
            out.writeInt(28+f.rgb().length);out.writeInt(1);out.writeInt(f.sequence());
            out.writeInt(NativeTaskmgrClient.WIDTH);out.writeInt(NativeTaskmgrClient.HEIGHT);
            out.writeInt(f.sourceWidth());out.writeInt(f.sourceHeight());out.writeInt(f.flags());out.write(f.rgb());
            if(f.sequence()%10==0){
                byte[] status=f.diagnostic().getBytes(StandardCharsets.UTF_8);
                out.writeInt(status.length+4);out.writeInt(2);out.write(status);
            }
            out.flush();
        }
    }catch(Exception e){try{socket.close();}catch(IOException ignored){}}}
    public void disconnect(){authenticated=false;Socket socket=client;if(socket!=null)try{socket.close();}catch(IOException ignored){}}
    @Override public void close(){
        closed=true;disconnect();if(server!=null)try{server.close();}catch(IOException ignored){}
        if(discovery!=null)try{if(Files.exists(discovery)&&Files.readString(discovery).contains(token))Files.delete(discovery);}catch(IOException ignored){}
    }
}
