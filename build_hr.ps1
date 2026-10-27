# build_hr.ps1 —— 激活 ESP-IDF 并构建心率应用,输出全部记入日志。
$ErrorActionPreference = "Continue"
$venv = "C:\Users\John\.espressif\python_env\idf5.5_py3.13_env"
$py = "$venv\Scripts\python.exe"
$mingit = "D:\Soft\DouBao\miband_heartrate\tools\MinGit\cmd"
$project = "D:\Soft\DouBao\miband_heartrate\ai-passport"

$env:IDF_PYTHON_ENV_PATH = $venv
$env:PATH = "$venv\Scripts;$mingit;$env:PATH"
# 组件仓库中国镜像(默认 components-file.espressif.com 不可达)。
$env:IDF_COMPONENT_STORAGE_URL = "https://components-file.espressif.cn"

. "D:\Soft\DouBao\esp-idf-v5.5.3\export.ps1" *> $null

& $py -c "import click, idf_component_manager"
if ($LASTEXITCODE -ne 0) { Write-Output "ENV_CHECK_FAILED"; exit 1 }
Write-Output "ENV_OK"

& $py "D:\Soft\DouBao\esp-idf-v5.5.3\tools\idf.py" -C $project build
Write-Output "BUILD_EXIT=$LASTEXITCODE"
