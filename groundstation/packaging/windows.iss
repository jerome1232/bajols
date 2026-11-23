[Setup]
AppId=Bajols.GroundStation
AppName=BAJOLS Ground Station
AppVersion={#AppVersion}
DefaultDirName={localappdata}\Programs\BAJOLS Ground Station
DefaultGroupName=BAJOLS Ground Station
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\packages
OutputBaseFilename=bajols-groundstation-{#AppVersion}-win-x64-setup
Compression=lzma2
SolidCompression=yes
UninstallDisplayIcon={app}\Bajols.GroundStation.exe

[Files]
Source: "..\package-publish\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\BAJOLS Ground Station"; Filename: "{app}\Bajols.GroundStation.exe"
