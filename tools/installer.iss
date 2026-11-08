; Установщик ASTRIX-Setup.exe. Собирается из чистой папки dist\ASTRIX: tools\make-release.ps1 -Installer
; Ставится в профиль пользователя: Аня хранит настройки рядом с программой, в Program Files ей писать нельзя.

#ifndef AppVer
  #define AppVer "1.3.1"
#endif

[Setup]
AppId={{7E3C1A52-9B46-4D1E-8F27-5A0C6D2B9E11}
AppName=ASTRIX - Voice assistant
AppVersion={#AppVer}
AppPublisher=ASTRIX
AppPublisherURL=https://github.com/zephiro4ka/Voice-assistant-ASTRIX
DefaultDirName={localappdata}\ASTRIX
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=ASTRIX-Setup
SetupIconFile=..\assets\astrix.ico
Compression=lzma2/fast
SolidCompression=no
WizardStyle=modern
UninstallDisplayIcon={app}\ASTRIX.exe
UninstallDisplayName=ASTRIX - Voice assistant
CloseApplications=yes

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "Создать ярлык на рабочем столе"
Name: "aimodel"; Description: "Скачать нейросеть Qwen 3.5 (3,2 ГБ) — без неё помощница не отвечает на вопросы"

[Files]
Source: "..\dist\ASTRIX\*"; DestDir: "{app}"; Excludes: "models\ggml-small.bin"; Flags: recursesubdirs createallsubdirs ignoreversion
; скачанное при установке (если файл уже стоит или не скачался — пропускаем)
Source: "{tmp}\ggml-small.bin"; DestDir: "{app}\models"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\cloudflared.exe"; DestDir: "{app}"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\Qwen3.5-4B-Q4_K_M.gguf"; DestDir: "{app}\models\qwen"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\mmproj-Qwen3.5-4B-BF16.gguf"; DestDir: "{app}\models\qwen"; Flags: external skipifsourcedoesntexist ignoreversion

[Icons]
Name: "{autoprograms}\ASTRIX"; Filename: "{app}\ASTRIX.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\ASTRIX"; Filename: "{app}\ASTRIX.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\ASTRIX.exe"; Description: "Запустить"; WorkingDir: "{app}"; Flags: postinstall nowait skipifsilent

[Code]
const
  HF_MODEL = 'https://huggingface.co/lmstudio-community/Qwen3.5-4B-GGUF/resolve/main/';
var
  DownloadPage: TDownloadWizardPage;
  Failed: String;

function OnProgress(const Url, FileName: String; const Progress, ProgressMax: Int64): Boolean;
begin
  Result := True;
end;

procedure InitializeWizard;
begin
  DownloadPage := CreateDownloadPage('Скачиваю нужные файлы', 'Распознавание речи, подключение пульта и нейросеть. Это может занять несколько минут.', @OnProgress);
end;

procedure Fetch(const Url, Name, Sha, Target, Title: String);
begin
  if FileExists(ExpandConstant(Target)) then Exit;
  try
    if WizardSilent then
      DownloadTemporaryFile(Url, Name, Sha, @OnProgress)
    else begin
      DownloadPage.Clear;
      DownloadPage.Add(Url, Name, Sha);
      DownloadPage.Download;
    end;
  except
    Failed := Failed + #13#10 + '• ' + Title;
  end;
end;

procedure FetchAll;
begin
  Failed := '';
  Fetch('https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin', 'ggml-small.bin', '1be3a9b2063867b937e64e2ec7483364a79917e157fa98c5d94b5c1fffea987b', '{app}\models\ggml-small.bin', 'распознавание речи');
  Fetch('https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-windows-amd64.exe', 'cloudflared.exe', '', '{app}\cloudflared.exe', 'пульт в Telegram (cloudflared)');
  if WizardIsTaskSelected('aimodel') then begin
    Fetch(HF_MODEL + 'Qwen3.5-4B-Q4_K_M.gguf', 'Qwen3.5-4B-Q4_K_M.gguf', '25082a7dd3776cc3c741c6347d3bd04523f05796607b3fbc32fa3a25dfa1418c', '{app}\models\qwen\Qwen3.5-4B-Q4_K_M.gguf', 'нейросеть');
    Fetch(HF_MODEL + 'mmproj-Qwen3.5-4B-BF16.gguf', 'mmproj-Qwen3.5-4B-BF16.gguf', 'ae08d9d7eceb8f2d0672d61b5e6aa78b611f2942b55ed71d21414980cc454b91', '{app}\models\qwen\mmproj-Qwen3.5-4B-BF16.gguf', 'зрение нейросети');
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpReady then begin
    DownloadPage.Show;
    try
      FetchAll;
    finally
      DownloadPage.Hide;
    end;
    if Failed <> '' then
      SuppressibleMsgBox('Не удалось скачать:' + Failed + #13#10#13#10 + 'Установка продолжится, а эти функции не будут работать, пока не запустишь установщик ещё раз с интернетом.', mbInformation, MB_OK, IDOK);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if WizardSilent then FetchAll;
end;
