; TTT2-Launcher-Windows-<version>.exe: installs TTT2 Ultimate Launcher (windows/launcher/ttt2_launcher.py, frozen
; with PyInstaller, with t7helper.exe) for the current user, with Start menu and desktop shortcuts. Nothing of either
; game is in it.
;
;   iscc /DSourceDir=<PyInstaller's folder> /DAppVersion=<version> /DOutputDir=<folder> windows\launcher\installer.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
AppId={{8C2E5B71-3D4F-4A9E-B6C1-2F7A9D0E4B53}
AppName=TTT2 Ultimate Launcher
AppVersion={#AppVersion}
AppPublisher=Tekken Tag 2 Ultimate Edition (unofficial)
DefaultDirName={localappdata}\Programs\TTT2 Ultimate Launcher
DisableProgramGroupPage=yes
; (asked: nothing need go to the system drive)
DisableDirPage=no
UsePreviousAppDir=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
OutputDir={#OutputDir}
OutputBaseFilename=TTT2-Launcher-Windows-{#AppVersion}
UninstallDisplayName=TTT2 Ultimate Launcher

[Languages]
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
es.Run=Abrir TTT2 Ultimate Launcher ahora
en.Run=Open TTT2 Ultimate Launcher now
es.ShortcutName=TTT2 Ultimate Launcher
en.ShortcutName=TTT2 Ultimate Launcher

[Messages]
es.WelcomeLabel2=Esto instala TTT2 Ultimate Launcher, el lanzador de Tekken Tag Tournament 2 Ultimate Edition: juega, actualiza el juego y el lanzador con un botón, instala el juego desde tu .wua y añade los luchadores de Tekken 7 que le faltan (Negan, Akuma, Kazumi...) a partir de tu propia copia.%n%nNo incluye nada de ninguno de los dos juegos.
en.WelcomeLabel2=This installs TTT2 Ultimate Launcher, the launcher of Tekken Tag Tournament 2 Ultimate Edition: play, update the game and the launcher with one button, install the game from your .wua and add the Tekken 7 fighters it lacks (Negan, Akuma, Kazumi...) from your own copy.%n%nNothing of either game is included.

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[InstallDelete]
; (the previous version's program; its settings, downloads and kit stay)
Type: filesandordirs; Name: "{app}\_internal"

[Icons]
Name: "{userprograms}\{cm:ShortcutName}"; Filename: "{app}\TTT2 Ultimate Launcher.exe"; WorkingDir: "{app}"
Name: "{userdesktop}\{cm:ShortcutName}"; Filename: "{app}\TTT2 Ultimate Launcher.exe"; WorkingDir: "{app}"

[Run]
Filename: "{app}\TTT2 Ultimate Launcher.exe"; WorkingDir: "{app}"; Description: "{cm:Run}"; Flags: postinstall nowait skipifsilent

[UninstallDelete]
; (what the launcher keeps beside itself: settings, downloads, the game installer's kit)
Type: filesandordirs; Name: "{app}\downloads"
Type: filesandordirs; Name: "{app}\tmp"
Type: filesandordirs; Name: "{app}\kit"
