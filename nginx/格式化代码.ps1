<#
    .SYNOPSIS
        调用clang-format格式化代码
#>

function Format-Cxx {
    [cmdletbinding()]
    param(
        [Parameter(ValueFromPipeline)]
        [string]$Path = ".",

        [Parameter()]
        [switch]$Recurse,

        [Parameter()]
        [string]$ClangBinary = ".\clang-format.exe",

        [Parameter()]
        [string]$FormatConfig = ".clang-format"
    )

    begin {
        Write-Verbose "[$((Get-Date).TimeofDay) BEGIN  ] Starting $($myinvocation.mycommand)"

        $params = @{
            Path    = $Path
            Recurse = $Recurse
            Include = @("*.cxx", "*.h", "*.cpp", "*.hpp", "*.c")
        }
    } #begin

    process {
        Write-Verbose "[$((Get-Date).TimeofDay) PROCESS] $Path "

        $codeFiles = Get-ChildItem @params | ForEach-Object { $_.FullName }
        foreach ($file in $codeFiles) {
            & $ClangBinary -style=file:$FormatConfig -i $file
        }
    } #process

    end {
        Write-Verbose "[$((Get-Date).TimeofDay) END    ] Ending $($myinvocation.mycommand)"

    } #end 

} #close Format-Cxx

Format-Cxx -Recurse