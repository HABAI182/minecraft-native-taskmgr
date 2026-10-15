package dev.mctaskmgr.mixin;
import dev.mctaskmgr.NativeTaskmgrClient;
import net.minecraft.client.util.Window;
import com.mojang.blaze3d.systems.RenderSystem;
import org.lwjgl.glfw.GLFW;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
@Mixin(Window.class)
public abstract class WindowMixin {
    @Shadow private long handle;
    @Inject(method="onWindowSizeChanged",at=@At("HEAD"),cancellable=true)
    private void mc$size(long window,int width,int height,CallbackInfo ci){
        if(NativeTaskmgrClient.active()&&(width<=0||height<=0))ci.cancel();
    }
    @Inject(method="swapBuffers",at=@At("HEAD"),cancellable=true)
    private void mc$swap(CallbackInfo ci){
        if(NativeTaskmgrClient.active()&&GLFW.glfwGetWindowAttrib(handle,GLFW.GLFW_ICONIFIED)!=0){
            // 最小化时跳过系统 SwapBuffers，保持 GLFW 事件与渲染队列处理。
            RenderSystem.replayQueue();GLFW.glfwPollEvents();ci.cancel();
        }
    }
}
