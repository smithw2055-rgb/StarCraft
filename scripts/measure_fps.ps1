$env:STARCRAFT_AI_OBSERVER_MATCH = "1"
$env:STARCRAFT_AI_DIFFICULTY = "Medium"
$env:STARCRAFT_AI_OBSERVER_RACES = "Terran,Protoss"
$env:STARCRAFT_LOG_FPS = "1"

$mapPath = "assets/maps/(2)The Small Divide.scm"
$proc = Start-Process -FilePath "build\StarCraft.exe" -ArgumentList @("--test-map", "`"$mapPath`"") -PassThru -RedirectStandardOutput "artifacts/fps_measure.log"
Start-Sleep -Seconds 10
if ($proc -and -not $proc.HasExited) {
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
}
Get-Content "artifacts/fps_measure.log" | Select-String "FPS" | Select-Object -Last 10
