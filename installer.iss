; SPDX-License-Identifier: MIT
#define AppVersion "0.4.0"
[Setup]
AppId={{F33B8E61-F180-40F4-9377-455BBBCE67A1}
AppName=TidyDesk
AppVersion={#AppVersion}
AppPublisher=JamesGZM
AppPublisherURL=https://github.com/JamesGZM/TidyDesk
DefaultDirName={localappdata}\Programs\TidyDesk
DefaultGroupName=TidyDesk
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir=package
OutputBaseFilename=TidyDesk-{#AppVersion}-Setup
SetupIconFile=app.ico
UninstallDisplayIcon={app}\{#AppVersion}\TidyDesk.exe
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
Source: "build\Release\TidyDesk.exe"; DestDir: "{app}\{#AppVersion}"; Flags: ignoreversion
Source: "build\Release\TidyDeskTap.dll"; DestDir: "{app}\{#AppVersion}"; Flags: ignoreversion
Source: "build\Release\TidyDeskAttach.exe"; DestDir: "{app}\{#AppVersion}"; Flags: ignoreversion
Source: "build\Release\TidyDeskStop.exe"; DestDir: "{app}\{#AppVersion}"; Flags: ignoreversion
Source: "build\Release\TidyDeskDesktop.exe"; DestDir: "{app}\{#AppVersion}"; Flags: ignoreversion
Source: "build\Release\TidyDeskIcons.exe"; DestDir: "{app}\{#AppVersion}"; Flags: ignoreversion
Source: "blank.ico"; DestDir: "{app}"; Flags: ignoreversion uninsneveruninstall
Source: "LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "INSTALL-NOTES.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\TidyDesk"; Filename: "{app}\{#AppVersion}\TidyDesk.exe"; WorkingDir: "{app}\{#AppVersion}"
Name: "{group}\卸载 TidyDesk"; Filename: "{uninstallexe}"
Name: "{autodesktop}\TidyDesk"; Filename: "{app}\{#AppVersion}\TidyDesk.exe"; WorkingDir: "{app}\{#AppVersion}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppVersion}\TidyDesk.exe"; Description: "启动 TidyDesk"; Flags: nowait postinstall skipifsilent runasoriginaluser

[UninstallRun]
Filename: "{app}\{#AppVersion}\TidyDeskStop.exe"; Flags: runhidden waituntilterminated; RunOnceId: "StopLiteTaskbar"

[Code]
const
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';
var
  PreviousStartup: Boolean;
  StartupChosen: Boolean;

function InitializeSetup(): Boolean;
var Command: String;
begin
  PreviousStartup := RegQueryStringValue(HKCU, RunKey, 'TidyDesk', Command) and (Command <> '');
  if not PreviousStartup then
    PreviousStartup := RegQueryStringValue(HKCU, RunKey, 'LiteTaskbar', Command) and (Pos('litetaskbar.exe', Lowercase(Command)) > 0);
  Result := True;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID = wpSelectTasks) and not StartupChosen then begin
    if PreviousStartup then WizardSelectTasks('startup');
    StartupChosen := True;
  end;
end;

procedure RemoveOldShortcut(const Path: String);
var Shell, Link: Variant;
begin
  if not FileExists(Path) then Exit;
  try
    Shell := CreateOleObject('WScript.Shell');
    Link := Shell.CreateShortcut(Path);
    if Pos(Lowercase(AddBackslash(ExpandConstant('{app}'))), Lowercase(Link.TargetPath)) = 1 then DeleteFile(Path);
  except
    Log('Old shortcut could not be inspected; preserved.');
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var Command, NewCommand: String; Success: Boolean;
begin
  if CurStep = ssPostInstall then begin
    NewCommand := '"' + ExpandConstant('{app}\{#AppVersion}\TidyDesk.exe') + '" --background';
    if WizardIsTaskSelected('startup') then
      Success := RegWriteStringValue(HKCU, RunKey, 'TidyDesk', NewCommand)
    else begin
      Success := True;
      if RegQueryStringValue(HKCU, RunKey, 'TidyDesk', Command) then
        if Pos(Lowercase(ExpandConstant('{app}')), Lowercase(Command)) > 0 then Success := RegDeleteValue(HKCU, RunKey, 'TidyDesk');
    end;
    if Success and RegQueryStringValue(HKCU, RunKey, 'LiteTaskbar', Command) then
      if Pos(Lowercase(ExpandConstant('{app}')), Lowercase(Command)) > 0 then RegDeleteValue(HKCU, RunKey, 'LiteTaskbar');
    RemoveOldShortcut(ExpandConstant('{autodesktop}\LiteTaskbar.lnk'));
    RemoveOldShortcut(ExpandConstant('{userprograms}\LiteTaskbar\LiteTaskbar.lnk'));
    RemoveOldShortcut(ExpandConstant('{userprograms}\LiteTaskbar\卸载 LiteTaskbar.lnk'));
  end;
end;

function InitializeUninstall(): Boolean;
var Saved: Cardinal; Code: Integer;
begin
  Result := True;
  if RegQueryDWordValue(HKLM64, 'SOFTWARE\TidyDesk\ArrowBackup', 'Saved', Saved) and (Saved = 1) then begin
    Result := ShellExec('runas', ExpandConstant('{app}\{#AppVersion}\TidyDeskIcons.exe'), '--restore', '', SW_SHOWNORMAL, ewWaitUntilTerminated, Code);
    Result := Result and (Code = 0);
    if not Result then MsgBox('图标设置尚未恢复，卸载已取消。请先在图标页恢复原设置。', mbInformation, MB_OK);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var Command: String;
begin
  if CurUninstallStep = usUninstall then begin
    if RegQueryStringValue(HKCU, RunKey, 'TidyDesk', Command) then
      if Pos(Lowercase(ExpandConstant('{app}')), Lowercase(Command)) > 0 then RegDeleteValue(HKCU, RunKey, 'TidyDesk');
  end;
  if CurUninstallStep = usPostUninstall then DeleteFile(ExpandConstant('{app}\blank.ico'));
end;
