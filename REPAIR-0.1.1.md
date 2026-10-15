# 0.1.1 崩溃修复记录

本机 Taskmgr 10.0.26100.9549 在 2026-10-14 22:26–22:28 出现 7 次访问违规，均为 Taskmgr+0x60DE9，匹配符号 WdcCalculatePowerUsage 的输出指针写入。

0.1.0 使用普通 C++ detour 拦截 RunTimeSettings::GetProcessorDetailsLogicalCount。原函数只读 RCX 对象并写 EAX，保留 EDX/R8；编译出的旧 detour 在 TLS 查找中明确执行 `mov edx, [tls_index]`、`mov r8d, 1`。Taskmgr 的内部优化调用者复用这些寄存器，因此这是已确认的调用约定错误。

0.1.1 移除该全局 Hook，核心计数始终保持原始 32；仅在原生 HeatMap 初始化实例中设置显示格子数，为 NUMA 数据和标志位分配等长存储，通过原生 SwitchChart 选择热力图模式。

回归结果：原生机器码/六个跳板检查通过，汇编探针验证 EDX/R8/R9/R10/R11 保持不变；真实 Taskmgr 加载运行 35 秒无新崩溃，CPU 数前后均 32，停止后 DLL 确认卸载。

限制：这次稳定性回归没有创建热力图（native_cells_created=0），所以尚未验证大网格绘制、任务管理器输入接管与完整退出布局恢复。不得把本次结果标为整个产品已可用。原生源码、EXE 已替换，旧版停止使用。
