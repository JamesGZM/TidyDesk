# LiteTaskbar

面向 Windows 11 的轻量任务栏透明工具，采用 MIT 许可证。

## 当前状态

0.2.0 实验版本：已实现原生托盘程序和 XAML 透明后端，目标为 Windows 11 22H2 及以后的 x64 系统。尚未完成实机兼容性和性能验证，不作为稳定版发布。10 MB 内存和低 CPU 是目标，不是实测承诺。

解压全部文件到可写目录，双击 `LiteTaskbar.exe`。单击托盘图标可查看状态或退出恢复；也可以运行 `LiteTaskbar.exe --stop`。不需要管理员权限，不修改注册表，不默认开机启动。`status.txt` 中 `state=transparent` 表示已修改背景元素；仍需肉眼确认效果。

只修改运行时类型精确匹配 `Taskbar.TaskbarBackground` 的元素，不修改图标和交互区域。通过 Windows XAML 诊断 API 将 `LiteTaskbarTap.dll` 加载到当前会话的 Explorer。空闲时等待事件，不使用周期轮询；启动有一次 10 秒超时，退出有一次 5 秒超时。Explorer 重建后事件触发重新连接。

退出时恢复修改前的背景属性；即使主进程异常退出，后端也会尝试恢复。DLL 为避免回调执行中卸载而保留映射，直至 Explorer 退出，不能只用主进程内存衡量总开销。若状态为 `exit_restore_not_confirmed`，请检查桌面是否已恢复。Windows 更新、多显示器或其他 XAML 美化工具可能影响兼容性，连接失败会停止，不持续重试。

## 无需开发环境的云端构建

进入仓库 **Actions → Windows build**，选择成功的运行，在 Artifacts 下载 `LiteTaskbar-experimental-windows-x64`。其中 `LiteTaskbar.exe` 是透明程序，`LiteTaskbarTap.dll` 是后端，二者必须位于同一目录。`LiteTaskbarProbe.exe` 是独立的只读诊断工具，只输出 Windows 版本、任务栏窗口类、相关进程 ID 和自身瞬时工作集，不改变系统设置，不读取窗口标题，不上传数据。

工作集数值仅属于短时诊断进程，不代表最终透明程序的常驻开销。构建产物包含 SHA-256 校验文件；程序尚未签名，也不是正式版本。

开发者可使用带 Windows SDK 的 Visual Studio 2022：

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

诊断工具退出码：0 成功；2 参数错误；3 无法读取系统版本；4 当前会话找不到任务栏。CI 验证编译、命令行行为和 DLL 的 COM 工厂初始化，不验证真实桌面的透明效果。

## 首版目标

- 固定透明的任务栏背景，保留图标及交互。
- 原生 C++ 实现，不依赖浏览器运行时。
- 事件驱动，避免高频轮询；任务栏重建后重新应用。
- 支持退出并恢复默认外观。
- 开机启动由用户选择。
- 使用 GitHub Actions 的 Windows 环境编译，用户无需安装开发工具。
- 不收集遥测，不添加运行时网络请求。

## 性能验收计划

空闲 CPU 接近 0%、进程工作集尽量低于 10 MB 是优化目标，尚无测量结果。测试必须同时观察程序、Explorer 和 DWM，防止把开销转移到系统进程而遗漏。

在同一台机器上比较开启前、开启后、退出后三组情况：空闲 60 秒、切换窗口、最大化/还原、打开开始菜单、多显示器变化、睡眠唤醒以及 Explorer 重启。记录 Windows 完整版本、CPU、内存、测量方法和可见卡顿。

## 实现约束

Windows 11 的现代任务栏基于 XAML，旧式窗口透明接口不足以保证背景透明。首版需先验证适用的实现方式，不能通过让整个任务栏（含图标）变淡来代替背景透明。

项目独立实现；不复制或重新许可 TranslucentTB 的 GPL 源码。引入第三方依赖前记录其许可证，并确保与项目的 MIT 发布方式兼容。

## License

[MIT](LICENSE) © 2026 JamesGZM
