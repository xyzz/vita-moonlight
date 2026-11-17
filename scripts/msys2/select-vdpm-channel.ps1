param(
    [Parameter(Mandatory = $true)]
    [string]$StagedManifest,
    [Parameter(Mandatory = $true)]
    [string]$SelectedManifest,
    [Parameter(Mandatory = $true)]
    [string]$ExpectedManifest
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$stagedPath = [IO.Path]::GetFullPath($StagedManifest)
$selectedPath = [IO.Path]::GetFullPath($SelectedManifest)
$expectedPath = [IO.Path]::GetFullPath($ExpectedManifest)

foreach ($path in @($stagedPath, $selectedPath, $expectedPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required channel manifest is missing: $path"
    }
}

$stagedDirectory = [IO.Path]::GetDirectoryName($stagedPath)
$selectedDirectory = [IO.Path]::GetDirectoryName($selectedPath)
if (-not [string]::Equals(
    $stagedDirectory,
    $selectedDirectory,
    [StringComparison]::OrdinalIgnoreCase
)) {
    throw "Staged and selected channel manifests must share a directory"
}

$stagedContent = [Convert]::ToBase64String([IO.File]::ReadAllBytes($stagedPath))
$expectedContent = [Convert]::ToBase64String([IO.File]::ReadAllBytes($expectedPath))
if ($stagedContent -cne $expectedContent) {
    throw "VDPM staged a channel manifest different from the selected snapshot"
}

$backupPath = Join-Path $selectedDirectory ("channel.json.backup." + [Guid]::NewGuid().ToString("N"))
[IO.File]::Replace($stagedPath, $selectedPath, $backupPath)

$selectedContent = [Convert]::ToBase64String([IO.File]::ReadAllBytes($selectedPath))
if ($selectedContent -cne $expectedContent) {
    [IO.File]::Replace($backupPath, $selectedPath, $stagedPath)
    throw "Selected VitaSDK channel manifest failed verification"
}

[IO.File]::Delete($backupPath)
Write-Output "Selected the refreshed VitaSDK channel manifest."
