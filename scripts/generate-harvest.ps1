# generate-harvest.ps1
# ==================================================================
# Generates a WiX v4 fragment file (DocuSearch.Harvest.wxs) that
# <ComponentGroupRef Id="HarvestedFiles" /> in DocuSearch.wxs can
# pull in to bundle every file in the windeployqt output directory
# (Qt DLLs, plugins, themes, etc.) into the MSI.
#
# This replaces `wix heat dir` because in WiX v4.0.5 `heat` is not
# a built-in subcommand of the `wix` CLI (it requires installing
# the WixToolset.Heat.wixext extension separately, which adds an
# extra failure surface on CI). PowerShell enumeration is reliable.
#
# Excludes (already declared in main DocuSearch.wxs or unwanted):
#   * DocuSearch.exe   — declared as <File Id="DocuSearchExe">
#   * tst_*.exe        — CTest unit-test binaries
#   *.pdb, *.exp, *.lib, *.ilk — build artifacts, never shipped
#
# Output: installer/DocuSearch.Harvest.wxs
#
# Usage:
#   pwsh generate-harvest.ps1
#   pwsh generate-harvest.ps1 -BuildDir build/bin/Release `
#       -OutputFile installer/DocuSearch.Harvest.wxs
# ==================================================================

[CmdletBinding()]
param(
    [string]$BuildDir      = "build\bin\Release",
    [string]$OutputFile    = "installer\DocuSearch.Harvest.wxs",
    [string]$RootDirId     = "BinFolder",
    # Use SINGLE QUOTES — this is a literal WiX preprocessor variable
    # reference $(var.BuildOutputDir), NOT a PowerShell subexpression.
    [string]$SourceVar     = '$(var.BuildOutputDir)'
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $BuildDir)) {
    throw "Build directory not found: $BuildDir"
}

# Normalize BuildDir to an absolute path so relative file paths
# can be computed correctly.
$BuildDirAbs = (Resolve-Path $BuildDir).Path

# Collect all files recursively. We use -File to skip directories.
$allFiles = Get-ChildItem -Path $BuildDirAbs -File -Recurse

# Filter out files we don't want in the installer.
$excludePatterns = @(
    'DocuSearch\.exe$'    # already in main wxs
    '^tst_'               # CTest binaries
    '\.pdb$'              # debug symbols
    '\.exp$'              # export tables
    '\.lib$'              # static libs
    '\.ilk$'              # incremental link files
    '\.lastcodeanalysissucceeded.xml$'
    '\.iobj$'             # intermediate object files (LTCG)
    '\.ipdb$'             # intermediate PDBs (LTCG)
)
$filteredFiles = $allFiles | Where-Object {
    $name = $_.Name
    $skip = $false
    foreach ($p in $excludePatterns) {
        if ($name -match $p) { $skip = $true; break }
    }
    -not $skip
}

if ($filteredFiles.Count -eq 0) {
    throw "No files to harvest in $BuildDir (after excluding patterns)."
}

Write-Host "=== Harvesting $($filteredFiles.Count) files from $BuildDir ==="

# Helper: deterministic GUID from a string (SHA-256 -> first 16 bytes -> GUID).
# Required so re-runs produce identical GUIDs (MSI tracks components by GUID).
#
# Note: we deliberately do NOT call `Add-Type -AssemblyName System.Security`
# or `System.Text` here. In PowerShell 7 (Core) those types are already
# available by default, and `Add-Type -AssemblyName` in PS 7 looks for a
# literal DLL file on disk (which fails with "Cannot find path ... .dll").
function Get-StableGuid {
    param([string]$seed)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $hash = $sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($seed.ToLowerInvariant()))
    $guidBytes = New-Object 'byte[]' 16
    [Array]::Copy($hash, $guidBytes, 16)
    # RFC 4122 v3 variant bits (set variant to 10xx and version to 3)
    $guidBytes[6] = ($guidBytes[6] -band 0x0F) -bor 0x30
    $guidBytes[8] = ($guidBytes[8] -band 0x3F) -bor 0x80
    return ([Guid]$guidBytes).ToString("D").ToUpperInvariant()
}

# Helper: sanitize a relative path into a WiX-friendly identifier.
# WiX Ids must match [A-Za-z_][A-Za-z0-9_.]*
function ConvertTo-WixId {
    param([string]$prefix, [string]$path)
    # Replace any non-identifier character with underscore
    $clean = $path -replace '[^A-Za-z0-9_.]', '_'
    return "${prefix}_$clean"
}

# Collect unique directory paths (relative to BuildDir) to declare them
# as <Directory> elements. The root directory itself is NOT declared here
# (it's already BinFolder in the main wxs).
$dirIds = @{}
$dirIdCounter = 0

# Build a map: relative dir path -> list of files in that dir.
$filesByDir = @{}

