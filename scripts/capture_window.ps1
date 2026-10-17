Add-Type -ReferencedAssemblies "System.Drawing" -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public class WinCapture {
    [DllImport("user32.dll")]
    public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBkgnd, uint nFlags);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    public static bool Capture(string windowTitle, string outputPath) {
        IntPtr hwnd = FindWindow(null, windowTitle);
        if (hwnd == IntPtr.Zero) {
            Console.WriteLine("Window not found: " + windowTitle);
            return false;
        }

        RECT rc;
        if (!GetWindowRect(hwnd, out rc)) {
            Console.WriteLine("GetWindowRect failed");
            return false;
        }
        int width = rc.Right - rc.Left;
        int height = rc.Bottom - rc.Top;
        if (width <= 0 || height <= 0) {
            Console.WriteLine("Invalid size: " + width + "x" + height);
            return false;
        }

        using (Bitmap bmp = new Bitmap(width, height)) {
            using (Graphics g = Graphics.FromImage(bmp)) {
                IntPtr hdc = g.GetHdc();
                try {
                    bool res = PrintWindow(hwnd, hdc, 2); // 2 = PW_RENDERFULLCONTENT
                    if (!res) {
                        res = PrintWindow(hwnd, hdc, 0);
                    }
                } finally {
                    g.ReleaseHdc(hdc);
                }
            }
            bmp.Save(outputPath, ImageFormat.Png);
        }
        Console.WriteLine("Captured to " + outputPath);
        return true;
    }
}
"@

[WinCapture]::Capture("StarCraft", "c:\Projects\StarCraft\screenshot_campaign.png")
