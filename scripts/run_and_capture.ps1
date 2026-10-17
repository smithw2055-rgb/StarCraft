param(
    [string]$MapPath = "assets\locales\zhCN\Assets\campaign\Terran\Terran01\staredit\scenario.chk",
    [int]$WaitSeconds = 4,
    [string]$OutputFile = "c:\Projects\StarCraft\screenshot_campaign.png",
    [switch]$Demo = $false
)

$ErrorActionPreference = "Stop"

$argsList = @("--map", $MapPath)
if ($Demo) {
    $argsList += "--interaction-demo"
}

Write-Host "Launching StarCraft with map: $MapPath"
$proc = Start-Process -FilePath "c:\Projects\StarCraft\build\StarCraft.exe" -ArgumentList $argsList -WorkingDirectory "c:\Projects\StarCraft" -PassThru

Write-Host "Waiting $WaitSeconds seconds for initial render and gameplay..."
Start-Sleep -Seconds $WaitSeconds

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bmp = New-Object System.Drawing.Bitmap($bounds.Width, $bounds.Height)
$graphics = [System.Drawing.Graphics]::FromImage($bmp)
$graphics.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
$bmp.Save($OutputFile, [System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose()
$bmp.Dispose()

Write-Host "Screenshot saved to $OutputFile"
Get-Item $OutputFile | Select-Object FullName, Length
