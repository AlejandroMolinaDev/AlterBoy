# Validates the Windows build: pluginval (VST3), Steinberg VST3 validator and
# clap-validator. Usable locally too, once the three tools are downloaded/built.
#
# Usage: validate_windows.ps1 -Artefacts <dir> -Pluginval <exe> -Vst3Validator <exe> -ClapValidator <exe>
param(
    [Parameter(Mandatory = $true)] [string] $Artefacts,
    [Parameter(Mandatory = $true)] [string] $Pluginval,
    [Parameter(Mandatory = $true)] [string] $Vst3Validator,
    [Parameter(Mandatory = $true)] [string] $ClapValidator
)

$ErrorActionPreference = 'Stop'

$vst3 = (Resolve-Path (Join-Path $Artefacts 'VST3\Metamorfo.vst3')).Path
$clap = (Resolve-Path (Join-Path $Artefacts 'CLAP\Metamorfo.clap')).Path

# Console tools: PowerShell waits for them and sets $LASTEXITCODE.
function Invoke-Console([string] $title, [scriptblock] $command) {
    Write-Host "::group::$title"
    & $command
    $code = $LASTEXITCODE
    Write-Host '::endgroup::'
    if ($code -ne 0) { throw "$title failed with exit code $code" }
}

# GUI-subsystem tools (pluginval.exe): PowerShell does NOT wait for them and
# $LASTEXITCODE stays empty, so start the process explicitly and wait.
function Invoke-Gui([string] $title, [string] $exe, [string[]] $arguments) {
    Write-Host "::group::$title"
    $out = New-TemporaryFile
    $err = New-TemporaryFile
    $process = Start-Process -FilePath $exe -ArgumentList $arguments -Wait -NoNewWindow -PassThru `
        -RedirectStandardOutput $out.FullName -RedirectStandardError $err.FullName
    Get-Content $out.FullName, $err.FullName | Write-Host
    Write-Host '::endgroup::'
    if ($process.ExitCode -ne 0) { throw "$title failed with exit code $($process.ExitCode)" }
}

Invoke-Gui 'pluginval VST3 (strictness 10)' $Pluginval @(
    '--strictness-level', '10', '--verbose', '--validate', "`"$vst3`"")
Invoke-Console 'Steinberg VST3 validator' { & $Vst3Validator $vst3 }
Invoke-Console 'clap-validator' { & $ClapValidator validate $clap }

Write-Host 'All Windows validations passed.'
