# PowerShell script to set the MSVC env vars globally for all the steps in our job.
# Adapted from https://github.com/microsoft/vswhere/wiki/Start-Developer-Command-Prompt
# (No, I don't know PowerShell; this is heavily cargo-culted.)
param (
  [string]$arch = "x64",
  [string]$hostArch = "x64"
 )

$vswherePath = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
$vsInstallationPath = & "$vswherePath" -latest -property installationPath
$vsDevCmdPath = "`"$vsInstallationPath\Common7\Tools\vsdevcmd.bat`""
$command = "$vsDevCmdPath -no_logo -arch=$arch -host_arch=$hostArch"

& "${env:COMSPEC}" /s /c "$command && set" | ForEach-Object {
  $name, $value = $_ -split '=', 2
  Write-Output "::set-env name=$name::$value"
}
