# Linux库文件准备脚本
# 在Windows上运行，将Linux SDK文件复制到项目中

$ErrorActionPreference = "Stop"

Write-Host "=========================================" -ForegroundColor Green
Write-Host "Linux SDK 文件准备脚本" -ForegroundColor Green
Write-Host "=========================================" -ForegroundColor Green
Write-Host ""

# 定义路径
$ProjectRoot = "d:\work\sell\result"
$LinuxLibDir = "$ProjectRoot\lib_linux"
$LinuxIncludeDir = "$ProjectRoot\include_linux"

$TdfSdkPath = "d:\work\sell\sample\行情接口\TDFAPI30_LINUX_Ubuntu1404_20220831\C"
$SecSdkPath = "d:\work\sell\sample\HTS-csecitpdk5.2.52.0\csecitpdk5.2.52.0\sdk\linux"

# 1. 创建目录
Write-Host "[1/4] 创建目录结构..." -ForegroundColor Cyan
if (!(Test-Path $LinuxLibDir)) {
    New-Item -ItemType Directory -Path $LinuxLibDir | Out-Null
    Write-Host "  ✓ 创建 lib_linux/" -ForegroundColor Green
} else {
    Write-Host "  ✓ lib_linux/ 已存在" -ForegroundColor Gray
}

if (!(Test-Path $LinuxIncludeDir)) {
    New-Item -ItemType Directory -Path $LinuxIncludeDir | Out-Null
    Write-Host "  ✓ 创建 include_linux/" -ForegroundColor Green
} else {
    Write-Host "  ✓ include_linux/ 已存在" -ForegroundColor Gray
}

# 2. 复制行情API库
Write-Host ""
Write-Host "[2/4] 复制行情API (TDFAPI)..." -ForegroundColor Cyan
if (Test-Path "$TdfSdkPath\lib\libTDFAPI30.so") {
    Copy-Item "$TdfSdkPath\lib\libTDFAPI30.so" -Destination $LinuxLibDir -Force
    Write-Host "  ✓ libTDFAPI30.so" -ForegroundColor Green
} else {
    Write-Host "  ✗ 未找到 libTDFAPI30.so" -ForegroundColor Red
}

if (Test-Path "$TdfSdkPath\include\TDFAPI.h") {
    Copy-Item "$TdfSdkPath\include\*" -Destination $LinuxIncludeDir -Force
    Write-Host "  ✓ TDFAPI.h 及相关头文件" -ForegroundColor Green
} else {
    Write-Host "  ✗ 未找到 TDFAPI.h" -ForegroundColor Red
}

# 3. 复制交易API库
Write-Host ""
Write-Host "[3/4] 复制交易API (SECITPDK)..." -ForegroundColor Cyan
if (Test-Path "$SecSdkPath\lib") {
    $SecLibFiles = Get-ChildItem "$SecSdkPath\lib\*.so"
    foreach ($file in $SecLibFiles) {
        Copy-Item $file.FullName -Destination $LinuxLibDir -Force
        Write-Host "  ✓ $($file.Name)" -ForegroundColor Green
    }
} else {
    Write-Host "  ✗ 未找到 SECITPDK lib 目录" -ForegroundColor Red
}

if (Test-Path "$SecSdkPath\include") {
    $SecIncludeDir = "$LinuxIncludeDir\secitpdk"
    if (!(Test-Path $SecIncludeDir)) {
        New-Item -ItemType Directory -Path $SecIncludeDir | Out-Null
    }
    Copy-Item "$SecSdkPath\include\*" -Destination $SecIncludeDir -Recurse -Force
    Write-Host "  ✓ SECITPDK 头文件" -ForegroundColor Green
} else {
    Write-Host "  ✗ 未找到 SECITPDK include 目录" -ForegroundColor Red
}

# 4. 验证结果
Write-Host ""
Write-Host "[4/4] 验证文件完整性..." -ForegroundColor Cyan

$LibFiles = Get-ChildItem $LinuxLibDir -Filter "*.so" -ErrorAction SilentlyContinue
$LibCount = $LibFiles.Count
Write-Host "  动态库文件: $LibCount 个" -ForegroundColor $(if($LibCount -ge 4){"Green"}else{"Yellow"})
foreach ($file in $LibFiles) {
    $size = [math]::Round($file.Length / 1KB, 2)
    Write-Host "    - $($file.Name) ($size KB)" -ForegroundColor Gray
}

$IncludeFiles = Get-ChildItem $LinuxIncludeDir -Recurse -Filter "*.h" -ErrorAction SilentlyContinue
$IncludeCount = $IncludeFiles.Count
Write-Host "  头文件: $IncludeCount 个" -ForegroundColor $(if($IncludeCount -gt 0){"Green"}else{"Yellow"})

Write-Host ""
Write-Host "=========================================" -ForegroundColor Green
if ($LibCount -ge 4 -and $IncludeCount -gt 0) {
    Write-Host "✓ Linux SDK 准备完成！" -ForegroundColor Green
    Write-Host ""
    Write-Host "下一步：" -ForegroundColor Cyan
    Write-Host "  1. 将 result/ 目录上传到Linux服务器" -ForegroundColor White
    Write-Host "     scp -r d:\work\sell\result user@server:/opt/trading/" -ForegroundColor Gray
    Write-Host ""
    Write-Host "  2. SSH登录并编译" -ForegroundColor White
    Write-Host "     ssh user@server" -ForegroundColor Gray
    Write-Host "     cd /opt/trading/result && ./deploy.sh" -ForegroundColor Gray
} else {
    Write-Host "⚠ SDK文件不完整，请检查源路径" -ForegroundColor Yellow
    Write-Host "  预期：至少4个.so文件 + 多个.h头文件" -ForegroundColor Yellow
}
Write-Host "=========================================" -ForegroundColor Green
