#ifndef MyAppVersion
  #define MyAppVersion "1.6.1"
#endif

[Setup]
#ifdef InstallerSmokeTest
AppId=CamCord.PackagingSmokeTest
CreateUninstallRegKey=no
UsePreviousAppDir=no
UsePreviousGroup=no
UsePreviousTasks=no
CloseApplications=no
#else
AppId={{4C7625A7-3B5A-4CAA-92E2-BA566D67E538}
CloseApplications=yes
#endif
AppName=CamCord
AppVersion={#MyAppVersion}
AppPublisher=CamCord
DefaultDirName={localappdata}\Programs\CamCord
DefaultGroupName=CamCord
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
OutputDir=..\dist
OutputBaseFilename=CamCord-Setup
Compression=lzma2
SolidCompression=yes
ArchiveExtraction=full
WizardStyle=modern
SetupIconFile=..\assets\branding\camcord.ico
UninstallDisplayIcon={app}\CamCord.exe
RestartApplications=no
SetupLogging=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
Source: "..\bin\Release\CamCord.exe"; DestDir: "{app}"; Flags: ignoreversion
; Downloaded directly from upstream on the recipient's PC, never embedded in Setup.
Source: "{tmp}\ffmpeg-extracted\ffmpeg-9.0.2-essentials_build\bin\ffmpeg.exe"; DestDir: "{app}"; ExternalSize: 105423872; Flags: external ignoreversion
Source: "..\bin\Release\ui\*"; DestDir: "{app}\ui"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\bin\Release\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\bin\Release\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\bin\Release\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\bin\Release\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\third_party\webview2-runtime\MicrosoftEdgeWebview2Setup.exe"; Flags: dontcopy

[Icons]
#ifndef InstallerSmokeTest
Name: "{group}\CamCord"; Filename: "{app}\CamCord.exe"
Name: "{autodesktop}\CamCord"; Filename: "{app}\CamCord.exe"; Tasks: desktopicon
#endif

[Run]
#ifndef InstallerSmokeTest
Filename: "{app}\CamCord.exe"; Description: "Open CamCord"; Flags: nowait postinstall skipifsilent
Filename: "{app}\CamCord.exe"; Flags: nowait skipifnotsilent; Check: IsAppUpdate
#endif

[Code]
const
  WebView2Key = 'Software\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}';
  FFmpegUrl = 'https://github.com/GyanD/codexffmpeg/releases/download/9.0.2/ffmpeg-9.0.2-essentials_build.zip';
  FFmpegArchiveHash = '60f467265b1e312373dbcd92200c2618a74850f98d3d078e94296bb3fa2047ba';
  FFmpegBinaryHash = '3256173f3f8bffd7df12227c68adf68025edb1832273a9530688a7bb1ed8edec';
#ifdef InstallerSmokeTest
  #ifndef StartupTestKey
    #define StartupTestKey "Software\CamCord\Tests\InstallerSmokeStartup"
  #endif
  StartupKey = '{#StartupTestKey}';
#else
  StartupKey = 'Software\Microsoft\Windows\CurrentVersion\Run';
#endif

var
  DownloadPage: TDownloadWizardPage;
  ExtractionPage: TExtractionWizardPage;

function IsAppUpdate: Boolean;
begin
  Result := ExpandConstant('{param:CAMCORDUPDATE|0}') = '1';
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Command: String;
begin
  if (CurStep = ssPostInstall) and RegQueryStringValue(HKCU, StartupKey, 'CamCord', Command) then
    RegWriteStringValue(HKCU, StartupKey, 'CamCord', '"' + ExpandConstant('{app}\CamCord.exe') + '" --startup');
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Command, Expected: String;
begin
  Expected := '"' + ExpandConstant('{app}\CamCord.exe') + '" --startup';
  if (CurUninstallStep = usPostUninstall) and RegQueryStringValue(HKCU, StartupKey, 'CamCord', Command) and
    (CompareText(Command, Expected) = 0) then RegDeleteValue(HKCU, StartupKey, 'CamCord');
end;

function RuntimeVersionExists(Root: Integer): Boolean;
var
  Version: String;
begin
  Result := RegQueryStringValue(Root, WebView2Key, 'pv', Version) and
    (Version <> '') and (Version <> '0.0.0.0');
end;

function WebView2Installed: Boolean;
begin
  Result := RuntimeVersionExists(HKLM32) or RuntimeVersionExists(HKCU32) or RuntimeVersionExists(HKCU64);
end;

procedure InitializeWizard;
begin
  WizardForm.WelcomeLabel2.Caption := WizardForm.WelcomeLabel2.Caption + #13#10#13#10 +
    'Internet is required. Setup downloads the FFmpeg recording engine (about 110 MB) directly from its upstream distributor and installs Microsoft Edge WebView2 if needed.';
  DownloadPage := CreateDownloadPage('Downloading recording engine', 'Downloading FFmpeg from its upstream distributor. Please keep your internet connection active.', nil);
  DownloadPage.ShowBaseNameInsteadOfUrl := True;
  ExtractionPage := CreateExtractionPage('Preparing recording engine', 'Verifying and extracting FFmpeg. This may take a moment.', nil);
end;

function PrepareFFmpeg: String;
var
  EnginePath, ExistingEngine: String;
begin
  Result := '';
  EnginePath := ExpandConstant('{tmp}\ffmpeg-extracted\ffmpeg-9.0.2-essentials_build\bin\ffmpeg.exe');
  try
    if FileExists(EnginePath) then begin
      if CompareText(GetSHA256OfFile(EnginePath), FFmpegBinaryHash) = 0 then Exit;
    end;
    ExistingEngine := AddBackslash(WizardDirValue) + 'ffmpeg.exe';
    if FileExists(ExistingEngine) then begin
      if CompareText(GetSHA256OfFile(ExistingEngine), FFmpegBinaryHash) = 0 then begin
        if ForceDirectories(ExtractFileDir(EnginePath)) and FileCopy(ExistingEngine, EnginePath, False) then begin
          if CompareText(GetSHA256OfFile(EnginePath), FFmpegBinaryHash) = 0 then begin
            Log('Reused existing FFmpeg executable after SHA-256 verification.');
            Exit;
          end;
        end;
      end;
    end;
    DownloadPage.Clear;
    DownloadPage.Add(FFmpegUrl, 'ffmpeg-9.0.2-essentials_build.zip', FFmpegArchiveHash);
    DownloadPage.Show;
    try
      DownloadPage.Download;
    finally
      DownloadPage.Hide;
    end;
    ExtractionPage.Clear;
    ExtractionPage.Add(ExpandConstant('{tmp}\ffmpeg-9.0.2-essentials_build.zip'), ExpandConstant('{tmp}\ffmpeg-extracted'), True);
    ExtractionPage.Show;
    try
      ExtractionPage.Extract;
    finally
      ExtractionPage.Hide;
    end;
    if not FileExists(EnginePath) then RaiseException('The downloaded package does not contain ffmpeg.exe.');
    if CompareText(GetSHA256OfFile(EnginePath), FFmpegBinaryHash) <> 0 then RaiseException('The extracted recording engine checksum did not match.');
    Log('FFmpeg archive and executable SHA-256 verification passed.');
  except
    Result := 'The recording engine could not be prepared. Check your internet connection and retry. ' + GetExceptionMessage;
    Log(Result);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ExitCode: Integer;
begin
  Result := PrepareFFmpeg;
  if Result <> '' then Exit;
  if WebView2Installed then Exit;
  WizardForm.PreparingLabel.Caption := 'Installing Microsoft Edge WebView2. This requires an internet connection and may take a few minutes...';
  ExtractTemporaryFile('MicrosoftEdgeWebview2Setup.exe');
  if not Exec(ExpandConstant('{tmp}\MicrosoftEdgeWebview2Setup.exe'), '/silent /install', '', SW_HIDE, ewWaitUntilTerminated, ExitCode) then begin
    Result := 'Microsoft Edge WebView2 could not be started. Install the WebView2 Evergreen Runtime from Microsoft, then run CamCord Setup again.';
    Exit;
  end;
  Log(Format('WebView2 bootstrapper exit code: %d', [ExitCode]));
  if (ExitCode <> 0) and (ExitCode <> 3010) then begin
    Result := Format('Microsoft Edge WebView2 installation failed (exit code %d). Check your internet connection and retry, or install the WebView2 Evergreen Runtime from Microsoft.', [ExitCode]);
    Exit;
  end;
  if not WebView2Installed then begin
    Result := 'Microsoft Edge WebView2 is still unavailable after installation. Restart Windows if requested, install the WebView2 Evergreen Runtime, and run CamCord Setup again.';
    Exit;
  end;
  if ExitCode = 3010 then NeedsRestart := True;
end;
