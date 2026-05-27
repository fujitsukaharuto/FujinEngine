# compile_shaders.ps1
# Compiles all HLSL shaders in Resource/Shaders/ to Resource/Shaders/Compiled/
# using the Windows SDK DXC (SM 6.0+).
# Called automatically as a pre-build event from FujinEngine.vcxproj.
#
# Usage:
#   compile_shaders.ps1 -ProjectDir <path> -Configuration <Debug|Release>

param(
    [string]$ProjectDir    = $PSScriptRoot,
    [string]$Configuration = "Debug"
)

$ProjectDir    = $ProjectDir.TrimEnd('\', '/')
$ShaderSrcDir  = Join-Path $ProjectDir "Resource\Shaders"
$ShaderOutDir  = Join-Path $ProjectDir "Resource\Shaders\Compiled"

# ── Locate dxc.exe ─────────────────────────────────────────────────────────────
$dxcExe = $null

# 1. Windows SDK path (preferred)
try {
    $kitsRoot = (Get-ItemProperty -Path "HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots" `
                                  -Name "KitsRoot10" -ErrorAction Stop).KitsRoot10
    $candidates = Get-ChildItem -Path (Join-Path $kitsRoot "bin") -Filter "dxc.exe" -Recurse `
                                -ErrorAction SilentlyContinue |
                  Where-Object { $_.FullName -match "x64" } |
                  Sort-Object FullName -Descending
    if ($candidates) { $dxcExe = $candidates[0].FullName }
} catch {}

# 2. PATH fallback
if (!$dxcExe) {
    $found = Get-Command "dxc.exe" -ErrorAction SilentlyContinue
    if ($found) { $dxcExe = $found.Source }
}

if (!$dxcExe) {
    Write-Error "[ShaderCompiler] dxc.exe not found. Install Windows SDK 10.0.20348.0+ or add DXC to PATH."
    exit 1
}

Write-Host "[ShaderCompiler] DXC: $dxcExe"

# ── Ensure output directory ────────────────────────────────────────────────────
if (!(Test-Path $ShaderOutDir)) {
    New-Item -ItemType Directory -Path $ShaderOutDir | Out-Null
}

# ── Determine shader target from filename extension ────────────────────────────
function Get-ShaderTarget([string]$filename) {
    if ($filename -match '\.VS\.hlsl$') { return "vs_6_0" }
    if ($filename -match '\.PS\.hlsl$') { return "ps_6_0" }
    if ($filename -match '\.CS\.hlsl$') { return "cs_6_0" }
    if ($filename -match '\.GS\.hlsl$') { return "gs_6_0" }
    if ($filename -match '\.HS\.hlsl$') { return "hs_6_0" }
    if ($filename -match '\.DS\.hlsl$') { return "ds_6_0" }
    return $null
}

# ── Compile each shader ────────────────────────────────────────────────────────
$failed   = 0
$compiled = 0

Get-ChildItem $ShaderSrcDir -Filter "*.hlsl" | ForEach-Object {
    $target = Get-ShaderTarget $_.Name
    if (!$target) { return }  # skip files without a recognised stage suffix

    $csoName = [IO.Path]::GetFileNameWithoutExtension($_.Name) + ".cso"
    $csoFile = Join-Path $ShaderOutDir $csoName

    # Incremental: skip if .cso is newer than .hlsl
    if ((Test-Path $csoFile) -and ($_.LastWriteTime -le (Get-Item $csoFile).LastWriteTime)) {
        return
    }

    Write-Host "[ShaderCompiler] $($_.Name) -> $csoName  [$target]"

    $optArgs = if ($Configuration -eq "Debug") { @("-Zi", "-Od") } else { @("-O3") }

    & $dxcExe -T $target -E main @optArgs -Fo $csoFile $_.FullName 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Error "[ShaderCompiler] FAILED: $($_.Name)"
        $failed++
    } else {
        $compiled++
    }
}

if ($failed -gt 0) {
    Write-Error "[ShaderCompiler] $failed shader(s) failed to compile."
    exit 1
}

if ($compiled -gt 0) { Write-Host "[ShaderCompiler] $compiled shader(s) compiled." }
else                  { Write-Host "[ShaderCompiler] All shaders up-to-date." }
