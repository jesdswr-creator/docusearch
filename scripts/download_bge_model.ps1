<#
.SYNOPSIS
    Download the BGE Small EN v1.5 model for semantic search.

.DESCRIPTION
    Downloads three files from HuggingFace into ./models/bge-small-en-v1.5/:
      - model.onnx      (~50 MB, the actual ONNX model)
      - tokenizer.json  (~470 KB, BERT WordPiece vocab)
      - config.json     (~480 B, model metadata)

    These are required for semantic search. Without them, DocuSearch
    falls back to keyword-only (FTS5) search — no crash, no error popup.

.PARAMETER TargetDir
    Root directory where models/ should be created. Defaults to the
    directory containing this script's repo root.

.EXAMPLE
    .\scripts\download_bge_model.ps1
#>

[CmdletBinding()]
param(
    [string]$TargetDir = ""
)

$ErrorActionPreference = "Stop"

function Write-Step  { param($msg) Write-Host "[bge] $msg" -ForegroundColor Cyan }
function Write-OK    { param($msg) Write-Host "[bge] $msg" -ForegroundColor Green }
function Write-Err   { param($msg) Write-Host "[bge] $msg" -ForegroundColor Red }

# Locate repo root (parent of the scripts/ directory).
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot  = Split-Path -Parent $ScriptDir
if (-not $TargetDir) { $TargetDir = $RepoRoot }

$ModelDir = Join-Path $TargetDir "models\bge-small-en-v1.5"
Write-Step "Target directory: $ModelDir"

if (-not (Test-Path $ModelDir)) {
    New-Item -ItemType Directory -Path $ModelDir -Force | Out-Null
}

# ── Files to download ──────────────────────────────────────
$Files = @(
    @{
        Name = "model.onnx"
        Url  = "https://huggingface.co/BAAI/bge-small-en-v1.5/resolve/main/onnx/model.onnx"
        MinSize = 40MB
    },
    @{
        Name = "tokenizer.json"
        Url  = "https://huggingface.co/BAAI/bge-small-en-v1.5/resolve/main/tokenizer.json"
        MinSize = 100KB
    },
    @{
        Name = "config.json"
        Url  = "https://huggingface.co/BAAI/bge-small-en-v1.5/resolve/main/config.json"
        MinSize = 100B
    },
    @{
        Name = "vocab.txt"
        Url  = "https://huggingface.co/BAAI/bge-small-en-v1.5/resolve/main/vocab.txt"
        MinSize = 100KB
    }
)

$anyFailed = $false

foreach ($f in $Files) {
    $destPath = Join-Path $ModelDir $f.Name
    Write-Step "Downloading $($f.Name) ..."

    if (Test-Path $destPath) {
        $existingSize = (Get-Item $destPath).Length
        if ($existingSize -ge $f.MinSize) {
            Write-OK "$($f.Name) already exists ($('{0:N1}' -f ($existingSize/1MB)) MB) — skipping."
            continue
        }
        Write-Step "  Existing file is too small ($existingSize bytes) — re-downloading."
    }

    try {
        # Use -UseBasicParsing to avoid IE engine dependency.
        $ProgressPreference = 'SilentlyContinue'  # speed up Invoke-WebRequest
        Invoke-WebRequest -Uri $f.Url -OutFile $destPath -UseBasicParsing -ErrorAction Stop

        $size = (Get-Item $destPath).Length
        if ($size -lt $f.MinSize) {
            throw "Downloaded file is too small ($size bytes, expected >= $($f.MinSize) bytes)"
        }
        Write-OK "$($f.Name) downloaded ($('{0:N1}' -f ($size/1MB)) MB)"
    } catch {
        Write-Err "Failed to download $($f.Name): $($_.Exception.Message)"
        Write-Err "  URL: $($f.Url)"
        $anyFailed = $true
    }
}

# ── Final verification ─────────────────────────────────────
$modelOnnx = Join-Path $ModelDir "model.onnx"
if (-not (Test-Path $modelOnnx)) {
    Write-Err "model.onnx is missing — semantic search will NOT work."
    exit 1
}
$modelSize = (Get-Item $modelOnnx).Length
if ($modelSize -lt 40MB) {
    Write-Err "model.onnx is only $modelSize bytes (expected >= 40 MB)."
    Write-Err "The download may be corrupted. Delete it and re-run this script."
    exit 1
}

if ($anyFailed) {
    Write-Err "Some files failed to download. Semantic search may be partially functional."
    exit 1
}

Write-Host ""
Write-OK "All BGE model files downloaded successfully."
Write-OK "Semantic search is ready to use."
Write-Host ""
Write-Host "Files installed at: $ModelDir" -ForegroundColor White
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Cyan
Write-Host "  1. Restart DocuSearch"
Write-Host "  2. Click the brain icon (Semantic: OFF) in the search bar to toggle ON"
Write-Host "  3. Run a search — results will include semantic matches marked with a brain icon"
exit 0
