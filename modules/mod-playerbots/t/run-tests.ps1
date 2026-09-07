# Local Windows runner for the mod-playerbots test suites that need no database:
# persistent_active_roster_tests, playerbot_event_store_contract_tests,
# world_thread_command_queue_tests and the playerbot_legacy_event_write_guard
# ctest.
#
# Ported from twow-repo, with three platform-only assumptions corrected:
#
#   1. -S pointed at $PSScriptRoot. That worked only while t/ carried its own
#      standalone CMakeLists.txt with a project() of its own. It does not any
#      more -- the suites are declared in modules/mod-playerbots/tests.cmake,
#      which the ROOT CMakeLists includes under BUILD_TESTING -- so the source
#      directory is the repository root and BUILD_TESTING=ON is passed.
#   2. -DROSTER_TEST_OPENSSL_LIBRARY was dropped: tests.cmake in core resolves
#      OpenSSL through OPENSSL_LIBRARIES / TW_OPENSSL_CRYPTO_LIBRARY and reads
#      no such variable. The -DependencyRuntime directory is still required and
#      still checked for libcrypto.lib, because it is what the built
#      executables load at run time.
#   3. The vcpkg prefix path was hardcoded to one machine's install. It is now
#      -VcpkgPrefixPath, defaulting to the same value so existing invocations
#      keep working.
#
# It builds and then runs ctest rather than launching one executable, so a suite
# added to tests.cmake later is picked up here without editing this file.

param(
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [Parameter(Mandatory=$true)][string]$DependencyRuntime,
    [string]$VcpkgPrefixPath='C:/TW/ComTW/vcpkg/installed/x64-windows',
    [string]$CMakeExecutable='C:\Program Files\CMake\bin\cmake.exe',
    [string]$LogPath=''
)
$ErrorActionPreference='Stop'
$cmake=$CMakeExecutable
$ctest=Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
# modules/mod-playerbots/t -> repository root.
$source=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$build=[IO.Path]::GetFullPath($BuildDirectory)
$runtime=(Resolve-Path -LiteralPath $DependencyRuntime).Path
$crypto=Join-Path $runtime 'libcrypto.lib'
if(!(Test-Path -LiteralPath $crypto -PathType Leaf)){throw "Dependency runtime does not contain libcrypto.lib: $runtime"}
if(!(Test-Path -LiteralPath (Join-Path $source 'CMakeLists.txt') -PathType Leaf)){throw "Repository root does not look like a CMake source tree: $source"}
if(Test-Path -LiteralPath $build){throw "Build directory already exists: $build"}
$transcript=[Text.StringBuilder]::new()

function Invoke-Clean([string]$file,[string[]]$arguments) {
    $psi=[Diagnostics.ProcessStartInfo]::new()
    $psi.FileName=$file;$psi.UseShellExecute=$false;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
    $psi.Environment.Clear()
    $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($entry in [Environment]::GetEnvironmentVariables().GetEnumerator()) {
        if($entry.Key -ieq 'Path' -or $entry.Key -eq 'CL'){continue}
        if($seen.Add([string]$entry.Key)){$psi.Environment[[string]$entry.Key]=[string]$entry.Value}
    }
    $psi.Environment['Path']=$runtime+';'+$env:Path
    foreach($argument in $arguments){[void]$psi.ArgumentList.Add($argument)}
    $process=[Diagnostics.Process]::Start($psi)
    $outTask=$process.StandardOutput.ReadToEndAsync();$errTask=$process.StandardError.ReadToEndAsync();$process.WaitForExit()
    $stdout=$outTask.GetAwaiter().GetResult();$stderr=$errTask.GetAwaiter().GetResult();$exit=$process.ExitCode;$process.Dispose()
    [void]$transcript.AppendLine("COMMAND=$file $($arguments -join ' ')")
    [void]$transcript.AppendLine("EXIT_CODE=$exit")
    [void]$transcript.Append($stdout);[void]$transcript.Append($stderr)
    if($stdout){[Console]::Out.Write($stdout)};if($stderr){[Console]::Error.Write($stderr)}
    if($exit-ne0){throw "Process failed with exit code ${exit}: $file"}
    return $stdout
}

Invoke-Clean $cmake @('-S',$source,'-B',$build,'-G','Visual Studio 17 2022','-A','x64','-DBUILD_TESTING=ON',("-DCMAKE_PREFIX_PATH="+$VcpkgPrefixPath)) | Out-Null
Invoke-Clean $cmake @('--build',$build,'--config','Release','--target','persistent_active_roster_tests','--parallel','2') | Out-Null
Invoke-Clean $cmake @('--build',$build,'--config','Release','--target','playerbot_event_store_contract_tests','--parallel','2') | Out-Null
Invoke-Clean $cmake @('--build',$build,'--config','Release','--target','world_thread_command_queue_tests','--parallel','2') | Out-Null

# ctest exits 0 on an empty test set, so the count is checked before the run --
# the same reason core's CI checks it. A green run that tested nothing is worse
# than a red one.
$listing=Invoke-Clean $ctest @('--test-dir',$build,'-C','Release','-N')
$count=0
if($listing -match '(?m)^Total Tests:\s*(\d+)\s*$'){$count=[int]$matches[1]}
if($count -lt 1){throw 'ctest registered no tests; the build configured but nothing would have run.'}
Invoke-Clean $ctest @('--test-dir',$build,'-C','Release','--output-on-failure') | Out-Null

if($LogPath){[IO.File]::WriteAllText([IO.Path]::GetFullPath($LogPath),$transcript.ToString(),[Text.UTF8Encoding]::new($false))}
