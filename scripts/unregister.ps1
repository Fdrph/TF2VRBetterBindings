param(
    [string]$ManifestName = 'XR_APILAYER_NIGHTEYES_tf2vr_better_bindings.json',
    [switch]$Machine
)

$ErrorActionPreference = 'Stop'
$root = if ($Machine) { 'HKLM:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit' } else { 'HKCU:\Software\Khronos\OpenXR\1\ApiLayers\Implicit' }

if (-not (Test-Path $root)) {
    Write-Output "No OpenXR implicit layers are registered under $root"
    return
}

$key = Get-Item $root
$removed = 0
foreach ($name in $key.GetValueNames()) {
    if ([System.IO.Path]::GetFileName($name) -ieq $ManifestName) {
        Remove-ItemProperty -Path $root -Name $name
        Write-Output "Removed $name"
        $removed++
    }
}
if ($removed -eq 0) {
    Write-Output "$ManifestName was not registered under $root"
}
