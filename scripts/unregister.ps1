$root = 'HKCU:\Software\Khronos\OpenXR\1\ApiLayers\Implicit'
$manifest = 'XR_APILAYER_NIGHTEYES_tf2vr_better_bindings.json'
$removed = 0
if (Test-Path $root) {
    foreach ($name in (Get-Item $root).GetValueNames()) {
        if ([System.IO.Path]::GetFileName($name) -ieq $manifest) {
            Remove-ItemProperty -Path $root -Name $name
            Write-Output "Removed $name"
            $removed++
        }
    }
}
if ($removed -eq 0) {
    Write-Output "$manifest was not registered"
}
