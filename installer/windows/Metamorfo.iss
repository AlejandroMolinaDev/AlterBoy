; Inno Setup 6 script: Metamorfo installer for Windows x64 (SPEC §10, phase 7).
; Build:  iscc /DMyAppVersion=0.1.0 /DArtefactsDir=<...\Metamorfo_artefacts\Release> installer\windows\Metamorfo.iss
; Installs VST3 to C:\Program Files\Common Files\VST3, CLAP to ...\Common Files\CLAP
; and the standalone app to C:\Program Files\Metamorfo.

#ifndef MyAppVersion
  #define MyAppVersion "0.1.0"
#endif
#ifndef ArtefactsDir
  #define ArtefactsDir "..\..\build\release\Metamorfo_artefacts\Release"
#endif

[Setup]
AppId={{5B9E3C0A-6F4D-4C1E-9A7B-2D8E41F0C3A5}
AppName=Metamorfo
AppVersion={#MyAppVersion}
AppVerName=Metamorfo {#MyAppVersion}
AppPublisher=Metamorfo Audio
DefaultDirName={autopf}\Metamorfo
DefaultGroupName=Metamorfo
DisableProgramGroupPage=yes
LicenseFile=..\..\LICENSE.md
OutputDir=..\..\dist
OutputBaseFilename=Metamorfo-{#MyAppVersion}-Windows-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Metamorfo {#MyAppVersion}

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Types]
Name: "full"; Description: "Completa (VST3 + CLAP + aplicación)"
Name: "custom"; Description: "Personalizada"; Flags: iscustom

[Components]
Name: "vst3"; Description: "Plugin VST3 (C:\Program Files\Common Files\VST3)"; Types: full custom
Name: "clap"; Description: "Plugin CLAP (C:\Program Files\Common Files\CLAP)"; Types: full custom
Name: "standalone"; Description: "Aplicación independiente (Standalone)"; Types: full

[Files]
Source: "{#ArtefactsDir}\VST3\Metamorfo.vst3\*"; DestDir: "{commoncf64}\VST3\Metamorfo.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: vst3
Source: "{#ArtefactsDir}\CLAP\Metamorfo.clap"; DestDir: "{commoncf64}\CLAP"; Flags: ignoreversion; Components: clap
Source: "{#ArtefactsDir}\Standalone\Metamorfo.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: standalone
Source: "..\..\docs\GUIA_USUARIO_es.md"; DestDir: "{app}"; DestName: "Guia de usuario.md"; Flags: ignoreversion
Source: "..\..\LICENSE.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\THIRD_PARTY_LICENSES.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Metamorfo"; Filename: "{app}\Metamorfo.exe"; Components: standalone

[Run]
Filename: "{app}\Guia de usuario.md"; Description: "Abrir la guía de usuario"; Flags: postinstall shellexec skipifsilent unchecked
