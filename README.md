# LiteTaskbar

面向 Windows 11 的轻量任务栏透明工具，采用 MIT 许可证。

## 当前状态

项目开发阶段。目前提供原生只读诊断程序 `LiteTaskbarProbe.exe` 和 GitHub Actions 云端构建，**尚未实现任务栏透明功能**。Windows 11 25H2 的透明效果、兼容性和性能均未验证。请勿将设计目标视为已实现的功能或性能承诺。

## 无需开发环境的云端构建

进入仓库 **Actions → Windows build**，选择成功的运行，在 Artifacts 下载 `LiteTaskbar-probe-windows-x64`。解压后在终端运行 `LiteTaskbarProbe.exe`；它只输出 Windows 版本、任务栏窗口类、相关进程 ID 和自身瞬时工作集，不改变系统设置，不读取窗口标题，不上传数据。

工作集数值仅属于短时诊断进程，不代表最终透明程序的常驻开销。构建产物包含 SHA-256 校验文件；程序尚未签名，也不是正式版本。

开发者可使用带 Windows SDK 的 Visual Studio 2022：

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

退出码：0 成功；2 参数错误；3 无法读取系统版本；4 当前会话找不到任务栏。CI 只验证构建和命令行行为，不验证真实桌面的透明效果。

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
