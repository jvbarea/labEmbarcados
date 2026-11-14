<#
  Gera o APK do FELIS neste PC, sem conta Expo e sem nuvem.

    powershell -ExecutionPolicy Bypass -File scripts\build-apk.ps1            # so gera
    powershell -ExecutionPolicy Bypass -File scripts\build-apk.ps1 -Install   # gera e instala pelo cabo USB

  O que faz:
    1. copia o app para C:\feedcat-build\app (fora do OneDrive e sem espacos no
       caminho: o build nativo do Android falha com caminhos longos, e o OneDrive
       tentaria sincronizar milhares de arquivos de build);
    2. instala as dependencias la (so quando o package-lock.json mudou);
    3. gera o projeto Android (npx expo prebuild) e compila o APK de release;
    4. copia o APK para app\dist-apk\ e, com -Install, instala com o adb.

  Precisa de: Android Studio (SDK em %LOCALAPPDATA%\Android\Sdk), JDK 17
  (winget install Microsoft.OpenJDK.17) e Node. A primeira vez demora (baixa o
  Gradle e as bibliotecas); as seguintes sao bem mais rapidas.

  O APK sai assinado com a chave de DEBUG do template: instala normalmente no
  celular, mas nao serve para publicar na Play Store.
#>
param(
  [switch]$Install,
  [string]$BuildDir = 'C:\feedcat-build'
)

$ErrorActionPreference = 'Stop'
$app = Split-Path $PSScriptRoot -Parent
$dest = Join-Path $BuildDir 'app'

function Passo($texto) { Write-Host "`n==> $texto" -ForegroundColor Cyan }

# --- ferramentas -------------------------------------------------------------
$jdk = Get-ChildItem 'C:\Program Files\Microsoft' -Directory -Filter 'jdk-17*' -ErrorAction SilentlyContinue |
  Sort-Object Name -Descending | Select-Object -First 1
if (-not $jdk) {
  throw 'JDK 17 nao encontrado. Instale com: winget install Microsoft.OpenJDK.17'
}
$sdk = Join-Path $env:LOCALAPPDATA 'Android\Sdk'
if (-not (Test-Path $sdk)) {
  throw "Android SDK nao encontrado em $sdk. Abra o Android Studio uma vez para instalar."
}
# So nesta sessao: nao mexe nas variaveis do Windows.
$env:JAVA_HOME = $jdk.FullName
$env:ANDROID_HOME = $sdk
$env:ANDROID_SDK_ROOT = $sdk
$env:Path = "$($jdk.FullName)\bin;$sdk\platform-tools;$env:Path"
Write-Host "JDK: $($jdk.FullName)"
Write-Host "SDK: $sdk"

# --- 1. copia ---------------------------------------------------------------
Passo "Copiando o app para $dest"
New-Item -ItemType Directory -Force $dest | Out-Null
# /MIR espelha; as pastas em /XD ficam de fora da copia E da limpeza, entao o
# node_modules e o android do build anterior sao reaproveitados.
robocopy $app $dest /MIR /XD node_modules .expo android dist dist-apk web-build /XF *.log /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy falhou (codigo $LASTEXITCODE)" }

# --- 2. dependencias --------------------------------------------------------
$hash = (Get-FileHash (Join-Path $dest 'package-lock.json')).Hash
$marca = Join-Path $dest 'node_modules\.feedcat-lock-hash'
$jaInstalado = (Test-Path $marca) -and ((Get-Content $marca -Raw).Trim() -eq $hash)
Push-Location $dest
try {
  if (-not $jaInstalado) {
    Passo 'Instalando dependencias (npm ci)'
    npm ci --no-audit --no-fund
    if ($LASTEXITCODE -ne 0) { throw 'npm ci falhou' }
    Set-Content -Path $marca -Value $hash
  } else {
    Write-Host 'Dependencias ja instaladas (package-lock.json nao mudou).'
  }

  # --- 3. projeto Android e APK ---------------------------------------------
  Passo 'Gerando o projeto Android (expo prebuild)'
  npx expo prebuild --platform android --clean --no-install
  if ($LASTEXITCODE -ne 0) { throw 'expo prebuild falhou' }

  Passo 'Compilando o APK de release (Gradle) - a primeira vez leva varios minutos'
  Push-Location (Join-Path $dest 'android')
  try {
    .\gradlew.bat assembleRelease
    if ($LASTEXITCODE -ne 0) { throw 'Gradle falhou' }
  } finally {
    Pop-Location
  }
} finally {
  Pop-Location
}

# --- 4. entrega -------------------------------------------------------------
$gerado = Join-Path $dest 'android\app\build\outputs\apk\release\app-release.apk'
if (-not (Test-Path $gerado)) { throw "APK nao encontrado em $gerado" }
$versao = (Get-Content (Join-Path $app 'app.json') -Raw | ConvertFrom-Json).expo.version
$saida = Join-Path $app 'dist-apk'
New-Item -ItemType Directory -Force $saida | Out-Null
$apk = Join-Path $saida ("felis-{0}-{1}.apk" -f $versao, (Get-Date -Format 'yyyyMMdd-HHmm'))
Copy-Item $gerado $apk
Passo "APK pronto: $apk ($([math]::Round((Get-Item $apk).Length / 1MB, 1)) MB)"

if ($Install) {
  Passo 'Instalando no celular (adb)'
  $dispositivos = (adb devices) -match "`tdevice$"
  if (-not $dispositivos) {
    throw 'Nenhum celular no adb. Ligue a Depuracao USB, conecte o cabo e aceite o aviso no celular.'
  }
  adb install -r $apk
  if ($LASTEXITCODE -ne 0) { throw 'adb install falhou' }
  Write-Host 'Instalado. Abra o FELIS no celular.'
}
