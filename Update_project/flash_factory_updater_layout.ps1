param(
    [string]$Port = "",
    [int]$Baud = 460800,
    [switch]$UpdaterOnly,
    [switch]$EraseFlash
)

$ErrorActionPreference = "Stop"

$projectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $projectDir "..")).Path
$idfExport = "C:\esp\v5.5.4\esp-idf\export.ps1"

if (Test-Path -LiteralPath $idfExport) {
    . $idfExport | Out-Null
}

function Resolve-RequiredFile([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required file not found: $path"
    }
    return (Resolve-Path -LiteralPath $path).Path
}

function Convert-SizeLiteral([string]$value) {
    $text = $value.Trim()
    if ($text.StartsWith("0x", [System.StringComparison]::OrdinalIgnoreCase)) {
        return [Convert]::ToInt64($text.Substring(2), 16)
    }
    if ($text -match '^(\d+)([KkMmGg])?$') {
        $size = [int64]$matches[1]
        $unit = $matches[2]
        if ([string]::IsNullOrWhiteSpace($unit)) {
            return $size
        }
        switch ($unit.ToUpperInvariant()) {
            "K" { return $size * 1KB }
            "M" { return $size * 1MB }
            "G" { return $size * 1GB }
        }
    }
    throw "Unable to parse size literal: $value"
}

function Format-HexOffset([long]$value) {
    return "0x{0:X}" -f $value
}

function Read-PartitionTable([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Partition table not found: $path"
    }

    $rows = @()
    foreach ($line in Get-Content -LiteralPath $path -Encoding UTF8) {
        $clean = ($line -split "#", 2)[0].Trim()
        if ([string]::IsNullOrWhiteSpace($clean)) {
            continue
        }
        $cols = @($clean.Split(",") | ForEach-Object { $_.Trim() })
        if ($cols.Count -lt 5) {
            continue
        }

        $rows += [pscustomobject]@{
            name = $cols[0]
            type = $cols[1]
            subtype = $cols[2]
            offset = Convert-SizeLiteral $cols[3]
            size = Convert-SizeLiteral $cols[4]
        }
    }
    return $rows
}

function Get-PartitionByName($partitions, [string]$name) {
    $partition = @($partitions | Where-Object { $_.name -eq $name })
    if ($partition.Count -ne 1) {
        throw "Partition table does not contain one unique partition named: $name"
    }
    return $partition[0]
}

function Get-AutoPort {
    $raw = @(& python -c "import serial.tools.list_ports as p; print('\n'.join(f'{x.device}`t{x.description}`t{x.hwid}' for x in p.comports()))")
    $rows = @($raw | Where-Object { $_ -and $_.Trim().Length -gt 0 })
    $ports = @()
    foreach ($row in $rows) {
        $parts = $row -split "`t", 3
        if ($parts.Count -lt 3) {
            continue
        }
        $haystack = ($parts[1] + " " + $parts[2]).ToLowerInvariant()
        if ($haystack -match "usb|jtag|serial|cp210|ch340|wch|espressif" -and $haystack -notmatch "acpi\\pnp0501") {
            $ports += $parts[0]
        }
    }
    if ($ports.Count -eq 1) {
        return $ports[0]
    }
    if ($rows.Count -eq 0) {
        throw "No serial port found. Pass -Port COMx."
    }
    if ($ports.Count -eq 0) {
        throw "No ESP USB serial port found. Visible ports: $($rows -join '; '). Pass -Port COMx after confirming the device port."
    }
    throw "Multiple ESP-like serial ports found: $($ports -join ', '). Pass -Port COMx."
}

if ([string]::IsNullOrWhiteSpace($Port)) {
    $Port = Get-AutoPort
}

$updaterBuild = Join-Path $projectDir "build_updater"
$mainBuild = Join-Path $repoRoot "build"
$partitionCsv = Join-Path $projectDir "partitions.csv"
$partitions = @(Read-PartitionTable $partitionCsv)
$otadataPartition = Get-PartitionByName $partitions "otadata"
$factoryPartition = Get-PartitionByName $partitions "factory"
$mainAppPartition = Get-PartitionByName $partitions "main_app"

$bootloader = Resolve-RequiredFile (Join-Path $updaterBuild "bootloader\bootloader.bin")
$partitionTable = Resolve-RequiredFile (Join-Path $updaterBuild "partition_table\partition-table.bin")
$otaData = Resolve-RequiredFile (Join-Path $updaterBuild "ota_data_initial.bin")
$updaterApp = Resolve-RequiredFile (Join-Path $updaterBuild "tab5_factory_updater.bin")

$flashFiles = @(
    "0x2000", $bootloader,
    "0x8000", $partitionTable,
    (Format-HexOffset $otadataPartition.offset), $otaData,
    (Format-HexOffset $factoryPartition.offset), $updaterApp
)

if (-not $UpdaterOnly) {
    $mainApp = Resolve-RequiredFile (Join-Path $mainBuild "M5stack_tab5_ssh_client.bin")
    $flashFiles += @((Format-HexOffset $mainAppPartition.offset), $mainApp)
}

if ($EraseFlash) {
    & python -m esptool --chip esp32p4 -p $Port -b $Baud --before default_reset --after hard_reset erase_flash
}

& python -m esptool --chip esp32p4 -p $Port -b $Baud --before default_reset --after hard_reset write_flash `
    --flash_mode dio --flash_size 16MB --flash_freq 80m @flashFiles
