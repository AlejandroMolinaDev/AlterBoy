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

$vst3 = Join-Path $Artefacts 'VST3\Metamorfo.vst3'
$clap = Join-Path $Artefacts 'CLAP\Metamorfo.clap'

function Invoke-Checked([string] $title, [scriptblock] $command) {
    Write-Host "::group::$title"
    & $command
    $code = $LASTEXITCODE
    Write-Host '::endgroup::'
    if ($code -ne 0) { throw "$title failed with exit code $code" }
}

Invoke-Checked 'pluginval VST3 (strictness 10)' {
    & $Pluginval --strictness-level 10 --verbose --validate $vst3
}
Invoke-Checked 'Steinberg VST3 validator' { & $Vst3Validator $vst3 }
Invoke-Checked 'clap-validator' { & $ClapValidator validate $clap }

Write-Host 'All Windows validations passed.'
