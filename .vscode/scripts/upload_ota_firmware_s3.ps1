param(
  [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

function Get-WorkspaceRoot {
  return (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
}

function Get-ConfigValue {
  param(
    [string]$Content,
    [string]$Name,
    [string]$DefaultValue = ''
  )

  $pattern = '(?:#define\s+' + [regex]::Escape($Name) + '\s+"([^"]*)"|' + [regex]::Escape($Name) + '="([^"]*)")'
  $match = [regex]::Match($Content, $pattern)
  if ($match.Success) {
    if (-not [string]::IsNullOrWhiteSpace($match.Groups[1].Value)) {
      return $match.Groups[1].Value
    }
    return $match.Groups[2].Value
  }

  return $DefaultValue
}

function Format-S3KeyPrefix {
  param([string]$Prefix)

  if ([string]::IsNullOrWhiteSpace($Prefix)) {
    return ''
  }

  $normalized = $Prefix.Trim().TrimStart('/').Replace('\', '/')
  if (-not $normalized.EndsWith('/')) {
    $normalized += '/'
  }

  return $normalized
}

function Invoke-AwsCli {
  param(
    [string[]]$CommandArguments
  )

  $stdoutPath = [System.IO.Path]::GetTempFileName()
  $stderrPath = [System.IO.Path]::GetTempFileName()

  try {
    $process = Start-Process -FilePath 'aws' -ArgumentList $CommandArguments -NoNewWindow -Wait -PassThru -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $stdoutText = (Get-Content $stdoutPath -Raw -ErrorAction SilentlyContinue)
    $stderrText = (Get-Content $stderrPath -Raw -ErrorAction SilentlyContinue)
    $outputText = @($stdoutText, $stderrText) -join [Environment]::NewLine
    $outputText = $outputText.Trim()

    return @{
      Success  = ($process.ExitCode -eq 0)
      ExitCode = $process.ExitCode
      Output   = $outputText
    }
  }
  finally {
    Remove-Item $stdoutPath -ErrorAction SilentlyContinue
    Remove-Item $stderrPath -ErrorAction SilentlyContinue
  }
}

function Get-FirmwareBinaryPath {
  param(
    [string]$WorkspaceRoot,
    [string]$PreferredName
  )

  $preferredPath = Join-Path $WorkspaceRoot ("build\{0}.bin" -f $PreferredName)
  if (Test-Path $preferredPath) {
    return (Resolve-Path $preferredPath).Path
  }

  $candidates = @(Get-ChildItem -Path (Join-Path $WorkspaceRoot 'build') -Filter '*.bin' -File |
    Where-Object { $_.Name -notin @('bootloader.bin', 'partition-table.bin', 'ota_data_initial.bin') } |
    Sort-Object Name)

  if ($candidates.Count -eq 0) {
    throw 'No application firmware binary was found under build\.'
  }

  return $candidates[0].FullName
}

function Invoke-S3Upload {
  param(
    [string]$SourcePath,
    [string]$Bucket,
    [string]$Key,
    [bool]$TryPublicRead,
    [string]$ContentType = 'application/octet-stream'
  )

  $target = "s3://$Bucket/$Key"
  $awsArguments = @('s3', 'cp', $SourcePath, $target, '--content-type', $ContentType, '--only-show-errors')
  $attemptResult = $null

  if ($TryPublicRead) {
    $attemptResult = Invoke-AwsCli -CommandArguments ($awsArguments + @('--acl', 'public-read'))
    if ($attemptResult.Success) {
      return @{ Target = $target; PublicRead = $true }
    }

    if ($attemptResult.Output -match 'AccessControlListNotSupported') {
      Write-Warning "Bucket blocks ACLs for $target. Retrying without ACL."
    }
    else {
      Write-Warning "Upload with public-read ACL failed for $target. Retrying without ACL."
      if (-not [string]::IsNullOrWhiteSpace($attemptResult.Output)) {
        Write-Warning $attemptResult.Output
      }
    }
  }

  $attemptResult = Invoke-AwsCli -CommandArguments $awsArguments
  if (-not $attemptResult.Success) {
    $message = "Upload failed for $target"
    if (-not [string]::IsNullOrWhiteSpace($attemptResult.Output)) {
      $message += ": $($attemptResult.Output)"
    }
    throw $message
  }

  Write-Host "Uploaded  : $target"
  return @{ Target = $target; PublicRead = $false }
}

function Test-S3ObjectMatchesLocalFile {
  param(
    [string]$Bucket,
    [string]$Key,
    [string]$LocalPath,
    [string]$ExpectedSha256,
    [long]$ExpectedSize
  )

  $tempPath = [System.IO.Path]::GetTempFileName()
  $target = "s3://$Bucket/$Key"

  try {
    $downloadResult = Invoke-AwsCli -CommandArguments @('s3', 'cp', $target, $tempPath, '--only-show-errors')
    if (-not $downloadResult.Success) {
      $message = "Verification download failed for $target"
      if (-not [string]::IsNullOrWhiteSpace($downloadResult.Output)) {
        $message += ": $($downloadResult.Output)"
      }
      throw $message
    }

    $downloadedItem = Get-Item $tempPath
    $downloadedHash = (Get-FileHash -Path $tempPath -Algorithm SHA256).Hash.ToLowerInvariant()

    if ($downloadedItem.Length -ne $ExpectedSize) {
      throw "Verification failed for ${target}: size mismatch local=$ExpectedSize remote=$($downloadedItem.Length)"
    }

    if ($downloadedHash -ne $ExpectedSha256) {
      throw "Verification failed for ${target}: sha256 mismatch local=$ExpectedSha256 remote=$downloadedHash"
    }

    Write-Host "Verified  : $target"
  }
  finally {
    Remove-Item $tempPath -ErrorAction SilentlyContinue
  }
}

function Get-PublicS3Url {
  param(
    [string]$Bucket,
    [string]$Region,
    [string]$Key
  )

  if ([string]::IsNullOrWhiteSpace($Region)) {
    return "https://s3.amazonaws.com/$Bucket/$Key"
  }

  return "https://s3.$Region.amazonaws.com/$Bucket/$Key"
}

$workspaceRoot = Get-WorkspaceRoot
$versionPath = Join-Path $workspaceRoot 'VERSION'
$projectDescriptionPath = Join-Path $workspaceRoot 'build\project_description.json'
$sdkconfigPath = Join-Path $workspaceRoot 'sdkconfig'

if (-not (Get-Command aws -ErrorAction SilentlyContinue)) {
  throw 'AWS CLI was not found in PATH. Install and configure aws before running this task.'
}
if (-not (Test-Path $versionPath)) {
  throw 'VERSION file is missing from the workspace root.'
}
if (-not (Test-Path $projectDescriptionPath)) {
  throw 'build\project_description.json was not found. Build the firmware first.'
}
if (-not (Test-Path $sdkconfigPath)) {
  throw 'sdkconfig was not found in the workspace root.'
}

$version = (Get-Content $versionPath -Raw).Trim()
if ([string]::IsNullOrWhiteSpace($version)) {
  throw 'VERSION file is empty.'
}

$projectDescription = Get-Content $projectDescriptionPath -Raw | ConvertFrom-Json
$projectName = if ($projectDescription.project_name) { [string]$projectDescription.project_name } else { 'RallyBox-Dashboard' }

$sdkconfigContent = Get-Content $sdkconfigPath -Raw
$bucket = Get-ConfigValue -Content $sdkconfigContent -Name 'CONFIG_RALLYBOX_OTA_S3_BUCKET'
if ([string]::IsNullOrWhiteSpace($bucket)) {
  $bucket = Get-ConfigValue -Content $sdkconfigContent -Name 'CONFIG_RALLYBOX_GPX_S3_BUCKET'
}
$region = Get-ConfigValue -Content $sdkconfigContent -Name 'CONFIG_RALLYBOX_OTA_S3_REGION'
if ([string]::IsNullOrWhiteSpace($region)) {
  $region = Get-ConfigValue -Content $sdkconfigContent -Name 'CONFIG_RALLYBOX_GPX_S3_REGION' -DefaultValue 'us-east-1'
}
$prefix = Format-S3KeyPrefix (Get-ConfigValue -Content $sdkconfigContent -Name 'CONFIG_RALLYBOX_OTA_S3_OBJECT_PREFIX' -DefaultValue 'firmware/RallyBox-Dashboard/')
$otaFileName = Get-ConfigValue -Content $sdkconfigContent -Name 'CONFIG_RALLYBOX_OTA_FILENAME' -DefaultValue 'RallyBox-Dashboard.bin'
$manifestFileName = 'ota-manifest.json'

if ([string]::IsNullOrWhiteSpace($bucket)) {
  throw 'No OTA or GPX S3 bucket is configured in sdkconfig.'
}
if ([string]::IsNullOrWhiteSpace($otaFileName)) {
  throw 'CONFIG_RALLYBOX_OTA_FILENAME is empty.'
}

$binaryPath = Get-FirmwareBinaryPath -WorkspaceRoot $workspaceRoot -PreferredName $projectName
$binaryItem = Get-Item $binaryPath
$binaryHash = (Get-FileHash -Path $binaryPath -Algorithm SHA256).Hash.ToLowerInvariant()
$otaStableKey = "$prefix$otaFileName"
$otaVersionedFileName = [System.IO.Path]::GetFileNameWithoutExtension($otaFileName) + '-' + $version + [System.IO.Path]::GetExtension($otaFileName)
$otaVersionedKey = "$prefix$otaVersionedFileName"
$manifestKey = "$prefix$manifestFileName"

$publicUrl = Get-PublicS3Url -Bucket $bucket -Region $region -Key $otaStableKey
$versionedUrl = Get-PublicS3Url -Bucket $bucket -Region $region -Key $otaVersionedKey
$manifestUrl = Get-PublicS3Url -Bucket $bucket -Region $region -Key $manifestKey

Write-Host "Workspace : $workspaceRoot"
Write-Host "Project   : $projectName"
Write-Host "Version   : $version"
Write-Host "Binary    : $binaryPath"
Write-Host "Bucket    : $bucket"
Write-Host "Region    : $region"
Write-Host "Stable    : s3://$bucket/$otaStableKey"
Write-Host "Versioned : s3://$bucket/$otaVersionedKey"
Write-Host "Manifest  : s3://$bucket/$manifestKey"
Write-Host "Latest URL: $publicUrl"
Write-Host "Size      : $($binaryItem.Length) bytes"
Write-Host "SHA256    : $binaryHash"

if ($DryRun) {
  Write-Host ''
  Write-Host 'Dry run complete. No upload was performed.'
  return
}

$manifestPath = [System.IO.Path]::GetTempFileName()

try {
  $manifest = [ordered]@{
    manifestVersion = 1
    product         = 'RallyBox-Dashboard'
    projectName     = $projectName
    version         = $version
    uploadedAtUtc   = (Get-Date).ToUniversalTime().ToString('o')
    bucket          = $bucket
    region          = $region
    prefix          = $prefix
    stable          = [ordered]@{
      fileName  = $otaFileName
      key       = $otaStableKey
      url       = $publicUrl
      sizeBytes = $binaryItem.Length
      sha256    = $binaryHash
    }
    versioned       = [ordered]@{
      fileName  = $otaVersionedFileName
      key       = $otaVersionedKey
      url       = $versionedUrl
      sizeBytes = $binaryItem.Length
      sha256    = $binaryHash
    }
  }

  $manifest | ConvertTo-Json -Depth 6 | Set-Content -Path $manifestPath -Encoding UTF8

  $stableUpload = Invoke-S3Upload -SourcePath $binaryPath -Bucket $bucket -Key $otaStableKey -TryPublicRead $true
  $versionedUpload = Invoke-S3Upload -SourcePath $binaryPath -Bucket $bucket -Key $otaVersionedKey -TryPublicRead $true
  $manifestUpload = Invoke-S3Upload -SourcePath $manifestPath -Bucket $bucket -Key $manifestKey -TryPublicRead $true -ContentType 'application/json'

  Test-S3ObjectMatchesLocalFile -Bucket $bucket -Key $otaStableKey -LocalPath $binaryPath -ExpectedSha256 $binaryHash -ExpectedSize $binaryItem.Length
  Test-S3ObjectMatchesLocalFile -Bucket $bucket -Key $otaVersionedKey -LocalPath $binaryPath -ExpectedSha256 $binaryHash -ExpectedSize $binaryItem.Length
}
finally {
  Remove-Item $manifestPath -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host 'OTA firmware upload completed.'
Write-Host "Latest URL : $publicUrl"
Write-Host "Manifest URL: $manifestUrl"
if (-not $stableUpload.PublicRead -or -not $versionedUpload.PublicRead -or -not $manifestUpload.PublicRead) {
  Write-Warning 'One or more uploads were performed without public-read ACL. Ensure the bucket policy still allows device OTA downloads.'
}
