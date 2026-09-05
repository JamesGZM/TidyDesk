# LiteTaskbar

原生 C++ 的 Windows 11 任务栏透明工具，MIT 开源，无浏览器运行时、无遥测、无运行时网络请求。

## 使用

从 [Releases](https://github.com/JamesGZM/LiteTaskbar/releases) 下载实验版 ZIP，解压全部文件到固定目录，双击 `LiteTaskbar.exe`。需要 Windows 11 现代 XAML 任务栏和 x64 系统；不需要管理员权限。

0.3.0 新增独立应用和托盘图标，以及原生设置窗口：

- 背景不透明度 0–100%；0% 全透明，100% 恢复系统背景属性。
- 可选“当前窗口最大化时使用系统默认背景”，只按当前前台窗口判断。
- 可选“登录 Windows 时自动启动”，初始关闭。
- 点击“应用”保存；关闭设置窗口后继续在托盘运行。
- 点击托盘图标，或再次双击程序，重新打开设置。Windows 可能将图标收进右下角 ∧ 隐藏区。
- 选择“退出并恢复”，或双击 `LiteTaskbarStop.exe`，退出并恢复原来的背景。

自启使用当前用户 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` 中的 `LiteTaskbar` 项，命令为带引号的完整程序路径和 `--background`。下次登录静默启动，取消勾选并应用可移除。开启后不要移动程序目录；移动后重新应用自启设置。Windows 任务管理器中禁用启动项也会影响自启。

偏好保存在 `HKCU\Software\LiteTaskbar`。卸载前取消自启并退出，再删除解压目录；此偏好键可手动删除。

## 资源占用与实现

设置页使用按需创建的 Win32 控件，关闭后销毁窗口和字体。主程序不加载 XAML 运行时；`LiteTaskbarAttach.exe` 仅在连接时短暂运行。无定时轮询；最大化规则使用系统事件通知和一次性防抖。

后端通过 Windows XAML 诊断接口进入 Explorer，仅修改精确匹配 `Taskbar.TaskbarBackground` 的元素，保留图标和交互。透明度变化在 XAML UI 线程执行，100% 和退出时恢复保存的局部属性。DLL 映射保留到 Explorer 结束以避免回调执行时卸载，因此主进程内存不等于总开销。

0.2.1 的历史短时测试主进程工作集约 9 MiB；这不是 0.3.0 或所有设备的承诺。最新验证范围见 [VALIDATION.md](VALIDATION.md)。未与 TranslucentTB 做严格同条件对照。

## 实验版限制

Windows 任务栏内部结构可能变化。多显示器、睡眠唤醒、Explorer 重启和长时间运行仍需更多验证。不要同时运行多个任务栏美化工具。本版没有实现模糊、亚克力或按应用匹配等高级规则。

程序目录内 `status.txt` 的 `custom_background` 表示后台已处理背景元素；`system_default` 表示使用系统背景；`restored` 表示退出恢复完成。状态不能替代肉眼确认。若显示 `exit_restore_not_confirmed`，请检查桌面。连接失败后停止，不持续重试。

## 云端构建

GitHub Actions → Windows build 自动在云端编译，用户无需开发环境。成功运行的 Artifacts 包含程序、后端、连接器、退出工具和 SHA-256 校验表。

开发者本地需要 Visual Studio 2022 C++ 和 Windows SDK：

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

CI 验证编译、命令行和 DLL COM 初始化，不验证真实桌面效果。`LiteTaskbarProbe.exe` 是只读诊断工具。程序尚未签名。

项目独立实现，参考 TranslucentTB 的功能方向，不复制其 GPL 源码。

## License

[MIT](LICENSE) © 2026 JamesGZM
