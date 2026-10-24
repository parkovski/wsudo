param()

function help {
  wsudo.exe --help
}

$exeArgs = [System.Collections.Generic.List[string]]::new()
$cmdArgs = [System.Collections.Generic.List[string]]::new()
$i = 0
$processingArgs = $true
for (; $i -lt $args.Length; ++$i) {
  if (-not $processingArgs) {
    $cmdArgs.Add($args[$i])
    continue
  }

  switch -CaseSensitive ($args[$i]) {
  "--" {
    $processingArgs = $false
  }
  "-h" {
    help
    exit
  }
  "--help" {
    help
    exit
  }
  "-L" {
    ++$i
    $exeArgs.Add("-L")
    $exeArgs.Add($args[$i])
  }
  "--log-level" {
    ++$i
    $exeArgs.Add("--log-level=" + $args[$i])
  }
  "-E" { $exeArgs.Add($args[$i]) }
  "--preserve-env" { $exeArgs.Add($args[$i]) }
  "--no-service" { $exeArgs.Add($args[$i]) }
  "--no-shim" { $exeArgs.Add($args[$i]) }
  "--detach" { $exeArgs.Add($args[$i]) }
  "-S" { $exeArgs.Add($args[$i]) }
  "--system" { $exeArgs.Add($args[$i]) }
  default {
    $processingArgs = $false
    $cmdArgs.Add($args[$i])
  }
  }
}

$hostProcessName = (Get-Process -Id $PID).MainModule.FileName
$targetCommand = (Get-Command $cmdArgs[0])
$realCmdArgs = $cmdArgs[1..99999]

if ($targetCommand.CommandType -eq 'Alias') {
  $targetCommand = $targetCommand.ResolvedCommand
}

if ($targetCommand.CommandType -eq 'Application') {
  wsudo.exe @exeArgs @cmdArgs
} elseif ($targetCommand.CommandType -eq 'Cmdlet') {
  wsudo.exe @exeArgs $hostProcessName -c @cmdArgs
} elseif ($targetCommand.CommandType -eq 'Function') {
  Write-Error "Can't elevate a PowerShell function."
  exit 1
}
