# Minecraft 任务管理器原生热力图 · 0.2.0 自动兼容版

固定游戏版本：**Java 1.21.1 + Fabric Loader 0.16.14 + Fabric API 0.116.17+1.21.1**。
系统适配：**Windows x64，按本机 Taskmgr 的微软符号与实际布局自动匹配**。六个 Win10/11 文件样本通过静态检查；目前仅 10.0.26100.9549 完成本机原生渲染与操作验收。详细范围和排错见 [兼容性说明](COMPATIBILITY-0.2.0.md)。

**本机再次验收：实际 Minecraft 内部帧在 MC 最小化时进入真实任务管理器的 2304 个 DirectUI 热力图单元，用户再次确认能操作并看到场景变化。动态匹配、停止恢复、DLL 卸载和核心数保持均有验收记录。跨系统静态检查不能替代跨系统实机测试。**

已有模组不用换，只需更换客户 EXE。本版不修改游戏端取帧、最小化或输入代码。新增独立“检查兼容性”按钮、后台符号下载、离线缓存和可分享的失败报告。

## 下载与仓库结构

直接运行请下载 [v0.2.0 Release](https://github.com/HABAI182/minecraft-native-taskmgr/releases/tag/v0.2.0) 中的完整 ZIP。Git 仓库包含源码、Gradle Wrapper 和验收记录；EXE、分发模组 JAR、调试符号与完整运行包放在 Release 附件中。

## 0.2.0 兼容性改造

首次按本机 Taskmgr 的 GUID/age 下载微软 PDB，使用内置反汇编器定位并核对内部对象布局，缓存后可离线使用。支持独立函数与旧版内联实现、冷热代码分离；失败时在注入前停止并给出具体原因。最终用户仍只运行一个 EXE。

六个样本与七个拒绝测试的完整范围见 [COMPATIBILITY-0.2.0.md](COMPATIBILITY-0.2.0.md)。

## 保留的 0.1.2 白屏修复

- 显式显示原生内层热力图容器。实测初始化时它为隐藏状态，外层页面可见并不能让它自动显示。
- 容器第一次布局的高度可能为零；不再提前放弃，UI 定时器继续重试布局。
- 清除模板文字、缩小继承的字体行高，并清空边距、内边距和边框，避免原生百分比文字撑高格子。
- 切换会话时用 DirectUI 的 DestroyAll 移除旧模板格子，再按 Taskmgr 自己的析构和分配器回收模型。只隐藏旧格子仍会占用 GridLayout 行数，导致新图被挤到下半区。
- 监听原生 CPU 视图更新，打开“性能 → CPU”即可自动切入热力图，已缓存的图表也能接管，不再要求反复切换图形模式。
- 客户端显示“等待布局 / 等待游戏帧 / 已绘制 / 绘制失败”，并在 `%LOCALAPPDATA%\MCNativeTaskmgr\renderer-live.json` 写入每秒一次的布局、亮度、调色板和刷新诊断。

## 0.1.1 崩溃修复（本版保留）

0.1.0 的全局 `GetProcessorDetailsLogicalCount` C++ Hook 不正确。原版函数是短小的内部 leaf 函数，原生调用者还依赖它保留 EDX、R8 等易失寄存器。旧 detour 的 TLS 查找改写这些寄存器，可能破坏调用者已经准备好的参数。

本机记录到 7 次访问违规，均为 `0xC0000005 / Taskmgr.exe+0x60DE9`，位于 `WdcCalculatePowerUsage` 的输出指针写入。旧 detour 的反汇编见 `evidence/crash-leaf-abi.txt`。

0.1.1 完全移除这个全局 getter Hook，也不改全局核心数。现在只扩容原生 `CpuHeatMap` 实例，提供足够长度的 NUMA ID 存储和停泊标志位图，并通过原生 `SwitchChart` 选择热力图。新增汇编回归探针验证真实 getter 的五个易失寄存器保持不变。退出时恢复图表模式并移除 Hook。

不要再运行旧 0.1.0 EXE。当前 `MCNativeTaskmgr.exe` 与 `MCNativeTaskmgr-0.2.0.exe` 为兼容版；旧版 Release 保留作回退。压缩包只含标准文件名 EXE。

## 客户端运行

1. 在启动器中安装并启动 Minecraft Java **1.21.1 + Fabric**，使用 Java 21。Fabric 安装工具：[官方页面](https://fabricmc.net/use/installer/)。
2. 把本包 `mods` 文件夹中的两个 JAR 放进该游戏实例的 `mods` 目录。第三方启动器有独立实例目录，使用对应实例的目录。
3. 运行 `MCNativeTaskmgr.exe` 并允许 UAC，可先点“检查兼容性”。首次匹配非内置版本需要联网下载微软符号。进入游戏世界后点“开始原生改造”，任务管理器未打开时程序会启动它。
4. 任务管理器打开“性能 → CPU”，程序会自动切换为原生热力图。建议横向放宽窗口。原生 GridLayout 会随显示区域拉伸，细小文字在 64×36 分辨率下不可读。
5. 在任务管理器中按 **F8** 接管游戏输入。模组会真正最小化 MC 源窗口。按 **Esc** 可返回游戏或打开暂停菜单；正常使用 WASD、空格、E、鼠标按钮和滚轮。
6. **F8** 释放输入，**F9** 或客户端“停止并恢复”结束会话。任务管理器失去前台焦点时自动释放键鼠。游戏源窗口保持当前最小化状态，需要时从任务栏恢复。

最终用户不需要 Python、Visual Studio、CMake 或手动加载 DLL。DLL 已嵌入 EXE，只在本次会话中提取到用户临时目录；没有修改系统 Taskmgr.exe 文件，没有后台服务或开机启动。

## 选择这条路线的原因

| 路线 | 普通家用 PC | 原生任务管理器渲染 | 最小化与操控 |
|---|---|---|---|
| A：真实核心占用率 | 核心数限制明显，32 个同时显示像素只能约 8×4；时间轴可扩展另一维，但不能任意摆放像素 | 是，显示真实统计图 | 还需要游戏端接入；会额外消耗 CPU |
| B：原生 CPU 热力图数据 Hook | 可创建虚拟显示格子，无须真实拥有对应数量的核心 | 是，使用原有 DirectUI 热力图模板、布局和颜色方法 | 配合 Fabric FBO 导出与输入 API 可支持 |
| C：GetDC/BitBlt 贴图、DWM 或播放器子窗口 | 容易做出原型 | 不满足本项目的原生图表要求 | 仅靠窗口捕获和窗口消息不能保证最小化后继续运行 |

采用 **B + Fabric 游戏端接入**。显示为 64×36、2304 个原生热力图单元，映射到原生主题的六档 CPU 颜色。源画面最多导出 20 FPS，任务管理器 UI 定时器目标为 10 FPS。0.1.2 本机 90 秒实际游戏帧曾达到约 9.19 FPS；新版实测见验收文件。

## 实现链路

`游戏内部 FBO → OpenGL 缩小与读回 → 仅 localhost 的带口令 TCP → 客户端共享内存 → Taskmgr UI 线程 → CpuHeatMap::GetBlockColors / SetBlockData`

每一格均由任务管理器自己的 DirectUI 元素绘制。没有使用 DWM 缩略图、OpenCV 播放窗口、外部画布或向任务管理器 DC 贴位图。

`任务管理器线程消息 / Raw Input → 有界输入队列 → 同一 localhost 连接 → Fabric 客户端线程 → 原版 Keyboard / Mouse / KeyBinding`

模组在内部帧缓冲展示前读回画面。最小化时保留窗口有效尺寸，继续处理 GLFW 事件和游戏渲染队列，跳过不可见窗口的系统 SwapBuffers。连接期间临时关闭失焦暂停和 VSync，断开后恢复这些设置；不写入 options.txt。远程输入调用游戏自身处理函数，不向最小化源窗口发送 SendInput 或 PostMessage。

输入只在任务管理器处于前台且 F8 已接管时转发。焦点丢失、连接断开、控制端心跳超时或输入队列溢出都会释放按键。监听固定在 127.0.0.1，使用随机端口和每次启动生成的 256 位口令；会话发现文件保存在本用户 `%LOCALAPPDATA%\MCNativeTaskmgr\session.json`。

## 源码与构建

```text
MCNativeTaskmgr.exe              内嵌 DLL 的客户 EXE
mods/                           游戏模组和官方 Fabric API
game-mod/src/main/java/          FBO 导出、网络、键鼠及 Mixin
native/src/bridge.cpp            原生热力图 Hook 与 UI 线程生命周期
native/src/injector.cpp          受目标限制的 DLL 加载和卸载
native/src/network.cpp           传输协议及有界共享队列
native/src/compatibility.cpp      本机指令与结构校验
native/src/symbol_cache.cpp       微软符号匹配、下载、缓存
native/src/profile.h             已验收版本的离线回退配置
native/taskmgr-profile.json      版本、哈希、符号身份
native/tests/                   原生指令跳板和寄存器回归探针
evidence/                       已完成的测试结果
```

游戏端构建，要求 JDK 21，Gradle Wrapper 会下载固定的 Gradle 8.10.2：

```powershell
cd game-mod
.\gradlew.bat build
```

输出 `build\libs\mc-native-taskmgr-0.1.0.jar`。模组的版本暂保持 0.1.0，本次兼容性改造发生在原生客户端。

开发运行可使用 `gradlew.bat runClient`，默认在 `game-mod/run` 创建独立演示目录；也可传入 `-Dmcntm.runDir=<路径>` 指定其他测试目录。

原生端构建，安装 Visual Studio 2022 Build Tools 的“使用 C++ 的桌面开发”，包括 Windows SDK、MASM 和 CMake：

```powershell
cd native
.\build.ps1 -BuildDirectory ..\work\native-build
..\work\native-build\Release\MCNativeProfileVerify.exe
..\work\native-build\Release\MCNativeGeometryVerify.exe
```

编译出的 `MCNativeTaskmgr.exe` 可直接分发。使用静态 C/C++ 运行库，用户无须额外安装 VC Runtime。所有绘制与 Hook 在 DllMain 外初始化；UI 定时器和属性变更在任务管理器 UI 线程执行；正常退出先撤销窗口回调与 Hook，再卸载 DLL。

开发者 CLI：

```powershell
.\MCNativeTaskmgr.exe --check-compat
.\MCNativeTaskmgr.exe --start
.\MCNativeTaskmgr.exe --self-test-native-symbols
.\MCNativeTaskmgr.exe --self-test-native
.\MCNativeTaskmgr.exe --self-test-renderer
.\MCNativeTaskmgr.exe --self-test-game-renderer
```

`--check-compat` 可独立检查系统支持情况；`--start` 连接游戏并开始。`--self-test-native-symbols` 强制绕过内置配置，执行 35 秒动态匹配生命周期检查。`--self-test-native` 执行 35 秒 DLL 生命周期检查；`--self-test-renderer` 使用明确标记的六档灰度与移动条纹，显示 90 秒后恢复；`--self-test-game-renderer` 使用正在运行的真实 MC 帧，同样显示 90 秒后恢复。测试都需要在任务管理器打开“性能 → CPU”，报告写入 EXE 同目录 `evidence`。测试画面不会用于正常游戏会话。

## 测试证据

新版：`compatibility-matrix-0.2.0.json`、`native-startup-compat-0.2.0.json`、`native-renderer-test-0.2.0.json`、`native-game-renderer-test-0.2.0.json`、`native-user-acceptance-0.2.0.json`。以下为保留的历史记录。

- `native-game-renderer-test.json`：本版完整的真实 MC → 共享帧 → 原生格子链路，90 秒约 **9.19 FPS**，源帧最小化标志为 2，2304 格，六档亮度，DLL 卸载通过。整个屏幕由原生 GridLayout 使用，最终截图见 `native-game-renderer.jpg`。
- `native-renderer-user-acceptance.json`：用户确认任务管理器内能看到操作引起的场景变化；这是用户验收，不将其冒充为每个按键的自动测量。保存的截图停留在演示说明画面，场景变化由用户另行确认。
- `native-renderer-test.json`：清理旧格子后的 90 秒灰度/移动条纹测试，原生格子从第一行到最后一行均有有效边界；这是灰度测试阶段的构建。最终交付构建另由真实 MC 测试覆盖。
- `native-profile-test.json`：七个 Hook 跳板检查和全局核心数 getter 寄存器回归探针通过。
- `native-geometry-test.json`：初始零高度、零宽度等待、视口适配和调整尺寸的计算检查通过；不以此代替屏幕验收。
- `game-bridge-test.json`：此前游戏端独立测试，IsIconic 为真，导出约 **19 FPS**，移动、视角、Esc 与断线释放通过。本版未改动游戏模组。
- `native-startup-test.json`：此前 0.1.1 的 35 秒生命周期记录，单独不能证明图像显示。

0.2.0 静态矩阵和七项拒绝测试见 `compatibility-matrix-0.2.0.json`；本机新渲染记录为文件名带 `-0.2.0` 的证据。上面的 0.1.2 记录保留为历史验收，不能冒充新版实测。跨系统实机、长时间运行、IME、着色器、Sodium/Iris 尚未覆盖。

## 排错

| 现象 | 处理 |
|---|---|
| 任务管理器崩溃 | 停止会话、关闭旧客户 EXE，重新打开任务管理器。检查自己运行的是本次 0.2.0 兼容版。保留 Application 1000/1001 的异常偏移，不要反复启动旧版 |
| 系统不兼容或符号下载失败 | 点“检查兼容性”，查看 compatibility.json；按兼容性说明排错，不要关闭身份或布局检查 |
| 找不到游戏模组 | 确认游戏实例是 1.21.1 Fabric，并安装两个 JAR；进入世界后重试。启动器不算游戏进程 |
| 帧计数增加，热力图未创建 | 打开“性能 → CPU”；本版会自动创建。查看客户端显示阶段和 renderer-live.json；确认没有运行旧 EXE |
| 帧在增加但图像仍白 | 检查原生格子为 2304、“已绘制”以及 luma/buckets 是否变化；0.1.2 已修复隐藏容器和旧模板占位问题 |
| F8 后仍在暂停菜单 | 在任务管理器中按 Esc 返回游戏；首次演示说明需点击画面中的完成按钮 |
| 鼠标被限制在图内 | F8 释放输入或 F9 停止；离开任务管理器前台也会释放 |
| 字太小、格子太挤 | 放大任务管理器。该版是原生六档蓝色低分辨率显示，细小文字不可读 |
| 中文输入法、游戏模组冲突 | 本版只验收基础按键/菜单；中文 IME、着色器、Sodium/Iris 等组合尚未验收，先用纯 Fabric 测试实例 |
| 网络依赖下载失败 | 确认 Gradle 的 JVM 能访问 Maven 和 Mojang 资源；Java 不一定自动使用终端的 HTTP_PROXY/HTTPS_PROXY |

## 来源

- 原生热力图设计参考：[turtiustrek/taskmanager](https://github.com/turtiustrek/taskmanager)。已归档，只适配较旧系统；本项目重新定位本机符号与 ABI，没有沿用旧偏移。
- 视频中的嵌入播放器对照：[TaskmgrPlayer 源码](https://github.com/svr2kos2/TaskmgrPlayer/blob/master/TaskmgrPlayer/TaskmgrPlayer.cpp)。其 SetParent/OpenCV 方案不作为本项目原生图表路径。
- Hook 库：[MinHook v1.3.4](https://github.com/TsudaKageyu/minhook/tree/v1.3.4)，BSD 2-Clause，完整许可证见 `native/third_party/LICENSE.txt`。
- 游戏 API：[Fabric API](https://github.com/FabricMC/fabric)、[Yarn 1.21.1](https://maven.fabricmc.net/docs/yarn-1.21.1+build.3/net/minecraft/client/MinecraftClient.html)。Fabric API 原始 JAR 保留其许可证文件。

没有在包中分发 Minecraft 游戏程序、原版反编译源码、微软系统 DLL 或 PDB。


## 安全边界与持续检查

这是非微软官方的本机实验工具，会向当前本机 Taskmgr 进程加载桥接 DLL，运行可能触发 UAC；不保证系统更新后的 ABI 兼容。身份、前像、PDB 和布局检查失败时停止，不建议关闭杀毒或系统防护。F9/停止并恢复会撤销会话；异常时关闭客户端并重新打开任务管理器。版本识别与注入限制保留。

[CI](.github/workflows/ci.yml) 只编译原生端、运行不依赖 Taskmgr 的 geometry CTest，并在 JDK 21 构建固定版本 Fabric 模组。CI 不启动游戏、不注入 Taskmgr，也不把六样本静态矩阵冒充跨系统实机验收。Gradle 8.10.2 分发已启用 URL 验证并固定 [官方 SHA256](https://services.gradle.org/distributions/gradle-8.10.2-bin.zip.sha256)；MinHook BSD 和 Zydis/Zycore MIT 许可保留。旧 Release 和 Tag 未修改。
