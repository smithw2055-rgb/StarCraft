Add-Type -AssemblyName System.Drawing
$bmp = [System.Drawing.Bitmap]::FromFile('c:\Projects\StarCraft\screenshot_campaign.png')
$w = $bmp.Width
$h = $bmp.Height
$samples = @()
for ($y = 0; $y -lt $h; $y += 5) {
    for ($x = 0; $x -lt $w; $x += 5) {
        $p = $bmp.GetPixel($x, $y)
        if ($p.G -gt 150 -and $p.R -lt 50 -and $p.B -lt 50) {
            $samples += "($($p.R), $($p.G), $($p.B)) at ($x, $y)"
            if ($samples.Count -ge 5) { break }
        }
    }
    if ($samples.Count -ge 5) { break }
}
$bmp.Dispose()
Write-Output "Found: $($samples.Count) samples: $($samples -join '; ')"
