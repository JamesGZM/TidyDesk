# 管理员权限助手（0.4.8 实验版）

在“常规”打开“管理员权限”，点击“应用”，确认 Windows UAC 提示后启用。
设置页显示服务实际状态，不把未授权的草稿当作已启用。取消授权不会自动重试。
以后隐藏、恢复和修复快捷方式箭头复用该授权；任务栏、收纳格和设置窗口仍以普通用户身份运行。
关闭开关并应用会撤销服务注册，不会改动已有收纳文件，也不会撤销当前的箭头外观设置。
卸载会先通过助手恢复箭头，再停用助手；恢复失败时中止卸载并保留备份。

## 持久性和范围

- 使用自动启动的 Windows 服务 `TidyDesk.SystemSettings`，重启电脑后仍有效。
- 可执行文件复制到 Program Files 下专用的 `TidyDesk System Settings` 文件夹。
- 文件夹显式保护 ACL：SYSTEM、Administrators 可写，普通用户只读和执行；已有目录权限不符或是重解析点时拒绝使用。
- 管道仅接受启用时选择的 Windows 用户 SID，拒绝远程客户端；第一实例独占。
- 管道 DACL 不授予普通客户端创建管道实例的权限。服务通过模拟令牌确认用户，不信任进程传入的 SID。
- 客户端核对管道服务端 PID 与 SCM 注册的服务 PID。
- 请求只有版本标识与固定枚举：查询、隐藏箭头、恢复箭头、停用。不接收文件路径、程序命令、注册表路径或任意配置内容。
- 停用后保留小型受保护助手文件和图标资源，避免系统缓存引用失效；没有服务运行或后台计划任务。再次启用需要重新授权。
- 助手按机器安装、绑定一个 Windows 用户。其他用户不能借用授权；箭头覆盖本身仍是 Windows 的机器级设置。
- 不关闭 UAC，不保存管理员凭据，不添加高权限计划任务，不重启 Explorer。

## 验证范围

CI 在临时 Windows 构建机验证服务安装、启动、固定请求、非法枚举拒绝、受限令牌访问、箭头隐藏与恢复、停用和再次启用；常规页仍遵循显式应用。
CI 测试只在 `GITHUB_ACTIONS=true` 且有管理员权限时运行，否则报告跳过。
目标笔记本上的 UAC 取消、重启后自动启动、多用户切换、系统图标缓存和实际占用仍需实机验证。
权限助手与箭头透明资源是两件事：授权复用不等于快捷方式黑块已经实机解决。

实现参考微软文档：
- https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights
- https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-impersonatenamedpipeclient
- https://learn.microsoft.com/en-us/windows-server/security/user-account-control/how-user-account-control-works
