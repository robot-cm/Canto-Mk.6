# resize4watch.ps1 — 把照片目录批量压成手表相册图（≤480px 长边，防大图 OOM）
# 用法:  .\resize4watch.ps1 D:\照片
# 输出:  simulator/build/fs/sdcard/ALBUM/  (模拟器 SD 卡相册目录)
param(
    [Parameter(Mandatory=$true)][string]$SourceDir,
    [int]$MaxEdge = 480,
    [string]$OutDir = ""
)

Add-Type -AssemblyName System.Drawing

if (-not (Test-Path $SourceDir)) { Write-Error "源目录不存在: $SourceDir"; exit 1 }
if ($OutDir -eq "") {
    $OutDir = Join-Path $PSScriptRoot "..\simulator\build\fs\sdcard\ALBUM"
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$exts = @(".png", ".jpg", ".jpeg")
$files = Get-ChildItem -Path $SourceDir -File | Where-Object { $exts -contains $_.Extension.ToLower() }
if ($files.Count -eq 0) { Write-Host "源目录无图片"; exit 0 }

$ok = 0; $skip = 0
foreach ($f in $files) {
    try {
        $img = [System.Drawing.Image]::FromFile($f.FullName)
        # 宽高必须在 Dispose 前缓存（dispose 后读属性会抛异常）
        $srcW = $img.Width
        $srcH = $img.Height
        $long = [Math]::Max($srcW, $srcH)
        if ($long -le $MaxEdge) {
            # 已达标直接复制，不二次压缩
            Copy-Item $f.FullName (Join-Path $OutDir $f.Name) -Force
            $img.Dispose()
            $skip++
            Write-Host ("  [拷] {0}  {1}x{2} (已达标)" -f $f.Name, $srcW, $srcH)
        } else {
            $ratio = $MaxEdge / $long
            $nw = [int][Math]::Round($srcW * $ratio)
            $nh = [int][Math]::Round($srcH * $ratio)
            $bmp = New-Object System.Drawing.Bitmap($nw, $nh)
            $g = [System.Drawing.Graphics]::FromImage($bmp)
            $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $g.DrawImage($img, 0, 0, $nw, $nh)
            $out = Join-Path $OutDir $f.Name
            if ($f.Extension.ToLower() -eq ".png") { $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png) }
            else { $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Jpeg) }
            $g.Dispose(); $bmp.Dispose(); $img.Dispose()
            $ok++
            Write-Host ("  [缩] {0}  {1}x{2} -> {3}x{4}" -f $f.Name, $srcW, $srcH, $nw, $nh)
        }
    } catch {
        Write-Host ("  [ERR] {0}: {1}" -f $f.Name, $_.Exception.Message)
    }
}
Write-Host ("完成: 缩放 $ok 张, 直拷 $skip 张 -> {0}" -f $OutDir)
