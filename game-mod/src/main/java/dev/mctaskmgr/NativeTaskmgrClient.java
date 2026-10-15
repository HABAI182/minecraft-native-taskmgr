package dev.mctaskmgr;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientLifecycleEvents;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gl.Framebuffer;
import net.minecraft.client.gl.SimpleFramebuffer;
import net.minecraft.client.option.KeyBinding;
import org.lwjgl.glfw.GLFW;
import org.lwjgl.opengl.*;
import org.lwjgl.system.MemoryUtil;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import dev.mctaskmgr.mixin.MouseAccess;
import dev.mctaskmgr.mixin.KeyboardAccess;
import java.nio.ByteBuffer;
import java.util.HashSet;
import java.util.Set;

/** Minecraft/GL 操作只在客户端线程执行；网络线程只传输不可变数据。 */
public final class NativeTaskmgrClient implements ClientModInitializer {
    public static final Logger LOG=LoggerFactory.getLogger("mc_native_taskmgr");
    public static final int WIDTH=64, HEIGHT=36;
    public static final BridgeServer SERVER=new BridgeServer();
    private static final Set<Integer> keys=new HashSet<>();
    private static boolean controlled, sessionActive, savedPause, savedVsync;
    private static SimpleFramebuffer reduced;
    private static ByteBuffer pixels;
    private static long nextFrame;
    private static int sequence;
    @Override public void onInitializeClient(){
        SERVER.start();
        ClientTickEvents.START_CLIENT_TICK.register(NativeTaskmgrClient::tick);
        ClientLifecycleEvents.CLIENT_STOPPING.register(client->{
            release(client);SERVER.close();
            if(reduced!=null)reduced.delete();if(pixels!=null)MemoryUtil.memFree(pixels);
        });
    }
    public static boolean active(){return SERVER.connected();}
    public static boolean controlled(){return controlled;}
    public static boolean keyDown(int key){return keys.contains(key);}
    private static void tick(MinecraftClient client){
        boolean connected=active();
        if(connected&&!sessionActive){
            // 先恢复真实的系统鼠标模式，再让后续 Mouse.lockCursor 只操作逻辑锁。
            GLFW.glfwSetInputMode(client.getWindow().getHandle(),GLFW.GLFW_CURSOR,GLFW.GLFW_CURSOR_NORMAL);
            sessionActive=true;savedPause=client.options.pauseOnLostFocus;
            savedVsync=client.options.getEnableVsync().getValue();
            client.options.pauseOnLostFocus=false;client.getWindow().setVsync(false);
            ((MouseAccess)client.mouse).mc$setLocked(false);
        }
        if(!connected&&sessionActive){
            release(client);sessionActive=false;client.options.pauseOnLostFocus=savedPause;
            client.getWindow().setVsync(savedVsync);client.mouse.unlockCursor();
        }
        BridgeServer.Input event;
        for(int count=0;count<512&&(event=SERVER.poll())!=null;count++){
            if(!connected)continue;
            long window=client.getWindow().getHandle();MouseAccess mouse=(MouseAccess)client.mouse;
            switch(event.type()){
                case 10->{if(controlled){
                    if(event.c()==GLFW.GLFW_RELEASE)keys.remove(event.a());else keys.add(event.a());
                    client.keyboard.onKey(window,event.a(),event.b(),event.c(),event.d());
                }}
                case 11->{if(controlled)mouse.mc$button(window,event.a(),event.b(),event.c());}
                case 12->{if(controlled){
                    if(client.currentScreen==null&&client.player!=null){
                        // 进入原版 Mouse.tick，保留灵敏度、反转 Y、平滑镜头、望远镜行为。
                        mouse.mc$setLocked(true);
                        mouse.mc$setDeltaX(mouse.mc$getDeltaX()+event.x());
                        mouse.mc$setDeltaY(mouse.mc$getDeltaY()+event.y());
                    }else mouse.mc$cursor(window,event.z()*client.getWindow().getWidth(),event.w()*client.getWindow().getHeight());
                }}
                case 13->{if(controlled)mouse.mc$scroll(window,event.x(),event.y());}
                case 14->{if(controlled)((KeyboardAccess)client.keyboard).mc$char(window,event.a(),event.b());}
                case 15->{
                    release(client);controlled=event.a()!=0;
                    if(controlled){GLFW.glfwIconifyWindow(window);mouse.mc$setLocked(client.currentScreen==null);}
                }
                case 16->release(client);
                default->{}
            }
        }
    }
    private static void release(MinecraftClient client){
        KeyBinding.unpressAll();keys.clear();controlled=false;
        MouseAccess mouse=(MouseAccess)client.mouse;
        mouse.mc$setLocked(false);mouse.mc$setDeltaX(0);mouse.mc$setDeltaY(0);
        mouse.mc$setLeft(false);mouse.mc$setRight(false);mouse.mc$setMiddle(false);mouse.mc$setActive(-1);
    }
    /** 游戏 FBO 展示之前读内部纹理；完全不依赖桌面截图与 DWM。 */
    public static void capture(MinecraftClient client){
        if(!active()||System.nanoTime()<nextFrame)return;nextFrame=System.nanoTime()+50_000_000L;
        Framebuffer source=client.getFramebuffer();
        if(source.fbo<0||source.textureWidth<=0||source.textureHeight<=0)return;
        int read=GL11.glGetInteger(GL30.GL_READ_FRAMEBUFFER_BINDING),draw=GL11.glGetInteger(GL30.GL_DRAW_FRAMEBUFFER_BINDING);
        int pack=GL11.glGetInteger(GL11.GL_PACK_ALIGNMENT),packBuffer=GL11.glGetInteger(GL21.GL_PIXEL_PACK_BUFFER_BINDING);
        int[] viewport=new int[4];GL11.glGetIntegerv(GL11.GL_VIEWPORT,viewport);
        try{
            if(reduced==null){reduced=new SimpleFramebuffer(WIDTH,HEIGHT,false,false);pixels=MemoryUtil.memAlloc(WIDTH*HEIGHT*4);}
            GL30.glBindFramebuffer(GL30.GL_READ_FRAMEBUFFER,source.fbo);GL30.glBindFramebuffer(GL30.GL_DRAW_FRAMEBUFFER,reduced.fbo);
            GL30.glBlitFramebuffer(0,0,source.textureWidth,source.textureHeight,0,0,WIDTH,HEIGHT,GL11.GL_COLOR_BUFFER_BIT,GL11.GL_LINEAR);
            GL30.glBindFramebuffer(GL30.GL_READ_FRAMEBUFFER,reduced.fbo);GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER,0);
            GL11.glPixelStorei(GL11.GL_PACK_ALIGNMENT,1);pixels.clear();
            GL11.glReadPixels(0,0,WIDTH,HEIGHT,GL11.GL_RGBA,GL11.GL_UNSIGNED_BYTE,pixels);
            byte[] rgb=new byte[WIDTH*HEIGHT*3];
            for(int y=0;y<HEIGHT;y++)for(int x=0;x<WIDTH;x++){
                int src=((HEIGHT-1-y)*WIDTH+x)*4,dst=(y*WIDTH+x)*3;
                rgb[dst]=pixels.get(src);rgb[dst+1]=pixels.get(src+1);rgb[dst+2]=pixels.get(src+2);
            }
            int flags=(client.mouse.isCursorLocked()?1:0)|(GLFW.glfwGetWindowAttrib(client.getWindow().getHandle(),GLFW.GLFW_ICONIFIED)!=0?2:0);
            String diagnostic="{\"controlled\":"+controlled+",\"iconified\":"+((flags&2)!=0)+",\"heldKeys\":"+keys.size()
                +",\"screen\":"+(client.currentScreen==null?"false":"true")+",\"inWorld\":"+(client.world!=null);
            if(client.player!=null)diagnostic+=",\"x\":"+client.player.getX()+",\"y\":"+client.player.getY()+",\"z\":"+client.player.getZ()
                +",\"yaw\":"+client.player.getYaw()+",\"pitch\":"+client.player.getPitch()+",\"slot\":"+client.player.getInventory().selectedSlot;
            diagnostic+="}";
            SERVER.offer(new BridgeServer.Frame(++sequence,source.textureWidth,source.textureHeight,flags,rgb,diagnostic));
        }catch(RuntimeException e){LOG.error("Framebuffer export failed",e);SERVER.disconnect();}
        finally{
            GL30.glBindFramebuffer(GL30.GL_READ_FRAMEBUFFER,read);GL30.glBindFramebuffer(GL30.GL_DRAW_FRAMEBUFFER,draw);
            GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER,packBuffer);GL11.glPixelStorei(GL11.GL_PACK_ALIGNMENT,pack);
            GL11.glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
        }
    }
}
