# TidyDesk · 整洁桌面

JamesGZM 的轻量 Windows 桌面美化与整理工具，原名 LiteTaskbar。原生 C++、MIT 开源、无浏览器运行时、无遥测。本项目与其他同名 TidyDesk 软件无关。

## 0.4.0 实验版

- 五页原生设置：概览、任务栏、桌面收纳、图标、常规，系统深浅色、高 DPI、自动保存。
- 任务栏背景透明度及“桌面存在最大化窗口”规则；普通前台窗口不会抵消后面的最大化窗口。
- 独立桌面收纳进程：分类文件夹、拖入拖出、排序、折叠、锁定、缩放、图标大小与背景浓度。
- 文件实际移动；拖入 `.exe` 创建快捷方式，不移动安装文件。重名、取消和失败使用 Windows Shell 文件操作处理。
- 隐藏系统快捷方式箭头：按需提权，备份并恢复原注册表值，刷新需用户主动操作。

首次打开“桌面收纳”，新建分类文件夹或绑定已有文件夹。创建文件夹可用系统选择窗口中的“新建文件夹”。右键框标题调整样式；右键项目可移回桌面。移除框只删除布局。

## 安装与迁移

在 [Actions](https://github.com/JamesGZM/TidyDesk/actions) 成功构建的 Artifacts 下载安装包。实验构建不等于稳定发行版。

安装器沿用 LiteTaskbar 的 AppId；旧安装保留原目录，新安装默认 `%LOCALAPPDATA%\Programs\TidyDesk`。启动前须退出旧版托盘进程，安装器不会强行重启 Explorer。版本子目录避免覆盖已加载的 DLL。旧偏好保留在 `HKCU\Software\LiteTaskbar`，首次启动迁入 `HKCU\Software\TidyDesk`。自启项为带引号的程序路径加 `--background`，升级保留选择。

分类文件默认位于用户文档的 `TidyDesk Collections`，布局在 `%LOCALAPPDATA%\TidyDesk\layout.ini`，带原子替换和 `layout.bak` 备份。退出、卸载均不删除分类文件和配置。关闭设置继续托盘运行；从托盘退出恢复任务栏并关闭收纳进程。

图标覆盖影响系统快捷方式，可能需要注销后重新登录。不要删除 `blank.ico`；卸载时先恢复覆盖设置。若其他程序已修改覆盖项，恢复会停止并保留备份。未自动重启 Explorer。

## 架构和边界

`TidyDesk.exe` 管理托盘、设置和任务栏规则；`TidyDeskDesktop.exe` 仅在启用收纳时运行，使用原生 ListView、OLE 拖拽、目录通知和异步图标加载，图标缓存上限 512/框。桌面模块不注入 Explorer。任务栏仍使用独立连接器和 XAML 后端。

桌面层依赖 Windows Shell 窗口结构，需在具体 Windows 版本验证。任务栏 XAML 重连仍有兼容性风险；失败显示在设置页，不重复弹窗。性能目标（新增桌面进程空闲平均 CPU ≤0.1%、私有内存 ≤50 MiB）尚需实测，不能以主进程内存替代总开销。详见 [VALIDATION.md](VALIDATION.md)。

## 开发与开源参考

GitHub Actions 使用 Windows 2022、MSVC C++17、Windows SDK 和 Inno Setup 6，编译、测试并生成安装包。用户不需要开发环境。

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

交互参考 [Desktop Frames+](https://github.com/limbo666/DesktopFramesPlus) 和 [NoFences](https://github.com/Twometer/NoFences)，本版桌面模块独立实现，未复制第三方源码。历史 LiteTaskbar 提交及标签保留。

MIT © 2026 JamesGZM
