$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'download.ps1')
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$destination = Join-Path $projectRoot 'third_party\json'
Get-VerifiedDownload 'https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp' (Join-Path $destination 'json.hpp') 'AAF127C04CB31C406E5B04A63F1AE89369FCCDE6D8FA7CDDA1ED4F32DFC5DE63'
Get-VerifiedDownload 'https://raw.githubusercontent.com/nlohmann/json/v3.12.0/LICENSE.MIT' (Join-Path $destination 'LICENSE.MIT') '46A65CFFD1EA955132D95A8DD921640714A8D6B537D2E4E482D31145AE95B603'
