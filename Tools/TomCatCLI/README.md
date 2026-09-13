# TomCatCLI

`TomCatCLI` is the window-free entry point for CI cooks and Player builds.

```powershell
vendor/premake/bin/premake5.exe --file=Tools/premake5.lua vs2022
msbuild Tools/Tools.sln -p:Configuration=Release -p:Platform=x64
Tools/bin/Release-windows-x86_64/TomCatCLI/TomCatCLI.exe cook --project C:/Game/Game.tcproj --output C:/Game/Build/Game.tcpak
Tools/bin/Release-windows-x86_64/TomCatCLI/TomCatCLI.exe build --project C:/Game/Game.tcproj
```

Both commands perform a fresh managed Release compile, validate assets, and use
the project's enabled entry scene. Migration is preview-only by default; after
reviewing the printed file list, pass `--migrate` to allow the transactional
upgrade.

Official and local Editor release archives include `TomCatCLI.exe` beside
`TomCat.exe`, `Managed`, and `Packages`. Run the CLI from that unpacked directory
so managed compilation and the default `Packages/PlayerTemplates/win-x64`
template are discovered without repository-only paths.
