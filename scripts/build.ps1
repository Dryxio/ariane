#Requires -Version 5.1
<#
.SYNOPSIS
Builds librw and Ariane (euryopa) on Windows. Run from the root of the repo,
inside a Visual Studio developer shell.

.PARAMETER Channel
Build channel passed to premake5: master (default) or PE.

.PARAMETER LibrwPath
Path to the librw worktree. When omitted, a folder chooser opens.
#>
param(
    [ValidateSet('master', 'PE')]
    [string]$Channel = 'master',
    [string]$LibrwPath
)

$ErrorActionPreference = 'Stop'

function Select-LibrwFolder
{
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $dialog.Description = 'Select your clone of librw'
    $dialog.ShowNewFolderButton = $false
    if ($env:LIBRW -and (Test-Path -LiteralPath $env:LIBRW))
    {
        $dialog.SelectedPath = $env:LIBRW
    }
    if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK)
    {
        throw 'No librw folder selected.'
    }
    return $dialog.SelectedPath
}

function Invoke-Native
{
    param([string]$Command, [string[]]$Arguments)
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0)
    {
        throw "$Command failed with exit code $LASTEXITCODE."
    }
}

if (-not $LibrwPath)
{
    $LibrwPath = Select-LibrwFolder
}
if (-not (Test-Path -LiteralPath $LibrwPath -PathType Container))
{
    throw "librw folder not found: $LibrwPath"
}
$LibrwPath = (Resolve-Path -LiteralPath $LibrwPath).Path

[System.Environment]::SetEnvironmentVariable('LIBRW', $LibrwPath, 'User')
$env:LIBRW = $LibrwPath

$repoRoot = (Get-Location).Path
$platform = 'win-amd64-d3d9'

Push-Location $LibrwPath
try
{
    Invoke-Native premake5 @('vs2019')
    Invoke-Native msbuild @('build\librw.sln', '/p:Configuration=Release', "/p:Platform=$platform", '/t:librw', '/m')
} finally
{
    Pop-Location
}

Set-Location $repoRoot
Invoke-Native premake5 @('vs2019', "--channel=$Channel")
Invoke-Native msbuild @('build\librwgta.sln', '/p:Configuration=Release', "/p:Platform=$platform", '/t:librwgta,euryopa', '/m')
