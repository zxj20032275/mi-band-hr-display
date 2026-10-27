# merge_hr.ps1 —— 生成可从 0x0 刷写的合并固件。
$ErrorActionPreference = "Continue"
$venv = "C:\Users\John\.espressif\python_env\idf5.5_py3.13_env"
$py = "$venv\Scripts\python.exe"
$mingit = "D:\Soft\DouBao\miband_heartrate\tools\MinGit\cmd"
$project = "D:\Soft\DouBao\miband_heartrate\ai-passport"

$env:IDF_PYTHON_ENV_PATH = $venv
$env:PATH = "$venv\Scripts;$mingit;$env:PATH"

. "D:\Soft\DouBao\esp-idf-v5.5.3\export.ps1" *> $null

& $py "D:\Soft\DouBao\esp-idf-v5.5.3\tools\idf.py" -C $project merge-bin
Write-Output "MERGE_EXIT=$LASTEXITCODE"
