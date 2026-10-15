package dev.mctaskmgr.mixin;
import dev.mctaskmgr.NativeTaskmgrClient;
import net.minecraft.client.Mouse;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
@Mixin(Mouse.class)
public abstract class MouseMixin {
    @Shadow private boolean cursorLocked;
    @Inject(method="lockCursor",at=@At("HEAD"),cancellable=true)
    private void mc$lock(CallbackInfo ci){if(NativeTaskmgrClient.active()){cursorLocked=true;ci.cancel();}}
    @Inject(method="unlockCursor",at=@At("HEAD"),cancellable=true)
    private void mc$unlock(CallbackInfo ci){if(NativeTaskmgrClient.active()){cursorLocked=false;ci.cancel();}}
}
