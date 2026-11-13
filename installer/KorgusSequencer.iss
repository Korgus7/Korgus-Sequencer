; Korgus Sequencer - instalator (Inno Setup 6)
; iscc /DAppVer=1.0.N /DSrc=<priecinok s exe> installer\KorgusSequencer.iss

#ifndef AppVer
  #define AppVer "1.0.0"
#endif
#ifndef Src
  #define Src "..\out"
#endif

[Setup]
AppId={{6B1E2A54-3C7D-4F2A-9E1B-5A0C4D7E8F21}
AppName=Korgus Sequencer
AppVersion={#AppVer}
AppVerName=Korgus Sequencer {#AppVer}
AppPublisher=Korgus
DefaultDirName={localappdata}\Programs\Korgus Sequencer
DefaultGroupName=Korgus Sequencer
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#Src}\..\setup
OutputBaseFilename=KorgusSequencer-Setup-{#AppVer}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
UninstallDisplayIcon={app}\Korgus Sequencer.exe
UninstallDisplayName=Korgus Sequencer

[Languages]
Name: "sk"; MessagesFile: "compiler:Languages\Slovak.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#Src}\Korgus Sequencer.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Src}\AI\*"; DestDir: "{app}\AI"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist
Source: "{#Src}\Korgus Inspire.vst3"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#Src}\Korgus.vst3"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{autoprograms}\Korgus Sequencer"; Filename: "{app}\Korgus Sequencer.exe"
Name: "{autodesktop}\Korgus Sequencer"; Filename: "{app}\Korgus Sequencer.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Korgus Sequencer.exe"; Description: "{cm:LaunchProgram,Korgus Sequencer}"; Flags: nowait postinstall