foreach ($f in $filteredFiles) {
    $relPath = $f.FullName.Substring($BuildDirAbs.Length).TrimStart('\', '/')
    # Use forward slashes for cross-platform safety in the XML source attr
    $relPathFwd = $relPath -replace '\\', '/'
    $relDir = [System.IO.Path]::GetDirectoryName($relPathFwd)
    if (-not $relDir) { $relDir = "" }

    if (-not $filesByDir.ContainsKey($relDir)) {
        $filesByDir[$relDir] = @()
    }
    $filesByDir[$relDir] += [PSCustomObject]@{
        File     = $f
        RelPath  = $relPathFwd
        RelDir   = $relDir
    }

    # Declare subdirectory IDs (skip empty relDir = root)
    if ($relDir -and -not $dirIds.ContainsKey($relDir)) {
        $dirIdCounter++
        $dirIds[$relDir] = "${RootDirId}_$dirIdCounter"
    }
}

# Build the directory tree. We need to nest <Directory> elements properly
# according to the path hierarchy. We sort dirs by depth (shallowest first)
# and then alphabetically so the output is deterministic.
# Note: each Sort-Object criterion MUST be a script block (or a property
# name string). A bare `$_` is not valid here - PowerShell 7 rejects it
# with "The value of a parameter was null".
$sortedDirs = $dirIds.Keys | Sort-Object { ($_ -split '/').Count }, { $_ }

# Map: rel dir -> its parent rel dir (or "" for top-level subdirs)
function Get-ParentDir {
    param([string]$dir)
    if (-not $dir) { return $null }
    $parts = $dir -split '/'
    if ($parts.Count -le 1) { return "" }
    return ($parts[0..($parts.Count - 2)] -join '/')
}

# Begin XML generation
$xml = [System.Text.StringBuilder]::new()
[void]$xml.AppendLine('<?xml version="1.0" encoding="UTF-8"?>')
[void]$xml.AppendLine('<!--')
[void]$xml.AppendLine('  DocuSearch.Harvest.wxs - AUTO-GENERATED by scripts/generate-harvest.ps1')
[void]$xml.AppendLine("  Generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ssZ')")
[void]$xml.AppendLine("  Source dir: $BuildDir")
[void]$xml.AppendLine("  Files harvested: $($filteredFiles.Count)")
[void]$xml.AppendLine('-->')
[void]$xml.AppendLine('<Wix xmlns="http://wixtoolset.org/schemas/v4/wxs">')
[void]$xml.AppendLine('  <Fragment>')
[void]$xml.AppendLine("    <DirectoryRef Id=`"$RootDirId`">")

# Emit subdirectory declarations. We use a recursive approach but emit
# them flat with parent tracking — for our use case (max 2 levels deep)
# a simple flat list with explicit nesting is fine.
# To support arbitrary depth, we group by parent.
$emittedDirs = @{}
function Emit-Dir($dir, $indent) {
    if ($emittedDirs.ContainsKey($dir)) { return }
    $parent = Get-ParentDir $dir
    if ($parent -eq $null) { return } # this is the root, skip
    # Ensure parent is emitted first
    if ($parent -ne "" -and -not $emittedDirs.ContainsKey($parent)) {
        Emit-Dir $parent $indent
    }
    $dirName = Split-Path $dir -Leaf
    $dirId = $dirIds[$dir]
    [void]$xml.AppendLine("${indent}<Directory Id=`"$dirId`" Name=`"$dirName`" />")
    $emittedDirs[$dir] = $true
}

foreach ($d in $sortedDirs) {
    Emit-Dir $d "      "
}

[void]$xml.AppendLine('    </DirectoryRef>')
[void]$xml.AppendLine('')

# Now emit all Components (one per file). Each Component is in its file's
# parent directory. For root-level files, Directory="$RootDirId"; for
# subdirectory files, Directory="<subdir id>".
$componentIds = @()

foreach ($dirKey in ($filesByDir.Keys | Sort-Object)) {
    foreach ($entry in $filesByDir[$dirKey]) {
        $relPath = $entry.RelPath
        $fileId = ConvertTo-WixId -prefix "fil" -path $relPath
        $compId = ConvertTo-WixId -prefix "cmp" -path $relPath
        $guid   = Get-StableGuid -seed $relPath
        $source = "$SourceVar\$($relPath -replace '/', '\')"

        if ($dirKey -eq "") {
            $dirAttr = $RootDirId
        } else {
            $dirAttr = $dirIds[$dirKey]
        }

        [void]$xml.AppendLine('    <Component Id="{0}" Guid="{1}" Directory="{2}" Bitness="always64">' -f $compId, $guid, $dirAttr)
        [void]$xml.AppendLine('      <File Id="{0}" Source="{1}" KeyPath="yes" />' -f $fileId, $source)
        [void]$xml.AppendLine('    </Component>')
        $componentIds += $compId
    }
}

[void]$xml.AppendLine('')
[void]$xml.AppendLine('    <ComponentGroup Id="HarvestedFiles">')
foreach ($cid in $componentIds) {
    [void]$xml.AppendLine("      <ComponentRef Id=`"$cid`" />")
}
[void]$xml.AppendLine('    </ComponentGroup>')
[void]$xml.AppendLine('  </Fragment>')
[void]$xml.AppendLine('</Wix>')

# Write output
$outDir = Split-Path $OutputFile -Parent
if ($outDir -and -not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}
$xml.ToString() | Out-File -FilePath $OutputFile -Encoding UTF8 -NoNewline
Add-Content -Path $OutputFile -Value "`n" # trailing newline

Write-Host "=== Wrote $OutputFile ==="
Write-Host "Total components: $($componentIds.Count)"
Write-Host "Total subdirectories: $($dirIds.Count)"
Write-Host "First 40 lines of output:"
Get-Content $OutputFile | Select-Object -First 40 | ForEach-Object { Write-Host "  $_" }
