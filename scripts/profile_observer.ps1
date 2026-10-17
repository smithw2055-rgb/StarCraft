$env:STARCRAFT_AI_OBSERVER_MATCH = "1"
$env:STARCRAFT_AI_DIFFICULTY = "Medium"
$env:STARCRAFT_AI_OBSERVER_RACES = "Terran,Protoss"

$mapPath = "assets/maps/(2)The Small Divide.scm"
$proc = Start-Process -FilePath "build\StarCraft.exe" -ArgumentList @("--test-map", "`"$mapPath`"") -PassThru -RedirectStandardOutput "artifacts/profile_out.log" -RedirectStandardError "artifacts/profile_err.log"

Write-Host "Process started: $($proc.Id)"
for ($i = 0; $i -lt 15; $i++) {
    Start-Sleep -Seconds 2
    $p = Get-Process -Id $proc.Id -ErrorAction SilentlyContinue
    if ($p) {
        $ws = [math]::Round($p.WorkingSet64 / 1MB, 1)
        $priv = [math]::Round($p.PrivateMemorySize64 / 1MB, 1)
        Write-Host "Sec $(($i+1)*2): WorkingSet=${ws}MB Private=${priv}MB"
    } else {
        Write-Host "Process exited early"
        break
    }
}

if ($proc -and -not $proc.HasExited) {
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    Write-Host "Stopped process $($proc.Id)"
}
