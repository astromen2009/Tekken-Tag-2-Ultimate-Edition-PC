; TTT2-Fighters-Windows-<version>.exe: installs TTT2 Fighters (windows/fighters/ttt2_fighters.py, frozen with
; PyInstaller, with t7helper.exe) for the current user, with a Start menu shortcut. Nothing of either game is in it.
;
;   iscc /DSourceDir=<PyInstaller's folder> /DAppVersion=<version> /DOutputDir=<folder> windows\fighters\installer.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
AppId={{3F7D2C91-8B4A-4E6F-A1D2-5C9E8B7A6F30}
AppName=TTT2 Fighters
AppVersion={#AppVersion}
AppPublisher=Tekken Tag 2 Ultimate Edition (unofficial)
DefaultDirName={localappdata}\Programs\TTT2 Fighters
DisableProgramGroupPage=yes
DisableDirPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
OutputDir={#OutputDir}
OutputBaseFilename=TTT2-Fighters-Windows-{#AppVersion}
UninstallDisplayName=TTT2 Fighters

[Languages]
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
es.Run=Abrir TTT2 Fighters ahora
en.Run=Open TTT2 Fighters now
es.ShortcutName=TTT2 Fighters (luchadores de Tekken 7)
en.ShortcutName=TTT2 Fighters (Tekken 7 fighters)

[Messages]
es.WelcomeLabel2=Esto instala TTT2 Fighters, que añade a Tekken Tag Tournament 2 Ultimate Edition los luchadores de Tekken 7 que no tiene (Negan, Akuma, Kazumi...), a partir de tus propias copias de ambos juegos.%n%nNecesitas Tekken Tag Tournament 2 ya instalado, Tekken 7 de Steam y Steam abierto. No incluye nada de ninguno de los dos juegos.
en.WelcomeLabel2=This installs TTT2 Fighters, which adds Tekken 7's fighters that Tekken Tag Tournament 2 Ultimate Edition lacks (Negan, Akuma, Kazumi...) from your own copies of both games.%n%nYou need Tekken Tag Tournament 2 installed, Tekken 7 from Steam and Steam running. Nothing of either game is included.

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[InstallDelete]
Type: filesandordirs; Name: "{app}\*"

[Icons]
Name: "{userprograms}\{cm:ShortcutName}"; Filename: "{app}\TTT2 Fighters.exe"; WorkingDir: "{app}"

[Run]
Filename: "{app}\TTT2 Fighters.exe"; WorkingDir: "{app}"; Description: "{cm:Run}"; Flags: postinstall nowait skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{localappdata}\ttt2\tekken7"
