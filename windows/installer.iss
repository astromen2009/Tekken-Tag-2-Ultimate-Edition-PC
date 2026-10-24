; TTT2-Setup.exe: installs the Windows kit (windows/CMakeLists.txt, with its
; toolchain) for the current user and starts ttt2-setup.exe, which builds
; the game program from the player's .wua and installs the game. Nothing of
; the game is in it.
;
;   iscc /DKitDir=<kit> /DAppVersion=<version> /DOutputDir=<folder> windows\installer.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
AppId={{6B1C7E2A-4E1B-4C51-9C3B-7A1F2E9D8C41}
AppName=Tekken Tag Tournament 2 Ultimate Edition Installer
AppVersion={#AppVersion}
AppPublisher=Tekken Tag 2 Ultimate Edition (unofficial)
DefaultDirName={localappdata}\Programs\TTT2 Installer
DisableProgramGroupPage=yes
DisableDirPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
Compression=lzma2/ultra64
SolidCompression=yes
LZMAUseSeparateProcess=yes
WizardStyle=modern
OutputDir={#OutputDir}
OutputBaseFilename=TTT2-Setup-Windows-{#AppVersion}
UninstallDisplayName=Tekken Tag Tournament 2 Ultimate Edition Installer

[Languages]
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
es.RunSetup=Instalar el juego ahora (elegir el .wua)
en.RunSetup=Install the game now (choose the .wua)
es.ShortcutName=Instalar Tekken Tag Tournament 2
en.ShortcutName=Install Tekken Tag Tournament 2

[Messages]
es.WelcomeLabel2=Esto instala en tu PC el instalador de Tekken Tag Tournament 2 Ultimate Edition.%n%nDespués elegirás tu copia del juego (TEKKEN TAG TOURNAMENT 2 Wii U EDITION, europea, con la actualización v16, como archivo .wua) y la carpeta donde instalarlo; el programa del juego se compila entonces para tu PC.%n%nNo incluye el juego: necesitas tu propia copia.
en.WelcomeLabel2=This installs the Tekken Tag Tournament 2 Ultimate Edition installer on your PC.%n%nThen you choose your copy of the game (TEKKEN TAG TOURNAMENT 2 Wii U EDITION, Europe, with update v16, as a .wua archive) and the folder to install it into; the game program is then built for your PC.%n%nThe game is not included: you need your own copy.

[Files]
Source: "{#KitDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[InstallDelete]
; A previous version's kit, all of it.
Type: filesandordirs; Name: "{app}\*"

[Icons]
Name: "{userprograms}\{cm:ShortcutName}"; Filename: "{app}\ttt2-setup.exe"; WorkingDir: "{app}"

[Run]
Filename: "{app}\ttt2-setup.exe"; WorkingDir: "{app}"; Description: "{cm:RunSetup}"; Flags: postinstall nowait skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{localappdata}\ttt2\setup-work"
Type: filesandordirs; Name: "{localappdata}\ttt2\setup-check"
