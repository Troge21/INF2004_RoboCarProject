[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('lift', 'curb', 'powered-lift', 'powered-ramp')]
    [string]$Trial,
    [string]$Port
)

$ErrorActionPreference = 'Stop'
if (!$Port) {
    $ports = @([System.IO.Ports.SerialPort]::GetPortNames())
    if ($ports.Count -ne 1) {
        $available = $ports -join ', '
        throw "Pass -Port with the Pico's COM port. Detected ports: $available"
    }
    $Port = $ports[0]
}

$action = switch ($Trial) {
    'lift' { 'Lift the whole car normally, hold for one second, then set it down.' }
    'curb' { 'Gently roll the car over the strip under one side, then stop.' }
    'powered-lift' { 'Lift by the chassis as usual, then return it to the raised support.' }
    'powered-ramp' { 'Click Resume, cross the ramp, then click Latch stop on flat ground.' }
}
$powered = $Trial -in @('powered-lift', 'powered-ramp')
$prefix = if ($powered) { '[S4 MOTOR TRACE] ' } else { '[S4 TRACE] ' }
$commonFields = '\d+,\d+,-?\d+,-?\d+,-?\d+,[01],-?\d+,-?\d+,[01]'
$diagnosticFields = ',-?\d+,-?\d+,-?\d+,[012],[01],[01],[01],-?\d+,-?\d+,[01],-?\d+,-?\d+,[01],\d+,\d+'
$samplePattern = if ($powered) {
    '^' + $commonFields + $diagnosticFields + '$'
} else {
    '^' + $commonFields + '(' + $diagnosticFields + ')?$'
}
$serial = [System.IO.Ports.SerialPort]::new($Port, 115200)
$serial.DtrEnable = $true
$serial.ReadTimeout = 200
$rows = [System.Collections.Generic.List[string]]::new()
$rows.Add('trial,phase,host_ms,sequence,uptime_ms,ax_mg,ay_mg,az_mg,encoder_valid,left_count,right_count,pickup_latched,' +
    'ref_x_mg,ref_y_mg,ref_z_mg,lift_phase,tilt_candidate,motor_valid,motor_active,left_duty_x100,right_duty_x100,' +
    'speed_valid,left_rpm_x10,right_rpm_x10,line_valid,centre_adc,right_adc')

$previousSequence = $null
$previousTime = $null
$missingRows = 0
$malformedRows = 0
$maxGapMs = 0
$encoderRows = 0
$actionRows = 0
$pickupSeen = $false
$actionShown = $false
$capture = $null
try {
    $serial.Open()
    $serial.DiscardInBuffer()
    if ($Trial -eq 'powered-lift') {
        Write-Host 'Keep the chassis level with both wheels spinning freely above the surface. Wait for GO.'
    } elseif ($Trial -eq 'powered-ramp') {
        Write-Host 'Start on flat ground just before the ramp with STOP latched. Wait for GO.'
    } else {
        Write-Host 'Keep the car flat and still while the recorder waits for calibrated samples.'
    }
    $waiting = [System.Diagnostics.Stopwatch]::StartNew()
    while (!$capture -or $capture.ElapsedMilliseconds -lt 8000) {
        if (!$capture -and $waiting.ElapsedMilliseconds -ge 15000) {
            if ($Trial -eq 'powered-lift') {
                throw 'No powered trace with both wheels spinning. Check the motor-trace UF2, COM port and dashboard state.'
            }
            throw 'No calibrated S4 trace received. Check the UF2, the COM port and that the car is still.'
        }
        if ($capture -and !$actionShown -and $capture.ElapsedMilliseconds -ge 2000) {
            Write-Host "GO: $action"
            $actionShown = $true
        }
        try {
            $line = $serial.ReadLine().Trim()
        } catch [System.TimeoutException] {
            continue
        }
        if (!$line.StartsWith($prefix)) { continue }
        $sample = $line.Substring($prefix.Length)
        if ($sample -notmatch $samplePattern) {
            ++$malformedRows
            continue
        }
        $fields = $sample.Split(',')
        if (!$capture) {
            if ($fields[8] -eq '1') {
                throw 'Pickup is already latched. Put the car flat, restart the Pico and repeat the trial.'
            }
            if ($Trial -eq 'powered-lift' -and
                ($fields[14] -ne '1' -or $fields[15] -ne '1' -or
                 $fields[18] -ne '1' -or [int]$fields[19] -eq 0 -or [int]$fields[20] -eq 0)) {
                continue
            }
            Write-Host 'Recording the baseline. Wait for GO.'
            $capture = [System.Diagnostics.Stopwatch]::StartNew()
        }
        $sequence = [long]$fields[0]
        $uptime = [long]$fields[1]
        if ($null -ne $previousSequence) {
            if ($sequence -le $previousSequence -or $uptime -lt $previousTime) {
                throw 'The Pico restarted during capture. Start a fresh trial.'
            }
            $missingRows += $sequence - $previousSequence - 1
            $maxGapMs = [Math]::Max($maxGapMs, $uptime - $previousTime)
        }
        $previousSequence = $sequence
        $previousTime = $uptime
        if ($fields[5] -eq '1') { ++$encoderRows }
        if ($fields[8] -eq '1') { $pickupSeen = $true }

        $phase = if ($actionShown) { 'action' } else { 'baseline' }
        if ($actionShown) { ++$actionRows }
        if ($fields.Count -eq 9) { $sample += ',' * 15 }
        $rows.Add("$Trial,$phase,$($capture.ElapsedMilliseconds),$sample")
    }
} finally {
    $serial.Dispose()
}

if ($actionRows -eq 0) { throw 'The trace ended before the action. Repeat the trial.' }
$stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')
$out = Join-Path $PSScriptRoot "logs/pickup-$Trial-$stamp.csv"
[System.IO.File]::WriteAllLines($out, $rows)
Write-Host "Saved $($rows.Count - 1) samples ($encoderRows with encoder feedback) to $out"
Write-Host "Largest sample gap: $maxGapMs ms. Missing rows: $missingRows. Malformed rows: $malformedRows."
Write-Host "Pickup latched during capture: $pickupSeen"
if ($maxGapMs -gt 40 -or $missingRows -gt 0 -or $malformedRows -gt 0) {
    Write-Warning 'This trace has gaps. Keep the CSV and repeat if the motion is missing.'
}
