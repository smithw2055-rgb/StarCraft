$env:STARCRAFT_MAP_PATH = "assets\maps\(3)Three Kingdoms.scm"
$env:STARCRAFT_AI_OBSERVER_MATCH = "1"
$env:STARCRAFT_AI_OBSERVER_SPEED = "4"
$env:STARCRAFT_AI_OBSERVER_RACES = "Terran,Zerg,Protoss"
$env:STARCRAFT_DEV_START_INGAME = "1"
Remove-Item Env:\STARCRAFT_EXIT_AFTER_FRAME -ErrorAction SilentlyContinue
Remove-Item Env:\STARCRAFT_SCREENSHOT_PATH -ErrorAction SilentlyContinue

$argList = @("--gui-hud") + $args
Start-Process -FilePath "$PSScriptRoot\build\StarCraft.exe" -ArgumentList $argList -WorkingDirectory "$PSScriptRoot"
Write-Host "StarCraft Release 4x Speed 3-Race AI Match launched successfully!"
