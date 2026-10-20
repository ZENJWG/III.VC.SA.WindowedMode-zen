# Used by build.bat. Requires Windows PowerShell 5.1+ and network access only
# when a missing dependency or compiler has to be installed.
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('Release', 'Gta3', 'GtaVC', 'GtaSA')]
    [string]$Configuration = 'Release',

    [switch]$NoToolInstall,
    [switch]$Package
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$externalDir = Join-Path $projectRoot 'external'
$gitCommand = Get-Command 'git.exe' -ErrorAction SilentlyContinue
$gitExe = if ($gitCommand) { $gitCommand.Source } else { $null }
$script:injectorRef = $null

function Log([string]$message) {
    Write-Host ('[build] ' + $message)
}

function Require-File([string]$relativePath) {
    $fullPath = Join-Path $projectRoot $relativePath
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "Missing required dependency file: $relativePath"
    }
}

function Invoke-Git([string[]]$arguments) {
    & $gitExe @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Git command failed (exit $LASTEXITCODE): git $($arguments -join ' ')"
    }
}

function Get-GitHubJson([string]$url) {
    return Invoke-RestMethod -Uri $url -Headers @{
        'User-Agent' = 'WindowedMode-Build-Bootstrap'
        'Accept' = 'application/vnd.github+json'
    } -TimeoutSec 60
}

function Get-DefaultBranch([string]$repository) {
    $info = Get-GitHubJson "https://api.github.com/repos/$repository"
    if (-not $info.default_branch) {
        throw "Could not determine default branch for $repository"
    }
    return [string]$info.default_branch
}

function Download-ZipRepository([string]$repository, [string]$reference, [string]$destination, [string]$marker) {
    $parts = $repository.Split('/')
    if ($parts.Length -ne 2) { throw "Unexpected GitHub repository: $repository" }

    $tempDir = Join-Path $externalDir ('.wm-download-' + [guid]::NewGuid().ToString('N'))
    $zipFile = Join-Path $env:TEMP ('wm-dep-' + [guid]::NewGuid().ToString('N') + '.zip')
    $url = "https://codeload.github.com/$($parts[0])/$($parts[1])/zip/$reference"
    try {
        New-Item -ItemType Directory -Path $tempDir -Force | Out-Null
        Log "Downloading $repository ($reference) ..."
        Invoke-WebRequest -Uri $url -UseBasicParsing -OutFile $zipFile -TimeoutSec 300
        Expand-Archive -LiteralPath $zipFile -DestinationPath $tempDir -Force
        $folders = @(Get-ChildItem -LiteralPath $tempDir -Directory)
        if ($folders.Count -ne 1) {
            throw "Unexpected ZIP archive layout for $repository"
        }
        $sourceDir = $folders[0].FullName
        if (-not (Test-Path -LiteralPath (Join-Path $sourceDir $marker) -PathType Leaf)) {
            throw "Downloaded $repository archive does not contain $marker"
        }
        if (Test-Path -LiteralPath $destination) {
            $existing = @(Get-ChildItem -LiteralPath $destination -Force | Select-Object -First 1)
            if ($existing.Count -ne 0) {
                throw "Existing folder is incomplete; refusing to overwrite: $destination"
            }
            Remove-Item -LiteralPath $destination -Force
        }
        Move-Item -LiteralPath $sourceDir -Destination $destination
    }
    finally {
        Remove-Item -LiteralPath $tempDir -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $zipFile -Force -ErrorAction SilentlyContinue
    }
}

