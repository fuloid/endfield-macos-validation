param(
    [string]$OutputDirectory = 'D:\Jimu\endfield-dev\gryphfield-windows-demo',
    [string]$ZigDirectory = 'C:\Users\Pintu Masuk\.t3\scratch\2026-10-16-hi-i-want-to-make-d00919c3\zig-x86_64-windows-0.16.0'
)
$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'This build is for Windows only.' }
$env:PATH = $ZigDirectory + ';' + $env:PATH
$appRoot = Split-Path -Parent $PSScriptRoot
& node (Join-Path $PSScriptRoot 'prepare.mjs') $OutputDirectory
if ($LASTEXITCODE -ne 0) { throw 'Demo overlay failed.' }
Push-Location (Join-Path $appRoot '.windows-demo\app')
$previousNodeOptions = $env:NODE_OPTIONS
$loaderUrl = ([System.Uri](Join-Path $PSScriptRoot 'ts-loader.mjs')).AbsoluteUri
$env:NODE_OPTIONS = ($previousNodeOptions + ' --import="' + $loaderUrl + '"').Trim()
try {
    & npx -y -p '@native-sdk/cli@0.11.0' native check
    if ($LASTEXITCODE -ne 0) { throw 'Demo check failed.' }
    & npx -y -p '@native-sdk/cli@0.11.0' native build --yes -Dautomation=true
    if ($LASTEXITCODE -ne 0) { throw 'Windows build failed.' }
    & npx -y -p '@native-sdk/cli@0.11.0' native package --target windows --output $OutputDirectory
    if ($LASTEXITCODE -ne 0) { throw 'Windows packaging failed.' }
    $shortcutShell = New-Object -ComObject WScript.Shell
    $shortcut = $shortcutShell.CreateShortcut((Join-Path $OutputDirectory 'Launch Gryphfield Demo.lnk'))
    $shortcut.TargetPath = Join-Path $OutputDirectory 'bin\gryphfield-demo.exe'
    $shortcut.WorkingDirectory = $OutputDirectory
    $shortcut.Save()
} finally {
    $env:NODE_OPTIONS = $previousNodeOptions
    Pop-Location
}
