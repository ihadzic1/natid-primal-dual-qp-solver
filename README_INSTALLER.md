# natidqp solver installers

Application name: **natidqp solver**  
Author: **Irfan Hadzic**  
Version: **1.0.0**

The project uses natID `SetupCollector` to produce a Windows MSI, macOS app
bundles for Apple Silicon and Intel, and an Ubuntu 24.04+ DEB package. The
application icon on every platform is generated from the project-owned
`gui/res/appIcon` assets.

## GitHub Actions

The workflow is stored at:

```text
.github/workflows/release-all-installers.yml
```

It deliberately keeps the natID source tree and prebuilt libraries on the same
release (`v4.2.1`, build `20260710`). Runner versions are pinned to toolchains
compatible with that SDK:

- Windows Server 2022 with Visual Studio 2022;
- macOS 15 Apple Silicon;
- macOS 15 Intel;
- Ubuntu 24.04 x64.

Every platform job builds `natid_qp`, `natid_qp_gui`, and `natid_qp_tests`, runs
the test suite, invokes `SetupCollector`, and verifies that its installer exists.
The workflow then uploads:

```text
natidqp-solver-Windows.zip
natidqp-solver-macOS-Silicon.zip
natidqp-solver-macOS-Intel.zip
natidqp-solver-Linux.zip
```

Run it manually from **Actions > Build natidqp solver installers > Run
workflow**. Leave `publish_release` set to `no` to create downloadable workflow
artifacts without publishing a GitHub Release.

To build and publish a tagged release:

```bash
git tag v1.0.0
git push origin v1.0.0
```

For a manual release, set `publish_release` to `yes` and provide an existing or
new tag name in `release_tag`.

The macOS apps are ad-hoc signed for archive integrity but are not notarized by
Apple. The Linux package is not repository-signed.

## Local Windows installer

Requirements:

- Visual Studio 2022 with Desktop C++ and CMake support;
- `C:\Users\<user>\natID.SDK` from natID `v4.2.1` with the matching Windows
  binary archive extracted into `bin`;
- `C:\Users\<user>\natID.Utils` containing
  `windows\SetupCollector.exe`.

From the project root run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_installer_windows.ps1
```

For an isolated build location, pass a parent directory with
`-BuildHomeRoot`. The script creates its required `natID.RAMDisk` below that
directory without changing `HOME` or `USERPROFILE`.

The result is:

```text
natidqp-solver-Windows-Installer.zip
```

The ZIP normally contains the installer bootstrapper and MSI. If Windows
Security blocks the unsigned generic bootstrapper during archiving, the script
creates a usable MSI-only ZIP without weakening antivirus protection.

## Packaging configuration

`packaging/NatIDQP.xml` points `SetupCollector` to the GUI resource directory,
includes the Matrix, symbolic solver, model solver, and natGUI runtime packages,
and embeds all QP problem folders from `data`.

The human-readable product name comes from `gui/res/DevRes.xml`. The Linux
`SetupCollector` derives Debian's machine-safe `Package` field from the
executable identifier, which would turn `natid_qp_gui` into an invalid Debian
package name. During collection, the Linux job places a narrow `dpkg-deb`
wrapper first on `PATH`; it changes only the generated control field to
`Package: natidqp-solver` and delegates packaging to `/usr/bin/dpkg-deb`.
The executable remains `natid_qp_gui`, and the visible product name remains
`natidqp solver`. The Linux job additionally places all PNG icon sizes into the
DEB hicolor icon tree because some `SetupCollector` builds do not populate
those directories themselves.

Icon assets:

```text
gui/res/appIcon/winApp.ico
gui/res/appIcon/macApp.icns
gui/res/appIcon/lnxApp16.png
gui/res/appIcon/lnxApp32.png
gui/res/appIcon/lnxApp48.png
gui/res/appIcon/lnxApp128.png
gui/res/appIcon/lnxApp256.png
```
