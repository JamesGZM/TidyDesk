; SPDX-License-Identifier: MIT
#define AppVersion "0.3.1"
[Setup]
AppId={{F33B8E61-F180-40F4-9377-455BBBCE67A1}
AppName=LiteTaskbar
AppVersion={#AppVersion}
AppPublisher=JamesGZM
AppPublisherURL=https://github.com/JamesGZM/LiteTaskbar
DefaultDirName={localappdata}\Programs\LiteTaskbar
DefaultGroupName=LiteTaskbar
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir=package
OutputBaseFilename=LiteTaskbar-{#AppVersion}-Setup
SetupIconFile=app.ico
UninstallDisplayIcon={app}\{#AppVersion}\LiteTaskbar.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
DisableProgramGroupPage=yes
CloseApplications=no
AppMutex=Local\LiteTaskbar.Host.Singleton
RestartApplications=no
LicenseFile=LICENSE
InfoBeforeFile=INSTALL-NOTES.txt

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; Flags: checkedonce
Name: "startup"; Description: "登录 Windows 时自动启动"; Flags: unchecked

[Files]
Source: "build\Release\LiteTaskbar.exe"; DestDir: "{app}\{#AppVersion}"; Flags: ignoreversion
Source: "build\Release\LiteTaskbarTap.dll"; DestDir: "{app}\{#AppVersion}"; Flags: ignoreversion
Source: "build\Release\LiteTaskbarAttach.exe"; DestDir: "{app}\{#AppVersion}"; Flags: ignoreversion
Source: "build\Release\LiteTaskbarStop.exe"; DestDir: "{app}\{#AppVersion}"; Flags: ignoreversion
Source: "LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "INSTALL-NOTES.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\LiteTaskbar"; Filename: "{app}\{#AppVersion}\LiteTaskbar.exe"; WorkingDir: "{app}\{#AppVersion}"
Name: "{group}\卸载 LiteTaskbar"; Filename: "{uninstallexe}"
Name: "{autodesktop}\LiteTaskbar"; Filename: "{app}\{#AppVersion}\LiteTaskbar.exe"; WorkingDir: "{app}\{#AppVersion}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "LiteTaskbar"; ValueData: """{app}\{#AppVersion}\LiteTaskbar.exe"" --background"; Tasks: startup

[Run]
Filename: "{app}\{#AppVersion}\LiteTaskbar.exe"; Description: "启动 LiteTaskbar"; Flags: nowait postinstall skipifsilent runasoriginaluser

[UninstallRun]
Filename: "{app}\{#AppVersion}\LiteTaskbarStop.exe"; Flags: runhidden waituntilterminated; RunOnceId: "StopLiteTaskbar"

[UninstallDelete]
Type: files; Name: "{app}\{#AppVersion}\status.txt"
Type: files; Name: "{app}\{#AppVersion}\events.txt"

[Messages]
ButtonNext=下一步(&N) >
ButtonBack=< 上一步(&B)
ButtonInstall=安装(&I)
ButtonFinish=完成(&F)
ButtonCancel=取消
SelectTasksLabel2=选择需要的快捷方式和启动选项，然后点击“下一步”。
FinishedLabel=LiteTaskbar 已安装到固定目录。之后可以通过桌面或开始菜单快捷方式独立启动。关闭设置窗口会继续在托盘运行。

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and not WizardIsTaskSelected('startup') then
    RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'LiteTaskbar');
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Command: String;
begin
  if CurUninstallStep = usUninstall then begin
    if RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'LiteTaskbar', Command) then
      if Pos(Lowercase(ExpandConstant('{app}')), Lowercase(Command)) > 0 then
        RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'LiteTaskbar');
  end;
end;

