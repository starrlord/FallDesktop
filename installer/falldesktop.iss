; Inno Setup 6 script for the Fall Desktop installer.
;   iscc /DAppVersion=1.2.3 /DSourceExe=..\build\Release\falldesktop.exe installer\falldesktop.iss
; Output: dist\FallDesktop-<version>-setup.exe

#define AppName "Fall Desktop"
; The internal name, used by the exe, its settings key (HKCU\Software\falldesktop)
; and its Run value.
#define AppKey "falldesktop"
#define AppExe "falldesktop.exe"
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef SourceExe
  #define SourceExe "..\build\Release\falldesktop.exe"
#endif

[Setup]
; Fall Desktop's own id: never reuse another app's, or setup would treat it as an upgrade of that app.
AppId={{63A5EF46-B6AB-43FE-BCEA-277F02922156}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Joshua Starr
AppPublisherURL=https://github.com/starrlord/FallDesktop
AppSupportURL=https://github.com/starrlord/FallDesktop/issues
AppUpdatesURL=https://github.com/starrlord/FallDesktop/releases
VersionInfoVersion={#AppVersion}
VersionInfoProductName={#AppName}
VersionInfoDescription={#AppName} setup
; Per-user install by default (no admin prompt); "all users" stays available.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
DefaultDirName={autopf}\{#AppName}
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir=..\dist
OutputBaseFilename=FallDesktop-{#AppVersion}-setup
SetupIconFile=..\res\falldesktop.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
CloseApplications=yes
RestartApplications=no
SetupLogging=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "startup"; Description: "Start {#AppName} when I sign in to Windows"; GroupDescription: "Options:"; Flags: unchecked

[Files]
Source: "{#SourceExe}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"; Comment: "Autumn leaves, pumpkins, scarecrows and cobwebs on your desktop"

[Registry]
; Same value the app's own "Start with Windows" switch reads and writes.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "{#AppKey}"; ValueData: """{app}\{#AppExe}"""; Tasks: startup

[Run]
Filename: "{app}\{#AppExe}"; Description: "Let the leaves fall now"; Flags: nowait postinstall skipifsilent

[Code]
const
  AppWindowClass = 'falldesktopApp';
  WM_CLOSE = $0010;

{ Ask a running copy to exit (it saves its settings on the way out). }
procedure CloseRunningApp();
var
  Wnd: HWND;
  Tries: Integer;
begin
  Wnd := FindWindowByClassName(AppWindowClass);
  if Wnd = 0 then
    Exit;
  PostMessage(Wnd, WM_CLOSE, 0, 0);
  Tries := 0;
  while (FindWindowByClassName(AppWindowClass) <> 0) and (Tries < 50) do
  begin
    Sleep(100);
    Tries := Tries + 1;
  end;
  Sleep(300);  { let the process finish exiting before files are replaced }
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  CloseRunningApp();
  Result := '';
end;

function InitializeUninstall(): Boolean;
begin
  CloseRunningApp();
  Result := True;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
  begin
    { Also covers "Start with Windows" enabled from inside the app. }
    RegDeleteValue(HKEY_CURRENT_USER, 'Software\Microsoft\Windows\CurrentVersion\Run', '{#AppKey}');
    RegDeleteValue(HKEY_CURRENT_USER, 'Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run', '{#AppKey}');
    RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, 'Software\{#AppKey}');
  end;
end;
