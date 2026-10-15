package dev.mctaskmgr.mixin;
import net.minecraft.client.Keyboard;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Invoker;
@Mixin(Keyboard.class)
public interface KeyboardAccess { @Invoker("onChar") void mc$char(long window,int codePoint,int modifiers); }
