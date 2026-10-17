$env:STARCRAFT_AI_OBSERVER_MATCH = "1"
$env:STARCRAFT_AI_DIFFICULTY = "Medium"
$env:STARCRAFT_AI_OBSERVER_RACES = "Terran,Protoss"

$mapPath = "assets/maps/(2)The Small Divide.scm"
$shotPath = "artifacts/observer_optimized.png"

$proc = Start-Process -FilePath "build\StarCraft.exe" -ArgumentList @("--test-map", "`"$mapPath`"", "--screenshot", "`"$shotPath`"", "--screenshot-frame", "60") -PassThru
Start-Sleep -Seconds 5
if ($proc -and -not $proc.HasExited) {
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
}
Write-Host "Screenshot captured at $shotPath"
