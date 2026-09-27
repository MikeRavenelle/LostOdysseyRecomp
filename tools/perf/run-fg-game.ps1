param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$BaselineDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [Parameter(Mandatory)][string]$GameDirectory,
    [ValidateRange(10,600)][int]$Seconds = 120,
    [switch]$DisableFg,
    [switch]$DisableObjectMotion,
    [switch]$WindowCycle,
    [ValidateSet('Baseline','D3D12','Vulkan')][string]$Backend = 'Baseline',
    [ValidateSet('Baseline','Off','Dlss','Fsr')][string]$Upscaler = 'Baseline',
    [ValidateRange(-1,3)][int]$Quality = -1,
    [ValidateSet('Diagnostic','Lightweight')][string]$CaptureMode = 'Diagnostic',
    [switch]$Background,
    [switch]$CaptureScreenshots
)
# ACTIVE GAME DRIVER: isolated profile/save/config copies, optional hidden gameplay,
# muted audio, bounded automated input, then closes only its own game process.
$ErrorActionPreference = 'Stop'
if ($Background -and $WindowCycle) { throw 'WindowCycle requires foreground interaction.' }
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$baseline = (Resolve-Path -LiteralPath $BaselineDirectory).Path
$game = (Resolve-Path -LiteralPath $GameDirectory).Path
$run = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $run) { throw 'OutputDirectory must be new; evidence is never overwritten.' }
New-Item -ItemType Directory -Path $run | Out-Null
foreach ($name in @('settings.ini','save','profile','shaders')) {
    $source = Join-Path $baseline $name
    if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $run -Recurse }
}
foreach ($file in Get-ChildItem -LiteralPath $build -File) {
    if ($file.Extension -eq '.dll' -or $file.Name -in @('LostOdysseyRecomp.exe','source-version.txt','LostOdysseyRecomp.exe.build.json')) {
        Copy-Item -LiteralPath $file.FullName -Destination $run
    }
}
# Overrides apply only to the isolated copy, never to the baseline.
$settingsPath = Join-Path $run 'settings.ini'
$settingsText = Get-Content -LiteralPath $settingsPath -Raw
$overrides = @{}
if ($Backend -ne 'Baseline') { $overrides['graphics_backend'] = $(if ($Backend -eq 'D3D12') { 0 } else { 1 }) }
if ($Upscaler -ne 'Baseline') { $overrides['upscaler'] = @{Off=0; Dlss=1; Fsr=2}[$Upscaler] }
if ($Quality -ge 0) { $overrides['dlss_quality'] = $Quality; $overrides['fsr_quality'] = $Quality }
foreach ($entry in $overrides.GetEnumerator()) {
    $pattern = '(?m)^' + [regex]::Escape($entry.Key) + '=.*$'
    $line = $entry.Key + '=' + $entry.Value
    if ([regex]::IsMatch($settingsText, $pattern)) { $settingsText = [regex]::Replace($settingsText, $pattern, $line) }
    else { $settingsText += "`n$line`n" }
}
if ($overrides.Count) { [IO.File]::WriteAllText($settingsPath, $settingsText) }
$exe = Join-Path $run 'LostOdysseyRecomp.exe'
if (!(Test-Path -LiteralPath $exe)) { throw 'BuildDirectory lacks LostOdysseyRecomp.exe' }
function BaselineMetadata {
    foreach ($name in @('settings.ini','save','profile')) {
        Get-ChildItem -LiteralPath (Join-Path $baseline $name) -File -Recurse -ErrorAction SilentlyContinue |
            ForEach-Object { [ordered]@{ path=$_.FullName; length=$_.Length; modified=$_.LastWriteTimeUtc.ToString('o') } }
    }
}
$before = @(BaselineMetadata)
$before | ConvertTo-Json -Depth 3 | Set-Content (Join-Path $run 'baseline-before.json')
# Child-specific environment: the caller's LO_* and validation settings cannot
# silently change the experiment. Other normal Windows environment is inherited.
$start = [Diagnostics.ProcessStartInfo]::new($exe)
$start.WorkingDirectory = $run
$start.UseShellExecute = $false
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$start.CreateNoWindow = [bool]$Background
foreach ($name in @($start.Environment.Keys)) {
    if ($name.StartsWith('LO_') -or $name.StartsWith('VK_LAYER') -or $name.StartsWith('VK_INSTANCE_LAYERS')) {
        $start.Environment.Remove($name) | Out-Null
    }
}
$start.ArgumentList.Add('--game'); $start.ArgumentList.Add($game); $start.ArgumentList.Add('--quiet-kernel')
$start.Environment['LO_DLSS_FG'] = $(if ($DisableFg) { '0' } else { '1' })
if ($CaptureMode -eq 'Diagnostic') { $start.Environment['LO_MV_LOG'] = '1' }
if ($DisableObjectMotion) { $start.Environment['LO_MV_REPLAY'] = '0' }
$start.Environment['LO_AUDIO_MUTE'] = '1'
if ($Background) { $start.Environment['LO_BACKGROUND'] = '1' }
if ($CaptureScreenshots) {
    $start.Environment['LO_SCREENSHOT_REQUEST'] = Join-Path $run 'screenshot-request.txt'
    $start.Environment['LO_SCREENSHOT_PATH'] = Join-Path $run 'scene.ppm'
}
$start.Environment['LO_LOG_FILE'] = Join-Path $run 'runtime.log'
$start.Environment['LO_SHADER_CACHE_DIR'] = Join-Path $run 'shader-cache'
$start.Environment['LO_AUTO_BUTTONS'] = 's@120,a@240,a@360,a@480,a@700,a@900'
$start.Environment['LO_AUTO_PULSE'] = '6'
if ($CaptureMode -eq 'Diagnostic') { $start.Environment['LO_RENDER_TIMING'] = '1' }
$start.Environment['LO_AUTO_STICK'] = '0,18000,1600,1900'
$process = [Diagnostics.Process]::Start($start)
$stdout = $process.StandardOutput.ReadToEndAsync()
$stderr = $process.StandardError.ReadToEndAsync()
$manifest = [ordered]@{ pid=$process.Id; started=[DateTime]::UtcNow.ToString('o'); exe=$exe;
    sha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash; fg=!$DisableFg;
    foreground=!$Background; muted=$true; object_motion=!$DisableObjectMotion;
    screenshot_requests=[bool]$CaptureScreenshots;
    backend=$Backend; upscaler=$Upscaler; quality=$Quality; capture_mode=$CaptureMode;
    render_timing=($CaptureMode -eq 'Diagnostic'); mv_log=($CaptureMode -eq 'Diagnostic');
    game=$game; baseline=$baseline; seconds=$Seconds }
