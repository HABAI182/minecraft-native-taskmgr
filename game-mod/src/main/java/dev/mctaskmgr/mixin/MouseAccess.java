package dev.mctaskmgr.mixin;
import net.minecraft.client.Mouse;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;
import org.spongepowered.asm.mixin.gen.Invoker;
@Mixin(Mouse.class)
public interface MouseAccess {
    @Invoker("onMouseButton") void mc$button(long window,int button,int action,int mods);
    @Invoker("onMouseScroll") void mc$scroll(long window,double horizontal,double vertical);
    @Invoker("onCursorPos") void mc$cursor(long window,double x,double y);
    @Accessor("cursorLocked") void mc$setLocked(boolean value);
    @Accessor("cursorDeltaX") double mc$getDeltaX();
    @Accessor("cursorDeltaY") double mc$getDeltaY();
    @Accessor("cursorDeltaX") void mc$setDeltaX(double value);
    @Accessor("cursorDeltaY") void mc$setDeltaY(double value);
    @Accessor("leftButtonClicked") void mc$setLeft(boolean value);
    @Accessor("rightButtonClicked") void mc$setRight(boolean value);
    @Accessor("middleButtonClicked") void mc$setMiddle(boolean value);
    @Accessor("activeButton") void mc$setActive(int value);
}
