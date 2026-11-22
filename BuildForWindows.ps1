param(
    [string]$SdlRoot = (Join-Path $PSScriptRoot 'deps/SDL2-2.32.10'),
    [string]$MixerRoot = (Join-Path $PSScriptRoot 'deps/SDL2_mixer-2.8.1')
)

$ErrorActionPreference = 'Stop'
$build = Join-Path $PSScriptRoot 'build'
$sdl = (Resolve-Path -LiteralPath $SdlRoot).Path
$mixer = (Resolve-Path -LiteralPath $MixerRoot).Path
cmake -S $PSScriptRoot -B $build -G "Visual Studio 17 2022" -A x64 "-DSDL2_PATH=$sdl" "-DSDL2_MIXER_PATH=$mixer" -DCMAKE_WIN32_EXECUTABLE:BOOL=ON
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build $build --config Release --parallel
exit $LASTEXITCODE