function Restore-Repository([string]$relativePath, [string]$repository, [string]$marker) {
    $target = Join-Path $projectRoot $relativePath
    if (Test-Path -LiteralPath (Join-Path $target $marker) -PathType Leaf) {
        Log "Already available: $relativePath"
        return
    }

    # A proper Git clone has exact, pinned submodule revisions. Keep those.
    if ($gitExe -and (Test-Path -LiteralPath (Join-Path $projectRoot '.git'))) {
        Log "Restoring pinned Git submodule: $relativePath"
        Invoke-Git -arguments @('-C', $projectRoot, 'submodule', 'update', '--init', '--depth', '1', '--', $relativePath.Replace('\','/'))
        if (-not (Test-Path -LiteralPath (Join-Path $target $marker) -PathType Leaf)) {
            throw "Git submodule $relativePath is still missing $marker"
        }
        return
    }

    if ($gitExe) {
        # Standalone source ZIP: clone missing repositories individually.
        $tempClone = Join-Path $externalDir ('.wm-clone-' + [guid]::NewGuid().ToString('N'))
        try {
            Log "Cloning $repository ..."
            & $gitExe clone --depth 1 "https://github.com/$repository.git" $tempClone
            if ($LASTEXITCODE -eq 0) {
                if (-not (Test-Path -LiteralPath (Join-Path $tempClone $marker) -PathType Leaf)) {
                    throw "Cloned $repository but $marker is missing"
                }
                if (Test-Path -LiteralPath $target) {
                    $existing = @(Get-ChildItem -LiteralPath $target -Force | Select-Object -First 1)
                    if ($existing.Count -ne 0) {
                        throw "Existing folder is incomplete; refusing to overwrite: $target"
                    }
                    Remove-Item -LiteralPath $target -Force
                }
                Move-Item -LiteralPath $tempClone -Destination $target
                return
            }
            Log "Git clone failed; attempting GitHub ZIP download for $repository."
        }
        finally {
            Remove-Item -LiteralPath $tempClone -Recurse -Force -ErrorAction SilentlyContinue
        }
    }

    # Git-free fallback also works when the original project came from a ZIP.
    $branch = Get-DefaultBranch $repository
    Download-ZipRepository $repository $branch $target $marker
    if ($relativePath -eq 'external\injector') {
        $script:injectorRef = $branch
    }
}

function Restore-SafetyHook {
    $injector = Join-Path $externalDir 'injector'
    $marker = 'include\safetyhook.hpp'
    $safetyhook = Join-Path $injector 'safetyhook'
    if (Test-Path -LiteralPath (Join-Path $safetyhook $marker) -PathType Leaf) {
        Log 'Already available: external\injector\safetyhook'
        return
    }

    # Preserve the exact SafetyHook revision specified by the injector project.
    if ($gitExe -and ((Test-Path -LiteralPath (Join-Path $injector '.git')) -or
                       (Test-Path -LiteralPath (Join-Path $projectRoot '.git')))) {
        Log 'Restoring pinned SafetyHook submodule ...'
        Invoke-Git -arguments @('-C', $injector, 'submodule', 'update', '--init', '--depth', '1', '--', 'safetyhook')
        if (-not (Test-Path -LiteralPath (Join-Path $safetyhook $marker) -PathType Leaf)) {
            throw 'Git restored SafetyHook, but its header is still missing.'
        }
        return
    }

    # GitHub source ZIPs omit gitlink folders. Query the injector tree's exact
    # SafetyHook gitlink SHA rather than fetching incompatible SafetyHook HEAD.
    $ref = $script:injectorRef
    if (-not $ref) {
        $ref = Get-DefaultBranch 'ThirteenAG/injector'
    }
    $tree = Get-GitHubJson "https://api.github.com/repos/ThirteenAG/injector/git/trees/$ref"
    $link = @($tree.tree | Where-Object { $_.path -eq 'safetyhook' -and $_.type -eq 'commit' })
    if ($link.Count -ne 1) {
        throw "Could not resolve SafetyHook gitlink SHA from ThirteenAG/injector ($ref)."
    }
    Download-ZipRepository 'cursey/safetyhook' $link[0].sha $safetyhook $marker
}

function Find-MSBuild {
    $candidates = @()
    if (${env:ProgramFiles(x86)}) {
        $candidates += (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe')
    }
    if ($env:ProgramFiles) {
        $candidates += (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
    }
    foreach ($vswhere in $candidates) {
        if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) { continue }
        # A VS 2022 MSBuild cannot build a solution generated for VS 2026.
        $locations = @(& $vswhere -latest -products '*' -version '[18.0,19.0)' `
            -requires 'Microsoft.Component.MSBuild' 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' `
            -property installationPath)
        foreach ($location in $locations) {
            if (-not $location) { continue }
            $path = Join-Path $location 'MSBuild\Current\Bin\MSBuild.exe'
            if (Test-Path -LiteralPath $path -PathType Leaf) {
                return $path
            }
        }
    }
    return $null
}

function Install-BuildTools {
    if ($NoToolInstall) {
        throw 'Visual Studio 2026 C++ Build Tools not found. Install the Desktop development with C++ workload and rerun build.bat.'
    }

    $bootstrapper = Join-Path $env:TEMP 'wm-vs2026-buildtools.exe'
    Log 'Visual Studio 2026 x86 C++ tools are missing.'
    Log 'Downloading Microsoft VS 2026 Build Tools. This is a large, one-time install that may ask for administrator approval.'
    $uri = 'https://aka.ms/vs/stable/vs_buildtools.exe'
    Invoke-WebRequest -Uri $uri -UseBasicParsing -OutFile $bootstrapper -TimeoutSec 300
    try {
        $signature = Get-AuthenticodeSignature -LiteralPath $bootstrapper
        if ($signature.Status -ne 'Valid' -or
            -not $signature.SignerCertificate -or
            $signature.SignerCertificate.Subject -notmatch 'Microsoft') {
            throw 'Downloaded VS installer is not validly signed by Microsoft. Aborting.'
        }
        & $bootstrapper '--passive' '--wait' '--norestart' '--add' `
            'Microsoft.VisualStudio.Workload.VCTools' '--includeRecommended'
        $result = $LASTEXITCODE
        if ($result -eq 3010 -or $result -eq 1641) {
            throw 'Build Tools installation requires a restart. Restart Windows and run build.bat again.'
        }
        if ($result -ne 0) {
            throw "Visual Studio Build Tools setup failed with exit code $result. See Visual Studio Installer logs."
        }
    }
    finally {
        Remove-Item -LiteralPath $bootstrapper -Force -ErrorAction SilentlyContinue
    }
}

try {
    if ($env:OS -ne 'Windows_NT') {
        throw 'This ASI requires Windows and Visual Studio 2026 for compilation.'
    }
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

    if ($Package -and $Configuration -ne 'Release') {
        throw '-Package can only be used with the Release configuration.'
    }

    Log 'Checking project dependencies ...'
    # Source ZIPs do not retain empty external/ directories.
    New-Item -ItemType Directory -Path $externalDir -Force | Out-Null
    Restore-Repository 'external\IniReader' 'ThirteenAG/IniReader' 'IniReader.h'
    Restore-Repository 'external\hooking' 'ThirteenAG/Hooking.Patterns' 'Hooking.Patterns.h'
    Restore-Repository 'external\injector' 'ThirteenAG/injector' 'include\injector\injector.hpp'
    Restore-SafetyHook

    Require-File 'external\IniReader\mINI\src\mini\ini.h'
    Require-File 'external\hooking\Hooking.Patterns.cpp'
    Require-File 'external\injector\zydis\Zydis.h'
    Require-File 'external\injector\zydis\Zydis.c'
    Require-File 'external\injector\safetyhook\include\safetyhook.hpp'
    Require-File 'premake5.exe'

    $msbuild = Find-MSBuild
    if (-not $msbuild) {
        Install-BuildTools
        $msbuild = Find-MSBuild
        if (-not $msbuild) {
            throw 'Could not find VS 2026 MSBuild with x86 C++ tools after installation. Check Visual Studio Installer and rerun.'
        }
    }
    Log "Using MSBuild: $msbuild"

    Push-Location $projectRoot
    try {
        Log 'Generating the Visual Studio 2026 solution ...'
        & (Join-Path $projectRoot 'premake5.exe') 'vs2026'
        if ($LASTEXITCODE -ne 0) { throw "Premake failed with exit code $LASTEXITCODE" }

        $solution = Join-Path $projectRoot 'build\III.VC.SA.WindowedMode.slnx'
        if (-not (Test-Path -LiteralPath $solution -PathType Leaf)) {
            throw "Premake did not create $solution. Check that premake5.exe supports vs2026."
        }

        Log "Compiling $Configuration / Win32 ..."
        & $msbuild $solution '/m' "/p:Configuration=$Configuration" '/p:Platform=Win32' '/nologo' '/v:minimal'
        if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE" }

        $outDir = if ($Configuration -eq 'Release') { 'data' } else { "data\$Configuration" }
        $outFile = Join-Path $projectRoot (Join-Path $outDir 'III.VC.SA.WindowedMode.asi')
        if (-not (Test-Path -LiteralPath $outFile -PathType Leaf)) {
            throw "Build returned success but the expected .asi is missing: $outFile"
        }

        if ($Package) {
            Log 'Packaging release ZIP ...'
            & (Join-Path $projectRoot 'data\release.bat')
            if ($LASTEXITCODE -ne 0) { throw "Release packaging failed with exit code $LASTEXITCODE" }
        }

        Log "SUCCESS - $outFile"
        if ($Package) {
            Log ("Package: " + (Join-Path $projectRoot 'data\III.VC.SA.WindowedMode.zip'))
        }
    }
    finally {
        Pop-Location
    }
    exit 0
}
catch {
    Write-Host ('[build] ERROR: ' + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
