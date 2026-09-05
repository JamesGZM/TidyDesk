# TidyDesk · 整洁桌面

JamesGZM 的轻量 Windows 桌面美化与整理工具，原名 LiteTaskbar。原生 C++、MIT 开源、无浏览器运行时、无遥测。本项目与其他同名 TidyDesk 软件无关。

## 0.4.5 实验版

- 四页原生设置：任务栏、桌面收纳、图标、常规，系统深浅色、高 DPI、显式应用。修改保持为草稿，点击“应用”才保存和执行。
- 任务栏背景透明度及“桌面存在最大化窗口”规则；普通前台窗口不会抵消后面的最大化窗口。
- 独立桌面收纳进程：分类文件夹、拖入拖出、排序、展开/收起、锁定、四边缩放与固定图标大小。
- 文件实际移动；拖入 `.exe` 创建快捷方式，不移动安装文件。重名、取消和失败使用 Windows Shell 文件操作处理。
- 隐藏系统快捷方式箭头：按需提权，备份并恢复原注册表值，刷新需用户主动操作。

桌面空白处右键 → 新建收纳框（Windows 11 可能在“显示更多选项”内）。设置页只启停模块和管理已有收纳框。拖边缘调整大小，拖标题移动；图标不随框缩小。滚轮浏览，无滚动条和数量提示。右上角展开/收起，展开时点击外部或 Esc 返回原尺寸。拖入停留 650ms 展开，拖出时撤掉遮罩。框内空白处右键管理；文件上右键使用 Windows 文件菜单。解散只需一次确认：内容移回桌面后删除对应空文件夹；取消、冲突或占用导致未完成时，保留收纳框与剩余文件。

0.4.3：新建收纳框直接生成默认名称，之后可用右键或 F2 重命名；移除收纳页运行状态。修正主题下拉框的高度与深浅色绘制。箭头透明资源增加显式透明遮罩和新的缓存路径；已启用隐藏的用户可在图标页点击“修复并刷新图标”。不会自动重启 Explorer；实际缓存效果仍须在目标桌面验证。

0.4.4：收纳框改用桌面持有的独立窗口，补上创建失败反馈、列表刷新和管理页解散。调整显示失败时的布局保存，保留尚未显示的收纳框。参考 NoFences 的桌面 owner-window 模式，独立实现，未复制其代码。快捷方式黑块仍需目标机器重新加载 Explorer 后验证，普通刷新不保证清除缓存。

0.4.5：解散改为单次确认，移回桌面并删除空目录；修复未初始化 COM 的文件操作和空目录误报。收纳图标改用原生颜色及透明度转换，隐藏快捷方式显示后缀；补充边框右键、重命名布局和首次显示重绘，管理列表显示名称与路径。

## 安装与迁移

在 [Actions](https://github.com/JamesGZM/TidyDesk/actions) 成功构建的 Artifacts 下载安装包。实验构建不等于稳定发行版。

安装器沿用 LiteTaskbar 的 AppId；旧安装保留原目录，新安装默认 `%LOCALAPPDATA%\Programs\TidyDesk`。启动前须退出旧版托盘进程，安装器不会强行重启 Explorer。版本子目录避免覆盖已加载的 DLL。旧偏好保留在 `HKCU\Software\LiteTaskbar`，首次启动迁入 `HKCU\Software\TidyDesk`。自启项为带引号的程序路径加 `--background`，升级保留选择。

分类文件默认位于用户文档的 `TidyDesk Collections`，布局在 `%LOCALAPPDATA%\TidyDesk\layout.ini`，带原子替换和 `layout.bak` 备份。退出、卸载均不删除分类文件和配置。关闭设置继续托盘运行；从托盘退出恢复任务栏并关闭收纳进程。

图标覆盖影响系统快捷方式，可能需要注销后重新登录。不要删除 `blank.ico`；卸载时先恢复覆盖设置。若其他程序已修改覆盖项，恢复会停止并保留备份。未自动重启 Explorer。

## 架构和边界

`TidyDesk.exe` 管理托盘、设置和任务栏规则；`TidyDeskDesktop.exe` 仅在启用收纳时运行，使用按事件重绘的透明圆角画布、OLE 拖拽、目录通知和异步图标加载，图标缓存上限 512/框。桌面模块不注入 Explorer。任务栏仍使用独立连接器和 XAML 后端。

桌面层依赖 Windows Shell 窗口结构，需在具体 Windows 版本验证。任务栏 XAML 重连仍有兼容性风险；失败显示“应用失败”，可重试；内部诊断不作为普通用户操作入口。性能目标（新增桌面进程空闲平均 CPU ≤0.1%、私有内存 ≤50 MiB）尚需实测，不能以主进程内存替代总开销。详见 [VALIDATION.md](VALIDATION.md)。

## 开发与开源参考

GitHub Actions 使用 Windows 2022、MSVC C++17、Windows SDK 和 Inno Setup 6，编译、测试并生成安装包。用户不需要开发环境。

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

交互参考 [Desktop Frames+](https://github.com/limbo666/DesktopFramesPlus) 和 [NoFences](https://github.com/Twometer/NoFences)，本版桌面模块独立实现，未复制第三方源码。历史 LiteTaskbar 提交及标签保留。

MIT © 2026 JamesGZM

0.4.2 修正：最大化规则使用 Windows 实际最大化状态，不把手动铺满工作区的普通窗口算作最大化。快捷方式箭头采用开关 + 应用；只有系统操作成功后才保存后续草稿。实机视觉和系统图标缓存刷新仍须按具体机器验证。
