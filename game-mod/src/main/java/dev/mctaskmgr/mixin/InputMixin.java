package dev.mctaskmgr.mixin;
import dev.mctaskmgr.NativeTaskmgrClient;
import net.minecraft.client.util.InputUtil;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;
@Mixin(InputUtil.class)
public abstract class InputMixin {
    @Inject(method="isKeyPressed",at=@At("HEAD"),cancellable=true)
    private static void mc$poll(long window,int code,CallbackInfoReturnable<Boolean> cir){
        if(NativeTaskmgrClient.active())cir.setReturnValue(NativeTaskmgrClient.keyDown(code));
    }
}
