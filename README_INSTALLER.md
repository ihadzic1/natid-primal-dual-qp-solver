# NatIDQP cross-platform installer add-on

This add-on follows the supplied natID `SetupCollector` packaging example and
creates installers for `natid_qp_gui` on Windows, macOS Apple Silicon, macOS
Intel and Linux.

## Copy into the repository

Copy these three folders/files into the root of the NatIDQP repository:

```text
NatIDQP/
|-- .github/
|   `-- workflows/release-all-installers.yml
|-- packaging/
|   |-- NatIDQP.xml
|   `-- GTK4.xml
`-- scripts/
    `-- build_installer_windows.ps1
```

Merge the folders with existing `.github`, `packaging`, and `scripts` folders;
do not put `NatIDQP_Installer_Addon` itself inside the repository.

If the earlier Windows-only add-on is already present, delete
`.github/workflows/release-windows-installer.yml`. The new
`release-all-installers.yml` replaces it and otherwise both workflows would
run for the same version tag.

Append the two entries from `GITIGNORE_SNIPPET.txt` to the repository's
existing `.gitignore` so locally generated installer files are not committed.

The root project must already contain the `gui` folder and exactly one:

```cmake
add_subdirectory(gui)
```

`packaging/NatIDQP.xml` intentionally uses the development path
`~Desktop/NatIDQP-Packaging-Source/gui`, because the GUI resource descriptor
is located at `gui/res/DevRes.xml`. Pointing `dev` at the repository root makes
SetupCollector fail with `There is no DevRes file`.

The GUI's `gui/res/DevRes.xml` must also contain non-empty product metadata,
especially `displayName`. SetupCollector uses this information for the Windows
installer and stops with `Product displayName cannot be empty` if it is absent.

## Build locally on Windows

Requirements:

- Visual Studio 2022 with C++ and CMake;
- `C:\Users\<user>\natID.SDK` with the Windows binary package extracted;
- `C:\Users\<user>\natID.Utils` containing `windows\SetupCollector.exe`.

Open PowerShell in the repository root and run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_installer_windows.ps1
```

The final archive is written to:

```text
NatIDQP-Windows-Installer.zip
```

It normally contains the installer bootstrapper `.exe` and `.msi`. Because the
generic natID bootstrapper is unsigned, Windows Security may classify it as a
potentially unwanted application while ZIP compression is reading it. The
script does not weaken antivirus protection: it automatically falls back to a
safe MSI-only ZIP. The MSI is the actual Windows installer and remains usable
on systems with the required Visual C++ runtime.

## Build all platforms using GitHub Actions

After committing the add-on, open **Actions > Build NatIDQP Installers > Run
workflow**. One workflow run builds and tests all supported targets and creates:

- `NatIDQP-Windows.zip` containing the EXE and MSI, or MSI only if Windows
  Security blocks the unsigned bootstrapper;
- `NatIDQP-macOS-Silicon.zip` for Apple Silicon Macs;
- `NatIDQP-macOS-Intel.zip` for Intel Macs;
- `NatIDQP-Linux.zip` containing the Ubuntu 24.04+ DEB package.

macOS and Linux packages must be built on their respective GitHub-hosted
runners; the local PowerShell script builds only the Windows installer.

To create a GitHub Release automatically, push a version tag:

```bash
git tag v1.0.0
git push origin v1.0.0
```

The workflow builds Release, executes `natid_qp_tests`, runs natID
`SetupCollector`, validates every platform package and attaches all four ZIP
files to the tagged GitHub Release. macOS bundles are ad-hoc signed but not
Apple-notarized, and the Linux DEB is not repository-signed.

## Runtime dependencies included

The collector includes:

- `mainUtils`, `dataProvider`, and `natGUI` through `natGUIALL`;
- `Matrix`;
- `symbSolvers` for sparse LDLT;
- GTK4 runtime DLLs and GLib schemas.

The QP datasets are intentionally not embedded because the application loads
any compatible problem through **Choose QP Folder**.
