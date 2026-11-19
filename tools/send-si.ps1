# i2r 보드(I2R_IoT)에 Wi-Fi와 MQTT 설정(si)을 USB로 보내는 스크립트
# 사용: powershell -ExecutionPolicy Bypass -File send-si.ps1 [-Port COM3]
#   -Port를 빼면 CH340(i2r 보드 USB 칩) 포트를 스스로 찾는다.
# 비밀번호는 화면에 보이지 않게 입력받고, 어디에도 저장하지 않는다.
# 보드에서 I2R_IoT 예제(NetCheck, MqttTest 등)가 돌고 있어야 한다. 시리얼 모니터는 닫아 둔다.
param([string]$Port = "")

function Read-Secret([string]$prompt) {
  $sec = Read-Host $prompt -AsSecureString
  $b = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($sec)
  try { return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($b) }
  finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($b) }
}

if ($Port -eq "") {
  $dev = Get-PnpDevice -Class Ports -PresentOnly -ErrorAction SilentlyContinue | Where-Object { $_.FriendlyName -match 'CH340' } | Select-Object -First 1
  if ($dev -and $dev.FriendlyName -match '\((COM\d+)\)') { $Port = $Matches[1] }
  if ($Port -eq "") { Write-Host "보드 포트를 찾지 못했습니다. USB를 연결하고 -Port COM번호 로 다시 실행하세요." -ForegroundColor Yellow; exit 1 }
}
Write-Host "보드 포트: $Port"
Write-Host ""

$ssid = Read-Host "공유기 이름(SSID)"
if ($ssid -eq "") { Write-Host "SSID가 비어 있어 멈춥니다." -ForegroundColor Yellow; exit 1 }
$pw = Read-Secret "공유기 비밀번호"

Write-Host ""
Write-Host "MQTT 서버: Enter = test.mosquitto.org (검증, 교육용 공개 서버) / none = MQTT 없이 Wi-Fi만 / 또는 주소 입력"
$broker = Read-Host "MQTT 서버"
if ($broker -eq "") { $broker = "test.mosquitto.org" }
if ($broker -eq "none") { $broker = "" }

$cfg = [ordered]@{ c = "si"; ssid = $ssid; password = $pw; mqttBroker = $broker }
if ($broker -ne "") {
  $portIn = Read-Host "포트 (Enter = 1883)"
  if ($portIn -ne "") { $cfg.mqttPort = [int]$portIn }
  $user = Read-Host "MQTT 아이디 (없으면 Enter)"
  if ($user -ne "") {
    $cfg.mqttUser = $user
    $cfg.mqttPass = Read-Secret "MQTT 비밀번호"
  }
  Write-Host "그룹: 토픽 i2r/<그룹>/in, out 의 그룹. Enter = 보드 MAC으로 자동 (보드끼리 연동할 때만 같은 이름을 넣음)"
  Write-Host "      공개 서버에서는 이메일이나 이름 같은 개인 정보를 넣지 마세요."
  $g = Read-Host "그룹"
  if ($g -ne "") { $cfg.g = $g }
}

$line = $cfg | ConvertTo-Json -Compress
$pw = $null; $cfg = $null

$p = New-Object System.IO.Ports.SerialPort $Port, 115200
$p.Encoding = [System.Text.Encoding]::UTF8
$p.NewLine = "`n"
$p.DtrEnable = $false   # 포트를 열 때 보드가 리셋되지 않게
$p.RtsEnable = $false
try {
  try { $p.Open() } catch { Write-Host "$Port 를 열 수 없습니다. 시리얼 모니터 등 다른 프로그램을 닫고 다시 실행하세요." -ForegroundColor Yellow; exit 1 }
  Start-Sleep -Milliseconds 300
  $null = $p.ReadExisting()
  $p.WriteLine($line)
  $line = $null
  Write-Host ""
  Write-Host "보냈습니다. 보드 응답을 8초 동안 읽습니다..."
  $end = (Get-Date).AddSeconds(8); $buf = ""
  while ((Get-Date) -lt $end) { $buf += $p.ReadExisting(); Start-Sleep -Milliseconds 200 }
  # 혹시 응답에 비밀번호가 섞여 나와도 화면에는 가린다
  $buf = $buf -replace '"(password|mqttPass)"\s*:\s*"[^"]*"', '"$1":"***"'
  Write-Host $buf
  if ($buf -match '"ok"\s*:\s*true') { Write-Host "`n[성공] 설정이 저장됐습니다. 보드가 다시 시작합니다." -ForegroundColor Green }
  else { Write-Host "`n[확인 필요] ok 응답을 받지 못했습니다. 보드에 I2R_IoT 예제가 올라가 있는지 확인하고, 위 내용을 알려 주세요." -ForegroundColor Yellow }
} finally {
  if ($p.IsOpen) { $p.Close() }
}
