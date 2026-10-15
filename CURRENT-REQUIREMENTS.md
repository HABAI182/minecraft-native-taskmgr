# 本次真正的目标与验收

用户已澄清：需要任务管理器自身的图表/热力图渲染 Minecraft，而非游戏窗口监看、DWM 缩略图、贴图播放器或额外覆盖窗口。原 Minecraft 窗口最小化时仍必须能持续生成新画面并通过任务管理器操作。

首版游戏平台已由用户确认：Minecraft Java 1.21.1 + Fabric。

正确的数据流应为：

```text
Minecraft 游戏线程与独立帧缓冲
  → 低分辨率游戏帧
  → 本地帧/输入通道
  → Taskmgr 原生图表的数据或原生绘制路径
  → 任务管理器接收键鼠并回传游戏端
```

必须同时验证：

1. MC 窗口确实处于最小化状态，源 HWND 的 IsIconic 为 true。
2. 任务管理器获得焦点，WASD、视角、攻击/使用、滚轮和基础菜单输入能控制游戏。
3. 显示调用进入 Taskmgr 的原生图表/热力图路径；不以 DWM 图像关系、外部播放器窗口或普通位图贴图冒充。
4. 最小化后连续出现新游戏帧，至少 5–10 FPS；转向或移动后任务管理器里的画面随输入改变。
5. 退出后原生图表、输入处理与游戏状态可以恢复，不能留下卡键。

此前 outputs/mc_taskmgr 和 outputs/mc_internal 的原型不满足这一目标，不能作为本次完成交付。

参考视频调研：已读取三段公开简介。第二个视频明确引用 svr2kos2/TaskmgrPlayer，其源码将 OpenCV 的播放器窗口 SetParent 到 CPU 图控件，并没有用任务管理器原生图表引擎生成图像。视频视觉效果与用户明确提出的原生渲染要求需分别判断。

资料：
- https://github.com/svr2kos2/TaskmgrPlayer/blob/master/TaskmgrPlayer/TaskmgrPlayer.cpp
- https://github.com/turtiustrek/taskmanager
- https://maven.fabricmc.net/docs/yarn-1.21.1+build.3/net/minecraft/client/MinecraftClient.html

本机需要适配的 Taskmgr.exe 文件版本为 10.0.26100.9549，操作系统 build 为 26300。两者不能混为同一个版本号，也不能套用旧 19041/22000 的函数偏移。
