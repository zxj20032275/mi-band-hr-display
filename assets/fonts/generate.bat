@echo off
rem 字体生成脚本(可复现):lv_font_conv 1.5.3 + Noto Sans CJK SC Regular(OFL 1.1)。
setlocal
set DIR=%~dp0
set LFC=%DIR%..\..\tools\node-pkgs\node_modules\.bin\lv_font_conv.cmd
set FONT=%DIR%NotoSansCJKsc-Regular.otf

echo [1/2] hr_cn_16 (ASCII + 中文界面子集)
call "%LFC%" --font "%FONT%" --size 16 --bpp 4 --format lvgl --no-compress ^
  --range 0x20-0x7E,0x2026 ^
  --symbols "上下键亮度开始停止待机搜索中已连接断重" ^
  --lv-font-name hr_cn_16 --lv-include lvgl.h ^
  --output "%DIR%hr_cn_16.c"
if errorlevel 1 exit /b 1

echo [2/2] hr_num_104 (大数字)
call "%LFC%" --font "%FONT%" --size 104 --bpp 4 --format lvgl --no-compress ^
  --range 0x30-0x39,0x2D ^
  --lv-font-name hr_num_104 --lv-include lvgl.h ^
  --output "%DIR%hr_num_104.c"
if errorlevel 1 exit /b 1

echo Fonts generated.
endlocal