$manifest | ConvertTo-Json | Set-Content (Join-Path $run 'run.json')
# A hidden parent terminal can leave the SDL window hidden even when
# AppActivate reports success. Restore the actual game HWND before sampling.
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class FgGameWindow {
    public delegate bool EnumWindowCallback(IntPtr window, IntPtr state);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowCallback callback, IntPtr state);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr w, IntPtr l);
    public static bool CloseOwnedWindows(uint pid) {
        bool posted = false;
        EnumWindows((window, state) => {
            uint owner; GetWindowThreadProcessId(window, out owner);
            if (owner == pid) posted |= PostMessage(window, 0x0010, IntPtr.Zero, IntPtr.Zero);
            return true;
        }, IntPtr.Zero);
        return posted;
    }
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
}
'@
$focusTimer = [Diagnostics.Stopwatch]::StartNew()
while (!$Background -and !$process.HasExited -and $focusTimer.Elapsed.TotalSeconds -lt 10) {
    $process.Refresh()
    if ($process.MainWindowHandle -ne [IntPtr]::Zero) {
        [void][FgGameWindow]::ShowWindow($process.MainWindowHandle, 9)
        [void][FgGameWindow]::SetForegroundWindow($process.MainWindowHandle)
        if ([FgGameWindow]::GetForegroundWindow() -ne $process.MainWindowHandle) {
            try {
                [FgGameWindow]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)
                [void][FgGameWindow]::SetForegroundWindow($process.MainWindowHandle)
            } finally {
                [FgGameWindow]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
            }
        }
        break
    }
    Start-Sleep -Milliseconds 100
}
[ordered]@{ visible_window=$process.MainWindowHandle.ToInt64();
    foreground=([FgGameWindow]::GetForegroundWindow() -eq $process.MainWindowHandle) } |
    ConvertTo-Json | Set-Content (Join-Path $run 'window-focus.json')
if ($WindowCycle) {
    $shell = New-Object -ComObject WScript.Shell
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class FgGameKeys {
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
}
'@
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $cycle = 0
    $events = @()
    while (!($finished = $process.WaitForExit(500)) -and $timer.Elapsed.TotalSeconds -lt $Seconds) {
        if ($cycle -lt 2 -and $timer.Elapsed.TotalSeconds -ge (30 + 15 * $cycle)) {
            $activated = $shell.AppActivate($process.Id)
            Start-Sleep -Milliseconds 150
            [uint32]$foregroundProcess = 0
            [void][FgGameKeys]::GetWindowThreadProcessId([FgGameKeys]::GetForegroundWindow(), [ref]$foregroundProcess)
            $activated = $activated -and $foregroundProcess -eq $process.Id
            if ($activated) {
                try {
                    [FgGameKeys]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)
                    Start-Sleep -Milliseconds 150
                    [FgGameKeys]::keybd_event(0x0D, 0, 0, [UIntPtr]::Zero)
                    Start-Sleep -Milliseconds 150
                } finally {
                    [FgGameKeys]::keybd_event(0x0D, 0, 2, [UIntPtr]::Zero)
                    [FgGameKeys]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
                }
            }
            $events += [ordered]@{ seconds=$timer.Elapsed.TotalSeconds; activated=$activated; action='Alt+Enter'; pid=$process.Id }
            $events | ConvertTo-Json | Set-Content (Join-Path $run 'window-cycle.json')
            ++$cycle
        }
    }
} else {
    $finished = $process.WaitForExit($Seconds * 1000)
}
$closed = $false
if (!$finished) {
    $closed = if ($Background) { [FgGameWindow]::CloseOwnedWindows([uint32]$process.Id) } else { $process.CloseMainWindow() }
    $finished = $process.WaitForExit(10000)
    if (!$finished) { $process.Kill(); $process.WaitForExit() }
}
$stdout.Result | Set-Content (Join-Path $run 'stdout.log')
$stderr.Result | Set-Content (Join-Path $run 'stderr.log')
$after = @(BaselineMetadata)
$after | ConvertTo-Json -Depth 3 | Set-Content (Join-Path $run 'baseline-after.json')
$preserved = ($before | ConvertTo-Json -Depth 3 -Compress) -eq ($after | ConvertTo-Json -Depth 3 -Compress)
[ordered]@{ exit_code=$process.ExitCode; close_requested=$closed; forced_stop=!$finished;
    baseline_preserved=$preserved; ended=[DateTime]::UtcNow.ToString('o') } |
    ConvertTo-Json | Tee-Object -FilePath (Join-Path $run 'termination.json')
if (!$preserved) { throw 'Baseline metadata changed during experiment.' }
