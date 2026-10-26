param([Parameter(Mandatory=$true)][string]$DocxPath)
$ErrorActionPreference = 'Stop'
$resolved = (Resolve-Path -LiteralPath $DocxPath).Path
$pdfPath = [IO.Path]::ChangeExtension($resolved, '.pdf')
# Own a separate COM application; never attach to a user's active Word instance.
$word = New-Object -ComObject Word.Application
$document = $null
try {
    $word.Visible = $false
    $word.DisplayAlerts = 0
    $document = $word.Documents.Open($resolved, $false, $true, $false)
    $document.ExportAsFixedFormat($pdfPath, 17)
}
finally {
    if ($null -ne $document) {
        $document.Close(0)
        [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($document)
    }
    $word.Quit()
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($word)
}
if (-not (Test-Path -LiteralPath $pdfPath)) { throw "Word produced no PDF: $pdfPath" }
Write-Output $pdfPath
