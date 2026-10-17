$screenshotPath = "C:\Users\macx\.gemini\antigravity-ide\brain\ec0ad26f-92e0-469b-a7b2-e874333375e2\gameplay_view.png"
$argsList = @(
    "--map", "assets\locales\zhCN\Assets\campaign\Terran\Terran01\staredit\scenario.chk",
    "--screenshot", $screenshotPath,
    "--screenshot-frame", "45"
)
$proc = Start-Process -FilePath "$PSScriptRoot\build\StarCraft.exe" -ArgumentList $argsList -WorkingDirectory "$PSScriptRoot" -PassThru
Start-Sleep -Seconds 3
if ($proc -and -not $proc.HasExited) {
    Write-Host "StarCraft is running with PID $($proc.Id)"
} else {
    Write-Host "StarCraft exited with code $($proc.ExitCode)"
}
