# CloudRedirect

Steam Cloud save redirection for unlocked games.

CloudRedirect intercepts Steam's internal cloud save RPC calls and redirects save files to third-party cloud storage providers (Google Drive, OneDrive, Cloudflare R2, S3-compatible, WebDAV, or local storage) natively within the Steam Client.

## Features

- **Steam Cloud Redirection**: Seamless cloud synchronization directly inside the Steam UI.
- **AutoCloud & Manual Save Support**: Compatible with both AutoCloud games and manual save file implementations.
- **Ecosystem Compatibility**: Works out of the box with **LuaTools**, **BetterSteamTools (BST)**, and **OpenSteamTool (OST)**.
- **Steam 1791+ Support**: Dynamically resolves Steam internal offsets (`CProtoBufMsg`, `AppInfoCache`) for modern Steam client builds.
- **Automated CI/CD**: Clean automated release builds providing verified binaries and SHA-256 checksums.

## Supported Storage Providers

- **Google Drive**
- **OneDrive**
- **Cloudflare R2**
- **S3-Compatible Storage** (AWS S3, MinIO, Backblaze B2, Wasabi, DigitalOcean Spaces, etc.)
- **WebDAV** (Nextcloud, ownCloud, Alist, custom WebDAV servers; supports Basic, Digest, NTLM, Negotiate, Bearer, anonymous)
- **Local Folder / Mapped Drive**

## Usage (Windows)

1. Download the latest release from the [Releases](https://github.com/Vigno04/CloudRedirect/releases) page.
2. Run `CloudRedirect.exe`.
3. In **Setup**, run the patches and deploy `cloud_redirect.dll`.
4. In the **Cloud Provider** tab, select your preferred storage provider and sign in / configure credentials.
5. Launch Steam.

If you are using **LuaTools**, you can also enable and manage CloudRedirect directly from the LuaTools Mode page.

## Building from Source

### Prerequisites
- Windows 10/11
- Visual Studio 2022 (C++ and .NET 8 desktop workloads)
- CMake 3.20+

### Build Commands
```cmd
cmake -S . -B build -G "Ninja Multi-Config"
cmake --build build --config Release --target cloud_redirect cloud_redirect_cli
dotnet publish ui/CloudRedirect.csproj -c Release -r win-x64 --self-contained false -o dist/publish
```

## Credits & License

- Original project by [Selectively11](https://github.com/Selectively11).
- Maintained and updated by [Davide Vignali (Vigno04)](https://github.com/Vigno04).
- Licensed under the MIT License.
