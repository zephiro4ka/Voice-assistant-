; Установщик ASTRIX-Setup.exe. Собирается из чистой папки dist\ASTRIX: tools\make-release.ps1 -Installer
; Ставится в профиль пользователя: Аня хранит настройки рядом с программой, в Program Files ей писать нельзя.

#ifndef AppVer
  #define AppVer "1.4"
#endif

[Setup]
AppId={{7E3C1A52-9B46-4D1E-8F27-5A0C6D2B9E11}
AppName=ASTRIX - Voice assistant
AppVersion={#AppVer}
AppPublisher=ASTRIX
AppPublisherURL=https://github.com/zephiro4ka/Voice-assistant-ASTRIX
DefaultDirName={localappdata}\ASTRIX
DisableProgramGroupPage=yes
DisableDirPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=ASTRIX-Engine
SetupIconFile=..\assets\astrix.ico
Compression=lzma2/fast
SolidCompression=no
WizardStyle=modern dark hidebevels
WizardBackImageFile=..\assets\installer-back.bmp
WizardBackColor=#120C22
WizardSizePercent=112
DisableWelcomePage=no
DisableReadyMemo=yes
WizardImageFile=..\assets\installer-side.bmp
WizardSmallImageFile=..\assets\installer-top.bmp
UninstallDisplayIcon={app}\ASTRIX.exe
UninstallDisplayName=ASTRIX - Voice assistant
CloseApplications=yes

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Messages]
russian.WelcomeLabel1=Привет! Сейчас установим ASTRIX
russian.WelcomeLabel2=Ничего настраивать не нужно. Просто нажимай «Далее», всё нужное скачается само.%n%nПонадобится интернет и примерно 5 ГБ свободного места.
russian.SelectTasksLabel2=Если не уверен, ничего не меняй и нажми «Далее».
russian.ClickNext=
russian.WizardSelectTasks=Пара мелочей
russian.SelectTasksDesc=Как тебе удобнее?
russian.WizardReady=Почти всё
russian.ReadyLabel1=Осталось нажать одну кнопку
russian.ReadyLabel2a=Нажми «Установить» и подожди. Можно пойти налить чай, если скорость интернета небольшая.
russian.ReadyLabel2b=Нажми «Установить» и подожди. Можно пойти налить чай, если скорость интернета небольшая.
russian.FinishedHeadingLabel=ASTRIX установлена!
russian.FinishedLabel=При первом запуске программа сама покажет, что и где находится.
russian.FinishedLabelNoIcons=При первом запуске программа сама покажет, что и где находится.

[Tasks]
Name: "startmenu"; Description: "Добавить в меню Пуск"
Name: "desktopicon"; Description: "Сделать значок на рабочем столе"

[Files]
Source: "..\dist\ASTRIX\*"; DestDir: "{app}"; Excludes: "models\*"; Flags: recursesubdirs createallsubdirs ignoreversion
; скачанное при установке (если файл уже стоит или не скачался — пропускаем)
Source: "{tmp}\ggml-small.bin"; DestDir: "{app}\models"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\cloudflared.exe"; DestDir: "{app}"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\st_voice_styles_M1.json"; DestDir: "{app}\models\supertonic-3\voice_styles"; DestName: "M1.json"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\st_voice_styles_M2.json"; DestDir: "{app}\models\supertonic-3\voice_styles"; DestName: "M2.json"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\st_voice_styles_M3.json"; DestDir: "{app}\models\supertonic-3\voice_styles"; DestName: "M3.json"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\st_voice_styles_M4.json"; DestDir: "{app}\models\supertonic-3\voice_styles"; DestName: "M4.json"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\st_voice_styles_M5.json"; DestDir: "{app}\models\supertonic-3\voice_styles"; DestName: "M5.json"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\st_LICENSE"; DestDir: "{app}\models\supertonic-3\"; DestName: "LICENSE"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\st_README.md"; DestDir: "{app}\models\supertonic-3\"; DestName: "README.md"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\st_onnx_duration_predictor.onnx"; DestDir: "{app}\models\supertonic-3\onnx"; DestName: "duration_predictor.onnx"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\st_onnx_text_encoder.onnx"; DestDir: "{app}\models\supertonic-3\onnx"; DestName: "text_encoder.onnx"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\st_onnx_tts.json"; DestDir: "{app}\models\supertonic-3\onnx"; DestName: "tts.json"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\st_onnx_unicode_indexer.json"; DestDir: "{app}\models\supertonic-3\onnx"; DestName: "unicode_indexer.json"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\st_onnx_vector_estimator.onnx"; DestDir: "{app}\models\supertonic-3\onnx"; DestName: "vector_estimator.onnx"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\st_onnx_vocoder.onnx"; DestDir: "{app}\models\supertonic-3\onnx"; DestName: "vocoder.onnx"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\st_voice_styles_F1.json"; DestDir: "{app}\models\supertonic-3\voice_styles"; DestName: "F1.json"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\st_voice_styles_F2.json"; DestDir: "{app}\models\supertonic-3\voice_styles"; DestName: "F2.json"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\st_voice_styles_F3.json"; DestDir: "{app}\models\supertonic-3\voice_styles"; DestName: "F3.json"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\st_voice_styles_F4.json"; DestDir: "{app}\models\supertonic-3\voice_styles"; DestName: "F4.json"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\st_voice_styles_F5.json"; DestDir: "{app}\models\supertonic-3\voice_styles"; DestName: "F5.json"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\Qwen3.5-0.8B-Q4_K_M.gguf"; DestDir: "{app}\models\q08"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\mmproj-Qwen3.5-0.8B-BF16.gguf"; DestDir: "{app}\models\q08"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\Qwen3.5-2B-Q4_K_M.gguf"; DestDir: "{app}\models\q2"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\mmproj-Qwen3.5-2B-BF16.gguf"; DestDir: "{app}\models\q2"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\Qwen3.5-4B-Q4_K_M.gguf"; DestDir: "{app}\models\q4"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\mmproj-Qwen3.5-4B-BF16.gguf"; DestDir: "{app}\models\q4"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\Qwen3.5-9B-Q4_K_M.gguf"; DestDir: "{app}\models\q9"; Flags: external skipifsourcedoesntexist ignoreversion
Source: "{tmp}\mmproj-Qwen3.5-9B-BF16.gguf"; DestDir: "{app}\models\q9"; Flags: external skipifsourcedoesntexist ignoreversion

[Icons]
Name: "{autoprograms}\ASTRIX"; Filename: "{app}\ASTRIX.exe"; WorkingDir: "{app}"; Tasks: startmenu
Name: "{autodesktop}\ASTRIX"; Filename: "{app}\ASTRIX.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\ASTRIX.exe"; Description: "Запустить"; WorkingDir: "{app}"; Flags: postinstall nowait skipifsilent

[Code]
var
  DownloadPage: TDownloadWizardPage;
  ModelPage: TInputOptionWizardPage;
  Failed: String;
  ProgressPath, CurrentTitle: String;
  LastPercent, GroupIndex, GroupTotal: Integer;

// Окно установщика (tools\setup-ui) читает этот файл и показывает ход работы
procedure Report(Percent: Integer; const Text: String);
var
  Lines: TArrayOfString;
begin
  if ProgressPath = '' then Exit;
  SetArrayLength(Lines, 2);
  Lines[0] := IntToStr(Percent);
  Lines[1] := Text;
  SaveStringsToUTF8File(ProgressPath, Lines, False);
end;

function OnProgress(const Url, FileName: String; const Progress, ProgressMax: Int64): Boolean;
var
  Percent: Integer;
begin
  Result := True;
  if ProgressMax <= 0 then Exit;
  // в группе бывает много файлов: процент считаем на всю группу, чтобы он не прыгал
  Percent := (GroupIndex - 1) * 100 div GroupTotal + (Progress * 100 div ProgressMax) div GroupTotal;
  if Percent <> LastPercent then begin
    LastPercent := Percent;
    Report(Percent, 'Скачиваю: ' + CurrentTitle);
  end;
end;

procedure CurInstallProgressChanged(CurProgress, MaxProgress: Integer);
var
  Percent: Integer;
begin
  if MaxProgress < 100 then Exit;
  Percent := CurProgress div (MaxProgress div 100);
  if Percent <> LastPercent then begin
    LastPercent := Percent;
    Report(Percent, 'Копирую файлы');
  end;
end;

procedure InitializeWizard;
begin
  ProgressPath := ExpandConstant('{param:PROGRESS|}');
  LastPercent := -1;
  ModelPage := CreateInputOptionPage(wpSelectTasks, 'Какую нейросеть поставить?', 'Это мозги помощницы. Не знаешь — оставь как есть.', 'Чем умнее нейросеть, тем больше она весит и тем мощнее нужен компьютер. Поменять можно потом прямо в программе.', True, False);
  ModelPage.Add('Самая лёгкая — 0,7 ГБ. Для слабых компьютеров. Отвечает быстро, но простовато.');
  ModelPage.Add('Лёгкая — 1,8 ГБ. Если видеокарта на 4 ГБ или её нет.');
  ModelPage.Add('Обычная (советую) — 3,2 ГБ. Для видеокарты от 6 ГБ. Хороший баланс.');
  ModelPage.Add('Умная — 6,1 ГБ. Для видеокарты от 8 ГБ. Умнее, но медленнее.');
  ModelPage.Add('Пока не скачивать — можно будет выбрать потом в настройках');
  ModelPage.SelectedValueIndex := 2;
  DownloadPage := CreateDownloadPage('Скачиваю нужные файлы', 'Ничего нажимать не нужно. Если интернет медленный, это может занять полчаса.', @OnProgress);
end;

procedure Fetch(const Url, Name, Sha, Target, Title: String);
begin
  if Title <> CurrentTitle then begin
    CurrentTitle := Title;
    GroupIndex := 0;
    if Title = 'голос' then GroupTotal := 18
    else if Title = 'нейросеть' then GroupTotal := 2
    else GroupTotal := 1;
  end;
  GroupIndex := GroupIndex + 1;
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
    if Pos(Title, Failed) = 0 then Failed := Failed + #13#10 + '• ' + Title;
  end;
end;

procedure FetchAll;
var
  Choice: Integer;
begin
  Failed := '';
  CurrentTitle := '';
  if WizardSilent then Choice := StrToIntDef(ExpandConstant('{param:MODEL|4}'), 4)
  else Choice := ModelPage.SelectedValueIndex;
  Fetch('https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin', 'ggml-small.bin', '1be3a9b2063867b937e64e2ec7483364a79917e157fa98c5d94b5c1fffea987b', '{app}\models\ggml-small.bin', 'распознавание речи');
  Fetch('https://huggingface.co/Supertone/supertonic-3/resolve/main/LICENSE', 'st_LICENSE', '0d944a9110fed9a9602d60e0423a272903e7bd21ab060490774efc77c2275e9f', '{app}\models\supertonic-3\\LICENSE', 'голос');
  Fetch('https://huggingface.co/Supertone/supertonic-3/resolve/main/README.md', 'st_README.md', 'd414e4b085fb9989ebfd2b64f868ff861694022b4bdb805b09e702386dbdc7d5', '{app}\models\supertonic-3\\README.md', 'голос');
  Fetch('https://huggingface.co/Supertone/supertonic-3/resolve/main/onnx/duration_predictor.onnx', 'st_onnx_duration_predictor.onnx', 'c3eb91414d5ff8a7a239b7fe9e34e7e2bf8a8140d8375ffb14718b1c639325db', '{app}\models\supertonic-3\onnx\duration_predictor.onnx', 'голос');
  Fetch('https://huggingface.co/Supertone/supertonic-3/resolve/main/onnx/text_encoder.onnx', 'st_onnx_text_encoder.onnx', 'c7befd5ea8c3119769e8a6c1486c4edc6a3bc8365c67621c881bbb774b9902ff', '{app}\models\supertonic-3\onnx\text_encoder.onnx', 'голос');
  Fetch('https://huggingface.co/Supertone/supertonic-3/resolve/main/onnx/tts.json', 'st_onnx_tts.json', '42078d3aef1cd43ab43021f3c54f47d2d75ceb4e75f627f118890128b06a0d09', '{app}\models\supertonic-3\onnx\tts.json', 'голос');
  Fetch('https://huggingface.co/Supertone/supertonic-3/resolve/main/onnx/unicode_indexer.json', 'st_onnx_unicode_indexer.json', '9bf7346e43883a81f8645c81224f786d43c5b57f3641f6e7671a7d6c493cb24f', '{app}\models\supertonic-3\onnx\unicode_indexer.json', 'голос');
  Fetch('https://huggingface.co/Supertone/supertonic-3/resolve/main/onnx/vector_estimator.onnx', 'st_onnx_vector_estimator.onnx', '883ac868ea0275ef0e991524dc64f16b3c0376efd7c320af6b53f5b780d7c61c', '{app}\models\supertonic-3\onnx\vector_estimator.onnx', 'голос');
  Fetch('https://huggingface.co/Supertone/supertonic-3/resolve/main/onnx/vocoder.onnx', 'st_onnx_vocoder.onnx', '085de76dd8e8d5836d6ca66826601f615939218f90e519f70ee8a36ed2a4c4ba', '{app}\models\supertonic-3\onnx\vocoder.onnx', 'голос');
  Fetch('https://huggingface.co/Supertone/supertonic-3/resolve/main/voice_styles/F1.json', 'st_voice_styles_F1.json', 'bbdec6ee00231c2c742ad05483df5334cab3b52fda3ba38e6a07059c4563dbc2', '{app}\models\supertonic-3\voice_styles\F1.json', 'голос');
  Fetch('https://huggingface.co/Supertone/supertonic-3/resolve/main/voice_styles/F2.json', 'st_voice_styles_F2.json', '7c722c6a72707b1a77f035d67f0d1351ba187738e06f7683e8c72b1df3477fc6', '{app}\models\supertonic-3\voice_styles\F2.json', 'голос');
  Fetch('https://huggingface.co/Supertone/supertonic-3/resolve/main/voice_styles/F3.json', 'st_voice_styles_F3.json', '12f6ef2573baa2defa1128069cb59f203e3ab67c92af77b42df8a0e3a2f7c6ab', '{app}\models\supertonic-3\voice_styles\F3.json', 'голос');
  Fetch('https://huggingface.co/Supertone/supertonic-3/resolve/main/voice_styles/F4.json', 'st_voice_styles_F4.json', 'c2fa764c1225a76dfc3e2c73e8aa4f70d9ee48793860eb34c295fff01c2e032b', '{app}\models\supertonic-3\voice_styles\F4.json', 'голос');
  Fetch('https://huggingface.co/Supertone/supertonic-3/resolve/main/voice_styles/F5.json', 'st_voice_styles_F5.json', '45966e73316415626cf41a7d1c6f3b4c70dbc1ba2bee5c1978ef0ce33244fc8d', '{app}\models\supertonic-3\voice_styles\F5.json', 'голос');
  Fetch('https://huggingface.co/Supertone/supertonic-3/resolve/main/voice_styles/M1.json', 'st_voice_styles_M1.json', 'e35604687f5d23694b8e91593a93eec0e4eca6c0b02bb8ed69139ab2ea6b0a5b', '{app}\models\supertonic-3\voice_styles\M1.json', 'голос');
  Fetch('https://huggingface.co/Supertone/supertonic-3/resolve/main/voice_styles/M2.json', 'st_voice_styles_M2.json', 'b76cbf62bac707c710cf0ae5aba5e31eea1a6339a9734bfae33ab98499534a50', '{app}\models\supertonic-3\voice_styles\M2.json', 'голос');
  Fetch('https://huggingface.co/Supertone/supertonic-3/resolve/main/voice_styles/M3.json', 'st_voice_styles_M3.json', 'ea1ac35ccb91b0d7ecad533a2fbd0eec10c91513d8951e3b25fbba99954e159b', '{app}\models\supertonic-3\voice_styles\M3.json', 'голос');
  Fetch('https://huggingface.co/Supertone/supertonic-3/resolve/main/voice_styles/M4.json', 'st_voice_styles_M4.json', 'ca8eefad4fcd989c9379032ff3e50738adc547eeb5e221b82593a6d7b3bac303', '{app}\models\supertonic-3\voice_styles\M4.json', 'голос');
  Fetch('https://huggingface.co/Supertone/supertonic-3/resolve/main/voice_styles/M5.json', 'st_voice_styles_M5.json', 'dd22b92740314321f8ae11c5e87f8dd60d060f15dd3a632b5adf77f471f77af2', '{app}\models\supertonic-3\voice_styles\M5.json', 'голос');
  Fetch('https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-windows-amd64.exe', 'cloudflared.exe', '', '{app}\cloudflared.exe', 'пульт в Telegram');
  if Choice = 0 then begin
    Fetch('https://huggingface.co/lmstudio-community/Qwen3.5-0.8B-GGUF/resolve/main/Qwen3.5-0.8B-Q4_K_M.gguf', 'Qwen3.5-0.8B-Q4_K_M.gguf', 'f5b14da98939b60bbe1019a964eba656407e1e0b64f1fe3003ff6d650e93bfec', '{app}\models\q08\Qwen3.5-0.8B-Q4_K_M.gguf', 'нейросеть');
    Fetch('https://huggingface.co/lmstudio-community/Qwen3.5-0.8B-GGUF/resolve/main/mmproj-Qwen3.5-0.8B-BF16.gguf', 'mmproj-Qwen3.5-0.8B-BF16.gguf', '6fdd1b4bdc3d2ae8bd15d783e23260dd07dcf83f45604a21dabfd6efad8f8bc5', '{app}\models\q08\mmproj-Qwen3.5-0.8B-BF16.gguf', 'нейросеть');
  end;
  if Choice = 1 then begin
    Fetch('https://huggingface.co/lmstudio-community/Qwen3.5-2B-GGUF/resolve/main/Qwen3.5-2B-Q4_K_M.gguf', 'Qwen3.5-2B-Q4_K_M.gguf', '0bfe35afc9f05b7fac3fa04925e051ac7939a42a8a17ea11afc99701bea826cc', '{app}\models\q2\Qwen3.5-2B-Q4_K_M.gguf', 'нейросеть');
    Fetch('https://huggingface.co/lmstudio-community/Qwen3.5-2B-GGUF/resolve/main/mmproj-Qwen3.5-2B-BF16.gguf', 'mmproj-Qwen3.5-2B-BF16.gguf', '31a8b681d34e70eaef158f7bacf1bc726a7a29babd185119ab3ebf402890dfbc', '{app}\models\q2\mmproj-Qwen3.5-2B-BF16.gguf', 'нейросеть');
  end;
  if Choice = 2 then begin
    Fetch('https://huggingface.co/lmstudio-community/Qwen3.5-4B-GGUF/resolve/main/Qwen3.5-4B-Q4_K_M.gguf', 'Qwen3.5-4B-Q4_K_M.gguf', '25082a7dd3776cc3c741c6347d3bd04523f05796607b3fbc32fa3a25dfa1418c', '{app}\models\q4\Qwen3.5-4B-Q4_K_M.gguf', 'нейросеть');
    Fetch('https://huggingface.co/lmstudio-community/Qwen3.5-4B-GGUF/resolve/main/mmproj-Qwen3.5-4B-BF16.gguf', 'mmproj-Qwen3.5-4B-BF16.gguf', 'ae08d9d7eceb8f2d0672d61b5e6aa78b611f2942b55ed71d21414980cc454b91', '{app}\models\q4\mmproj-Qwen3.5-4B-BF16.gguf', 'нейросеть');
  end;
  if Choice = 3 then begin
    Fetch('https://huggingface.co/lmstudio-community/Qwen3.5-9B-GGUF/resolve/main/Qwen3.5-9B-Q4_K_M.gguf', 'Qwen3.5-9B-Q4_K_M.gguf', 'cd76ec205963b3b33350093e6904d9de16c4e666fd104e1f632d25c7f15f2a13', '{app}\models\q9\Qwen3.5-9B-Q4_K_M.gguf', 'нейросеть');
    Fetch('https://huggingface.co/lmstudio-community/Qwen3.5-9B-GGUF/resolve/main/mmproj-Qwen3.5-9B-BF16.gguf', 'mmproj-Qwen3.5-9B-BF16.gguf', '330d17547bfdbcd0e7a0cb3f4b06b4ceeac0aaa1e449122d9d66ef957aeb74b3', '{app}\models\q9\mmproj-Qwen3.5-9B-BF16.gguf', 'нейросеть');
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
      SuppressibleMsgBox('Не удалось скачать:' + Failed + #13#10#13#10 + 'Установка продолжится. Эти функции заработают, если запустить установщик ещё раз с интернетом.', mbInformation, MB_OK, IDOK);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if WizardSilent then begin
    FetchAll;
    LastPercent := -1;
    Report(0, 'Копирую файлы');
    if (ProgressPath <> '') and (Failed <> '') then SaveStringToFile(ProgressPath + '.failed', '', False);
    if (ProgressPath <> '') and (Failed <> '') then SaveStringsToUTF8File(ProgressPath + '.failed', [Trim(Failed)], False);
  end;
end;
