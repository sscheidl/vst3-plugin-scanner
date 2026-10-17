param([Parameter(Mandatory=$true)][string]$Executable)
$ErrorActionPreference = 'Stop'
$directory = Join-Path ([IO.Path]::GetDirectoryName($Executable)) ('single-exe-test-' + [Guid]::NewGuid().ToString('N'))
$process = $null
try {
    New-Item -ItemType Directory -Path $directory | Out-Null
    $standalone = Join-Path $directory 'Scanner with spaces.exe'
    Copy-Item -LiteralPath $Executable -Destination $standalone
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $standalone
    $start.Arguments = '--probe "' + (Join-Path $directory 'missing plugin.vst3') + '"'
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $start
    if (-not $process.Start()) { throw 'The standalone probe did not start.' }
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(10000)) {
        $process.Kill()
        throw 'The standalone probe exceeded its timeout.'
    }
    $result = $stdout.Result | ConvertFrom-Json
    if ($result.schemaVersion -ne 2 -or $result.status -ne 'load_error' -or $process.ExitCode -ne 4) {
        throw ('Unexpected standalone probe response: ' + $stdout.Result + $stderr.Result)
    }
    Write-Output 'Single EXE returns valid probe JSON without a companion executable.'
} finally {
    if ($process) { $process.Dispose() }
    # The directory is uniquely created by this test next to the supplied executable.
    if (Test-Path -LiteralPath $directory) { Remove-Item -LiteralPath $directory -Recurse -Force }
}
