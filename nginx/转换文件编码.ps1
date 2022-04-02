
if (Get-Module -ListAvailable -Name FileEncodingHelper) {
    Write-Verbose "FileEncodingHelper module exists!"
} 
else {
    Write-Verbose "Module does not exist, install FileEncodingHelper module..."
    Install-Module -Name FileEncodingHelper -AllowClobber
}

Import-Module -Name FileEncodingHelper

$textFiles = Get-ChildItem -Path . -Recurse -File | Where-Object { $_.FullName }
foreach ($file in $textFiles) {
    $detected = (Get-FileEncoding -Path $file).Detected
    if ($detected) {
        Convert-FileEncoding -Path $file -Detected $detected -NewEncoding 'utf-8'
    }
}