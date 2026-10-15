package dev.mctaskmgr.mixin;
import dev.mctaskmgr.NativeTaskmgrClient;
import net.minecraft.client.MinecraftClient;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;
@Mixin(MinecraftClient.class)
public abstract class ClientMixin {
    @Inject(method="render",at=@At(value="INVOKE",target="Lnet/minecraft/client/gl/Framebuffer;endWrite()V"))
    private void mc$capture(boolean tick,CallbackInfo ci){NativeTaskmgrClient.capture((MinecraftClient)(Object)this);}
    @Inject(method="isWindowFocused",at=@At("HEAD"),cancellable=true)
    private void mc$focus(CallbackInfoReturnable<Boolean> cir){if(NativeTaskmgrClient.active())cir.setReturnValue(true);}
}
