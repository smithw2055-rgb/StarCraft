$cs = @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public class WinEnum {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);

    [DllImport("user32.dll")]
    public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    public static List<string> FindWindowsForPid(uint targetPid) {
        var list = new List<string>();
        EnumWindows((hWnd, lParam) => {
            uint pid;
            GetWindowThreadProcessId(hWnd, out pid);
            if (pid == targetPid) {
                StringBuilder sb = new StringBuilder(256);
                GetWindowText(hWnd, sb, 256);
                list.Add(hWnd.ToString("X") + " visible=" + IsWindowVisible(hWnd) + " title=" + sb.ToString());
            }
            return true;
        }, IntPtr.Zero);
        return list;
    }
}
'@

try {
    Add-Type -TypeDefinition $cs -ErrorAction Stop
} catch {
    Write-Warning $_
}

$proc = Get-Process StarCraft -ErrorAction SilentlyContinue | Select-Object -First 1
if ($proc) {
    Write-Output "Found StarCraft PID=$($proc.Id)"
    [WinEnum]::FindWindowsForPid($proc.Id)
} else {
    Write-Output "No StarCraft process found."
}
