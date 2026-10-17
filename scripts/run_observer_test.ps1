param(
    [string]$Races = "Protoss,Zerg",
    [int]$Speed = 8,
    [int]$WaitSeconds = 25,
    [string]$ScreenshotFile = "c:\Projects\StarCraft\artifacts\observer_pz.png",
    [string]$LogFile = "c:\Projects\StarCraft\artifacts\observer_run.log"
)

$env:STARCRAFT_AI_OBSERVER_MATCH = "1"
$env:STARCRAFT_AI_OBSERVER_SPEED = "$Speed"
$env:STARCRAFT_AI_DIFFICULTY = "Medium"
$env:STARCRAFT_AI_OBSERVER_RACES = $Races
$env:STARCRAFT_DEBUG_HARVEST = "1"
$env:STARCRAFT_DEBUG_PATHING = "1"

if (!(Test-Path "c:\Projects\StarCraft\artifacts")) {
    New-Item -ItemType Directory -Path "c:\Projects\StarCraft\artifacts" -Force | Out-Null
}

$argsList = @("--test-map", "`"assets/maps/(2)The Small Divide.scm`"")
$proc = Start-Process -FilePath "c:\Projects\StarCraft\build\StarCraft.exe" -ArgumentList $argsList -WorkingDirectory "c:\Projects\StarCraft" -PassThru -RedirectStandardOutput $LogFile -RedirectStandardError "c:\Projects\StarCraft\artifacts\observer_err.log"

Write-Host "Started observer process with PID $($proc.Id)... waiting $WaitSeconds seconds..."
Start-Sleep -Seconds $WaitSeconds

try {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bmp = New-Object System.Drawing.Bitmap($bounds.Width, $bounds.Height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
    $bmp.Save($ScreenshotFile, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose()
    $bmp.Dispose()
    Write-Host "Screenshot saved to $ScreenshotFile"
} catch {
    Write-Host "Screenshot capture failed: $_"
}

if ($proc -and -not $proc.HasExited) {
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    Write-Host "Stopped observer process PID $($proc.Id)"
}
