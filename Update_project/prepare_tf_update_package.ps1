param(
    [Parameter(Mandatory = $true)]
    [string]$TfRoot
)

$ErrorActionPreference = "Stop"

$projectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $projectDir "..")).Path
$mainApp = Join-Path $repoRoot "build\M5stack_tab5_ssh_client.bin"

if (-not (Test-Path -LiteralPath $mainApp)) {
    throw "Main app binary not found: $mainApp"
}
if (-not (Test-Path -LiteralPath $TfRoot)) {
    throw "TF root not found: $TfRoot"
}

$updateDir = Join-Path $TfRoot "update"
New-Item -ItemType Directory -Force -Path $updateDir | Out-Null

$targetBin = Join-Path $updateDir "update.bin"
Copy-Item -LiteralPath $mainApp -Destination $targetBin -Force

$request = [ordered]@{
    schema = 1
    action = "install_from_tf"
    project = "M5stack_tab5_ssh_client"
    model = "m5tab5"
    chip = "esp32p4"
    partition_scheme = "factory-updater-v1"
    target_partition = "main_app"
    firmware_path = "/sdcard/update/update.bin"
    firmware_size = (Get-Item -LiteralPath $targetBin).Length
}

$requestPath = Join-Path $updateDir "request.json"
$request | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $requestPath -Encoding UTF8

Write-Output "Prepared TF update package:"
Write-Output "  $targetBin"
Write-Output "  $requestPath"
