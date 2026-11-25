$ErrorActionPreference = 'Stop'
$pythonCommand = Get-Command python -ErrorAction SilentlyContinue
if ($pythonCommand -and $pythonCommand.Source -notlike '*WindowsApps\python.exe') {
    & $pythonCommand.Source (Join-Path $PSScriptRoot 'server.py')
    exit $LASTEXITCODE
}
$bundles = Get-ChildItem -LiteralPath 'C:\Program Files\WindowsApps' -Filter 'OpenAI.CodexPrimaryRuntime*' -Directory -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending
foreach ($bundle in $bundles) {
    $candidate = Join-Path $bundle.FullName 'dependencies\python\python.exe'
    if (Test-Path -LiteralPath $candidate) {
        & $candidate (Join-Path $PSScriptRoot 'server.py')
        exit $LASTEXITCODE
    }
}
throw 'Python 3.10 이상을 설치한 뒤 python server.py로 실행해 주세요.'

