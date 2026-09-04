<#
.SYNOPSIS
    Automated testlab pipeline for Tortoise-WoW and Playerbots compilation and deployment.
.DESCRIPTION
    This script automates the full deployment workflow, including client data verification,
    vcpkg dependency check, Git synchronization with submodules, CMake/MSBuild compilation,
    directory restructuring, configuration tuning, and automated database generation.
.PARAMETER SkipBotRegen
    A switch parameter to bypass the time-consuming process of deleting and regenerating
    the characters and logon databases. When active, a full backup of 'tw_char' and 'tw_logon'
    is generated via mysqldump, and restored automatically at the end of the pipeline.
.EXAMPLE
    .\Setup-Testlab.ps1
    Runs the complete clean pipeline, dropping all databases and regenerating bots from scratch.
.EXAMPLE
    .\Setup-Testlab.ps1 -SkipBotRegen
    Runs the fast pipeline. Preserves your existing game accounts, GM characters, and playerbot data.
.PARAMETER applyPatches
    A semicolon-separated string of external git commit hashes to cherry-pick onto the active branch context.
.EXAMPLE
    .\Setup-Testlab.ps1 -applyPatches "0ee0748"
    Runs the clean pipeline and dynamically injects the SkillRaceClassInfo DBC Override System hotfix.
.EXAMPLE
    .\Setup-Testlab.ps1 -applyPatches "0ee0748;abc1234" -SkipBotRegen
    Runs the fast pipeline, applies multiple external commit patches sequentially, and preserves character data.
.EXAMPLE
    .\Setup-Testlab.ps1 -WorkspaceRoot C:\WOW\testlab -VcpkgDirectory D:\vcpkg
    Runs the script straight out of the repository against a testlab folder elsewhere on disk.
.NOTES
    Windows only (PowerShell 5.1+, Visual Studio 2022, CMake). See README.md next to this
    script for the folder layout it expects and what you have to supply yourself.
#>
param (
    # Switch to bypass character database drop, playerbot data import, and configuration wipe
    [switch]$SkipBotRegen,

	# Dynamic string sequence containing commit hashes separated by semicolons (e.g. "0ee0748;abc1234")
    [string]$applyPatches,

    # Testlab root: the folder holding 'server\' and the 'tortoise-wow\' checkout. Defaults to
    # the directory this script sits in, which is the layout you get by copying the script out
    # of the repository into an empty working folder. Point it elsewhere to run the script
    # straight out of a checkout: -WorkspaceRoot C:\WOW\testlab
    [string]$WorkspaceRoot = $PSScriptRoot,

    # vcpkg installation providing ACE and Boost for x64-windows
    [string]$VcpkgDirectory = "C:\WOW\vcpkg",

    # MariaDB credentials. Defaults match the portable MariaDB this testlab ships with;
    # override them rather than editing the script body.
    [string]$RootPassword = "mangos",
    [string]$DbPassword   = "mangos",

    # Name of the portable MariaDB directory inside server\
    [string]$MariaDbFolderName = "mariadb-10.3.39-winx64"
)

# StrictMode turns a typo'd or never-assigned variable into a hard error instead of an
# empty string. Without it, '$NewDumpPDirSetting' (a typo for $NewPDumpDirSetting) and an
# undefined $ElunaScriptPath silently rewrote mangosd.conf with a blank PDumpDir and an
# empty Eluna.ScriptPath - the pipeline reported success and the server lost both settings.
Set-StrictMode -Version Latest

[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

# Credential files are created in step 00 and removed by Stop-Pipeline / the final cleanup.
# Declared up front so the cleanup helper can always test them under StrictMode.
$script:RootDefaultsFile = $null
$script:DbDefaultsFile   = $null

# Run lock. $LockOwned stays false until THIS run creates the file, so aborting because
# somebody else's run holds the lock can never delete their lock on the way out.
$script:LockFile  = $null
$script:LockOwned = $false

# Start recording everything that appears in the PowerShell console window
Start-Transcript -Path (Join-Path $WorkspaceRoot "pipeline_console.log") -Append -ErrorAction SilentlyContinue


# ==============================================================================
# FUNCTIONS DEFINITION
# ==============================================================================
function Write-PipelineHeader {
    param (
        [string]$StepName
    )
    $Line = "=" * 80
    Write-Host ""
    Write-Host $Line -ForegroundColor Cyan
    Write-Host ">>> PIPELINE STEP $($StepName.ToUpper())" -ForegroundColor Yellow
    Write-Host $Line -ForegroundColor Cyan
    Write-Host ""
}

# Deletes the temporary MariaDB credential files. Safe to call more than once.
function Remove-PipelineCredentialFiles {
    foreach ($CredentialFile in @($script:RootDefaultsFile, $script:DbDefaultsFile)) {
        if ($CredentialFile -and (Test-Path $CredentialFile)) {
            Remove-Item -Path $CredentialFile -Force -ErrorAction SilentlyContinue
        }
    }
}

# Removes this run's lock file - but only if this run is the one that created it.
function Remove-PipelineLock {
    if ($script:LockOwned -and $script:LockFile -and (Test-Path $script:LockFile)) {
        Remove-Item -Path $script:LockFile -Force -ErrorAction SilentlyContinue
    }
    $script:LockOwned = $false
}

# Single exit path for the whole pipeline: report, release the lock, drop the credential
# files, close the transcript, then leave with a meaningful exit code.
function Stop-Pipeline {
    param (
        [string]$Message,
        [int]$ExitCode = 1
    )
    if ($Message) { Write-Error $Message }
    Remove-PipelineLock
    Remove-PipelineCredentialFiles
    try { Stop-Transcript | Out-Null } catch { }
    exit $ExitCode
}

# Refuses to start a second run on top of a live one.
#
# The pipeline drops databases and wipes the server directory; two of them interleaved
# would corrupt the result in ways that are painful to diagnose. A lock left behind by a
# crashed or killed run is detected and taken over, so a stale file never blocks the
# testlab permanently.
function Assert-NoConcurrentRun {
    if (-not (Test-Path $script:LockFile)) { return }

    $LockData = @{}
    foreach ($Line in [System.IO.File]::ReadAllLines($script:LockFile)) {
        if ($Line -match '^\s*([^=]+?)\s*=\s*(.*)$') { $LockData[$Matches[1]] = $Matches[2] }
    }

    $OwnerPid     = 0
    $OwnerStarted = if ($LockData.ContainsKey('Started')) { $LockData['Started'] } else { 'unknown time' }
    if ($LockData.ContainsKey('Pid')) { [void][int]::TryParse($LockData['Pid'], [ref]$OwnerPid) }

    $OwnerAlive = $false
    if ($OwnerPid -gt 0) {
        $OwnerProcess = Get-Process -Id $OwnerPid -ErrorAction SilentlyContinue
        # Windows recycles PIDs. Only treat it as a live run when the process that holds the
        # PID is actually a PowerShell host, otherwise an unrelated program inheriting the
        # number would block the pipeline forever.
        if ($OwnerProcess -and @('powershell', 'pwsh') -contains $OwnerProcess.ProcessName) {
            $OwnerAlive = $true
        }
    }

    if ($OwnerAlive) {
        Stop-Pipeline -Message ("Another pipeline run is already in progress (PID $OwnerPid, started $OwnerStarted). " +
                                "Wait for it to finish. If you are certain it is gone, delete $($script:LockFile) and run again.") `
                      -ExitCode 2
    }

    Write-Warning "Stale lock found from PID $OwnerPid (started $OwnerStarted) - that run never finished cleanly. Taking over."
    Remove-Item -Path $script:LockFile -Force -ErrorAction SilentlyContinue
}

# Records that a run is underway, for the next run and for anyone looking at the folder.
function New-PipelineLock {
    $LockContent = @(
        "Pid=$PID"
        "Started=$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
        "User=$env:USERNAME"
        "Machine=$env:COMPUTERNAME"
        "SkipBotRegen=$SkipBotRegen"
        "WorkspaceRoot=$WorkspaceRoot"
        "Script=$PSCommandPath"
    ) -join "`r`n"

    [System.IO.File]::WriteAllText($script:LockFile, ($LockContent + "`r`n"), (New-Object System.Text.UTF8Encoding($false)))
    $script:LockOwned = $true
}

# Aborts the pipeline when the last native command reported a failure. Native tools
# (git, cmake, mysql) do NOT raise PowerShell errors - without this check the pipeline
# happily continues on a failed clone or a failed CMake configure and reports success.
function Assert-LastExitCode {
    param (
        [string]$Message
    )
    if ($LASTEXITCODE -ne 0) {
        Stop-Pipeline -Message "$Message (exit code $LASTEXITCODE)" -ExitCode $LASTEXITCODE
    }
}

# Writes a MariaDB option file holding the credentials, so no password is ever passed on a
# command line where any local user can read it out of the process list.
# The file is UTF8 *without* BOM - a BOM makes MariaDB's option parser reject the file.
function New-MySqlDefaultsFile {
    param (
        [Parameter(Mandatory = $true)][string]$User,
        [Parameter(Mandatory = $true)][string]$Password
    )

    $CredentialPath = Join-Path $env:TEMP ("tw_pipeline_{0}_{1}.cnf" -f $User, [guid]::NewGuid().ToString('N'))
    $Content        = "[client]`r`nuser=$User`r`npassword=$Password`r`n"
    [System.IO.File]::WriteAllText($CredentialPath, $Content, (New-Object System.Text.UTF8Encoding($false)))

    # The file holds a plaintext credential: strip inherited permissions and grant the
    # current user only.
    try {
        $Acl = Get-Acl -Path $CredentialPath
        $Acl.SetAccessRuleProtection($true, $false)
        $Rule = New-Object System.Security.AccessControl.FileSystemAccessRule(
            [System.Security.Principal.WindowsIdentity]::GetCurrent().Name, 'FullControl', 'Allow')
        $Acl.SetAccessRule($Rule)
        Set-Acl -Path $CredentialPath -AclObject $Acl
    } catch {
        Write-Warning "Could not tighten permissions on the temporary credential file: $_"
    }

    return $CredentialPath
}

# Runs a single SQL statement block.
function Invoke-MySqlQuery {
    param (
        [Parameter(Mandatory = $true)][string]$Query,
        [string]$Database,
        [string]$DefaultsFile,
        [string]$FailureMessage = "MariaDB query failed",
        [switch]$AllowFailure
    )

    if (-not $DefaultsFile) { $DefaultsFile = $script:RootDefaultsFile }

    $Arguments = @("--defaults-extra-file=$DefaultsFile", "--default-character-set=utf8mb4", "-e", $Query)
    if ($Database) { $Arguments += $Database }

    & $MariaDBPath @Arguments
    if (-not $AllowFailure) { Assert-LastExitCode -Message $FailureMessage }
}

# Imports a .sql file.
#
# Two things this deliberately does NOT do:
#   1. It does not pipe the file through PowerShell (`Get-Content file | mysql`). That
#      marshals every single line into the child process one at a time and is orders of
#      magnitude slower on the multi-megabyte world dumps.
#   2. It does not hand the raw file to MariaDB either: 185 files under sql/base carry the
#      MariaDB 11 "enable the sandbox mode" preamble, which the bundled 10.3 client cannot
#      parse. The file is streamed into a filtered temporary copy first, which also
#      normalises the encoding to UTF8 without BOM.
function Invoke-MySqlFile {
    param (
        [Parameter(Mandatory = $true)][string]$Path,
        [string]$Database,
        [string]$DefaultsFile,
        [string]$FailureMessage = "MariaDB import failed",
        [switch]$AllowFailure
    )

    if (-not $DefaultsFile) { $DefaultsFile = $script:RootDefaultsFile }

    $FilteredPath = Join-Path $env:TEMP ("tw_import_{0}.sql" -f [guid]::NewGuid().ToString('N'))
    $Writer = New-Object System.IO.StreamWriter($FilteredPath, $false, (New-Object System.Text.UTF8Encoding($false)))
    try {
        foreach ($Line in [System.IO.File]::ReadLines($Path)) {
            if ($Line -notmatch 'sandbox mode') { $Writer.WriteLine($Line) }
        }
    } finally {
        $Writer.Dispose()
    }

    $Arguments = @("--defaults-extra-file=$DefaultsFile", "--default-character-set=utf8mb4")
    if ($Database) { $Arguments += $Database }

    try {
        # Start-Process feeds the file straight into the client's stdin - no per-line
        # PowerShell pipeline, no re-encoding.
        $ImportProcess = Start-Process -FilePath $MariaDBPath `
                                       -ArgumentList $Arguments `
                                       -RedirectStandardInput $FilteredPath `
                                       -NoNewWindow `
                                       -PassThru `
                                       -Wait

        if ($ImportProcess.ExitCode -ne 0 -and -not $AllowFailure) {
            Stop-Pipeline -Message "$FailureMessage : $Path (exit code $($ImportProcess.ExitCode))" `
                          -ExitCode $ImportProcess.ExitCode
        }
    } finally {
        Remove-Item -Path $FilteredPath -Force -ErrorAction SilentlyContinue
    }
}

# Writes one of the server launcher .bat files, but only when it is not already there -
# these are the operator's entry points and may well have been hand-tuned, so an existing
# file is always left alone.
#
# Content is deliberately plain ASCII: .bat files are read by cmd.exe in the OEM code page,
# so accented characters in a comment would come out as mojibake on a Czech console.
function New-ServerLauncherScript {
    param (
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Content
    )

    $LauncherName = Split-Path $Path -Leaf

    if (Test-Path $Path) {
        Write-Host " -> [SKIP] Launcher already present, left untouched: $LauncherName"
        return
    }

    # ASCII + CRLF, the only combination cmd.exe is guaranteed to read back correctly.
    [System.IO.File]::WriteAllText($Path, ($Content -replace "`r?`n", "`r`n"), [System.Text.Encoding]::ASCII)
    Write-Host " -> Created launcher: $LauncherName" -ForegroundColor Green
}

# MariaDB credentials and the vcpkg location come in through param() at the top of this
# script - override them on the command line instead of editing anything down here.

# Repository VARIABLES
$RepoUrl    = "https://github.com/Shyalya/tortoise-wow.git"

$BranchName = "playerbots-integration-gh"

# PLAYERBOTS TESTLAB SCALING VARIABLES
$MinRandomBots     = 5
$MaxRandomBots     = 10
$RandomBotMinLevel = 1
$RandomBotMaxLevel = 20
$RandomBotAccountsCount = 10

# CORE ENGINE SYSTEM DIRECTORIES PARAMETERS
$MangosBinDir   = "bin"
$MangosLibDir   = "lib"
$MangosEtcDir   = "etc"
$MangosDataDir   = "data"
$MangosLogsDir   = "logs"
$MangosHonorDir  = "honor"
$MangosPDumpDir   = "pdump"
$MangosLuaDir	 = "lua_scripts"
$MangosToolsDir   = "tools"
$MangosInstalationDir = "server"
$MangosBuildDir = "build"
$MangosTortoiseSourceDir = "tortoise-wow"
$MangosPipelineBackupDir = "pipeline_backups"

# NETWORK PARAMETERS
$RealmlistIPAddress = "127.0.0.1"
$RealmlistPort = "8090"

# STEP variable definitions
Write-PipelineHeader -StepName "00: Starting variable definitions"
Write-Host "Setting up variables"
# Everything below hangs off the testlab root (-WorkspaceRoot, default: this script's folder)
$ScriptDirectory = $WorkspaceRoot

if (-not (Test-Path $ScriptDirectory)) {
    Stop-Pipeline -Message "Workspace root does not exist: $ScriptDirectory"
}

# Refuse to run on top of a live run, then publish our own marker.
$script:LockFile = Join-Path $ScriptDirectory "pipeline_running.lock"
Assert-NoConcurrentRun
New-PipelineLock
Write-Host "Run lock acquired: $($script:LockFile) (PID $PID)"

# Define absolute paths based on the workspace root
$MariaDBPath      = "$ScriptDirectory\$MangosInstalationDir\$MariaDbFolderName\bin\mysql.exe"
$SqlBaseDirectory = "$ScriptDirectory\$MangosTortoiseSourceDir\sql\base"
$CreateDatabasesSql = "$ScriptDirectory\$MangosTortoiseSourceDir\sql\create_databases.sql"
# Define local build artifacts directories
$SourceDir  = Join-Path $ScriptDirectory $MangosTortoiseSourceDir
$BuildDir   = Join-Path $SourceDir $MangosBuildDir
$InstallDir = Join-Path $ScriptDirectory $MangosInstalationDir # Targeted server binaries directory
$BackupFolder  = Join-Path $ScriptDirectory $MangosPipelineBackupDir

# Materialise the credentials into option files (see New-MySqlDefaultsFile).
$script:RootDefaultsFile = New-MySqlDefaultsFile -User "root"   -Password $RootPassword
$script:DbDefaultsFile   = New-MySqlDefaultsFile -User "mangos" -Password $DbPassword

# Verify the retrieved path
Write-Host "[OK] Variables were set up."  -ForegroundColor Green

# ==============================================================================
# PIPELINE STEP 01: CLIENT DATA INTEGRITY & DBC HASH VERIFICATION
# ==============================================================================
Write-PipelineHeader -StepName "01: Client Data Verification"
Write-Host "Initializing client data integrity verification..."

# Define targeted data paths within your pipeline structure
$DataRoot     = Join-Path $ScriptDirectory "$MangosInstalationDir\$MangosDataDir"
# The DBC hash manifest ships next to this script in the repository, but a workspace copy
# takes precedence so a testlab can pin its own client build without editing the tool.
$JsonVerifier = Join-Path $ScriptDirectory "dbc_verifier.json"
if (-not (Test-Path $JsonVerifier)) {
    $JsonVerifier = Join-Path $PSScriptRoot "dbc_verifier.json"
}

# 1. Verify existence of required data subdirectories
$RequiredFolders = @("dbc", "maps", "vmaps", "mmaps")
$DataFoldersValid = $true

foreach ($Folder in $RequiredFolders) {
    $TargetFolder = Join-Path $DataRoot $Folder
    if (-not (Test-Path $TargetFolder)) {
        Write-Warning "Required data directory is missing: $TargetFolder"
        $DataFoldersValid = $false
    }
}

if (-not $DataFoldersValid) {
    Stop-Pipeline -Message "Client data directories verification failed. Please extract maps/dbc files before running the pipeline."
}
Write-Host "[OK] All required data directories (dbc, maps, vmaps, mmaps) are present." -ForegroundColor Green

# 2. Verify SHA256 hashes of DBC files using the local JSON definitions file
if (-not (Test-Path $JsonVerifier)) {
    Stop-Pipeline -Message "Verification blueprint missing. Could not locate json manifest at: $JsonVerifier"
}

Write-Host "Reading SHA256 blueprint from dbc_verifier.json..."

# Using native .NET file stream engine to completely bypass any Windows PowerShell BOM/Encoding parser bugs
$AbsoluteJsonPath = [System.IO.Path]::GetFullPath($JsonVerifier)
$JsonRawContent = [System.IO.File]::ReadAllText($AbsoluteJsonPath)
$DbcManifest = $null

try {
    $DbcManifest = ConvertFrom-Json $JsonRawContent -ErrorAction Stop
} catch {
    Stop-Pipeline -Message "Failed to parse dbc_verifier.json manifest structure. Ensure it is valid JSON. Error: $_"
}

# Safety check: If the manifest object remains null or empty, terminate execution immediately
if ($null -eq $DbcManifest) {
    Stop-Pipeline -Message "DBC manifest parsing yielded an empty configuration object. Terminating pipeline execution."
}

$HashVerificationPassed = $true
Write-Host "Verifying DBC file signatures integrity..."

# Iterate through each defined file inside the JSON manifest properties
foreach ($DbcFile in $DbcManifest.psobject.Properties) {
    $FileName    = $DbcFile.Name
    $ExpectedHash = $DbcFile.Value
    $FullFilePath = Join-Path (Join-Path $DataRoot "dbc") $FileName

    if (-not (Test-Path $FullFilePath)) {
        Write-Warning "DBC file declared in manifest is missing from directory: $FileName"
        $HashVerificationPassed = $false
        continue
    }

    # Compute the local file SHA256 hash checksum stream matching standard formats
    $FileHashResult = Get-FileHash -Path $FullFilePath -Algorithm SHA256
    $ActualHash     = $FileHashResult.Hash.ToLower()
    $ExpectedHash   = $ExpectedHash.ToLower()

    if ($ActualHash -ne $ExpectedHash) {
        Write-Warning "Hash mismatch detected on file: $FileName"
        Write-Warning " -> Expected: $ExpectedHash"
        Write-Warning " -> Detected: $ActualHash"
        $HashVerificationPassed = $false
    }
}

if (-not $HashVerificationPassed) {
    Stop-Pipeline -Message "DBC integrity verification failed. Version mismatch detected against build requirements."
}
Write-Host "[OK] All DBC file signatures successfully verified against SHA256 blueprint." -ForegroundColor Green

# ==============================================================================
# PIPELINE STEP 02: VCPKG DEPENDENCY CHECK
# ==============================================================================
Write-PipelineHeader -StepName "02: Vcpkg Dependency Check"
Write-Host "Verifying external C++ library environments via vcpkg toolchain..."

# Define the absolute path to your vcpkg installation directory
$VcpkgExecutable = Join-Path $VcpkgDirectory "vcpkg.exe"

# Verify that the vcpkg engine exists before triggering package installation
if (-not (Test-Path $VcpkgExecutable)) {
    Stop-Pipeline -Message "Critical component missing: Could not locate vcpkg executable at $VcpkgExecutable"
}

Write-Host "Starting library deployment (ACE and Boost modules) for x64-windows..."

# Build the string array containing all required modules for playerbots integration
$VcpkgArguments = @(
    "install",
    "ace:x64-windows",
    "boost-algorithm:x64-windows",
    "boost-asio:x64-windows",
    "boost-bimap:x64-windows",
    "boost-bind:x64-windows",
    "boost-filesystem:x64-windows",
    "boost-functional:x64-windows",
    "boost-smart-ptr:x64-windows",
    "boost-stacktrace:x64-windows",
    "boost-thread:x64-windows",
    "boost-system:x64-windows"
)

# Execute vcpkg inside its own folder without shifting the global pipeline context
# -NoNewWindow channels the compilation logs directly into the current pipeline frame
# -Wait forces the pipeline to halt until vcpkg safely evaluates all requirements
# The argument array is passed straight through - joining it into one string first would
# break the moment any path or triplet contained a space.
$VcpkgProcess = Start-Process -FilePath $VcpkgExecutable `
                              -ArgumentList $VcpkgArguments `
                              -WorkingDirectory $VcpkgDirectory `
                              -NoNewWindow `
                              -PassThru `
                              -Wait

# Evaluate the final runtime output code delivered by the vcpkg module compiler
if ($VcpkgProcess.ExitCode -ne 0) {
    Stop-Pipeline -Message "Vcpkg deployment sequence failed with exit code $($VcpkgProcess.ExitCode). Aborting pipeline." `
                  -ExitCode $VcpkgProcess.ExitCode
}

Write-Host "[OK] All required libraries (ACE and Boost modules) are fully verified and deployed." -ForegroundColor Green


# ==============================================================================
# PIPELINE STEP 03: GIT SOURCE MANAGEMENT (WITH SUBMODULES SUPPORT)
# ==============================================================================
Write-PipelineHeader -StepName "03: Git Source Management"
Write-Host "Managing repository source files and submodules..."

if (-not (Test-Path $SourceDir)) {
    Write-Host "Repository not found. Cloning branch '$BranchName' with all submodules..."

    # --recurse-submodules forces Git to automatically clone Eluna and any other nested modules
    git clone --branch $BranchName --recurse-submodules $RepoUrl $SourceDir
    Assert-LastExitCode -Message "git clone of '$BranchName' failed"

    Write-Host "[OK] Repository and all nested submodules successfully cloned." -ForegroundColor Green
} else {
    Write-Host "Repository found. Syncing latest code changes and submodules layout..."

    # Move context temporarily to repository folder to execute git updates securely
    Push-Location $SourceDir

    # Checkout target branch and pull core engine changes
    git checkout $BranchName
    Assert-LastExitCode -Message "git checkout of '$BranchName' failed"

    git pull
    Assert-LastExitCode -Message "git pull failed"

    Write-Host "Synchronizing and updating git submodules (Eluna engine)..."
    # Update and initialize any new or existing submodules recursively
    git submodule update --init --recursive
    Assert-LastExitCode -Message "git submodule update failed"

    Pop-Location
    Write-Host "[OK] Repository source files and submodules are fully up to date." -ForegroundColor Green
}

# ==============================================================================
# PIPELINE SUB-STEP (OPTIONAL): DYNAMIC CHERRY-PICK HOTFIXES
# ==============================================================================
if (-not [string]::IsNullOrEmpty($applyPatches)) {
    Write-Host "Evaluating dynamic hotfix runtime patches list..."

    $CommitHashesList = $applyPatches -split ';' | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne "" }

    # 1. CRITICAL: Jump inside the cloned repository directory FIRST before calling Git
    Push-Location $SourceDir

    # 2. FORCE CLEANUP FIRST: If a previous pipeline run crashed, abort any stuck cherry-pick states immediately
    Write-Host "Cleaning up any stuck or unresolved repository states..."
    git cherry-pick --abort 2>$null

    # 3. Synchronize remote mapping nodes securely
    #    ('pengle' is the misspelling this script used before - dropped too, so an existing
    #     workspace does not keep a stale duplicate remote around.)
    $TargetRemoteUrl = "https://github.com/Penqle/tortoise-wow.git"
    git remote remove pengle 2>$null
    git remote remove penqle 2>$null
    git remote add penqle $TargetRemoteUrl
    Assert-LastExitCode -Message "Could not register the 'penqle' git remote"

    # 4. Fetch ALL tracking branches (including 1181dev) from Penqle node explicitly
    Write-Host "Performing deep object fetch from Penqle fork layout..."
    git fetch penqle
    Assert-LastExitCode -Message "git fetch from the 'penqle' remote failed"

    # 5. Preserve any work in progress. This used to be a bare `git reset --hard HEAD` per
    #    patch, which silently destroyed every uncommitted local change in the source tree.
    #    Stashing keeps them recoverable with `git stash pop`.
    $WorkingTreeState = git status --porcelain
    if ($WorkingTreeState) {
        $StashLabel = "pipeline auto-stash $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
        Write-Warning "Uncommitted changes detected in the source tree. Stashing them as '$StashLabel'."
        Write-Warning "Recover them afterwards with: git -C `"$SourceDir`" stash pop"
        git stash push -u -m $StashLabel
        Assert-LastExitCode -Message "Could not stash local changes before applying patches"
    }

    foreach ($CommitHash in $CommitHashesList) {
        Write-Host "Processing signature checks for patch entry: $CommitHash"

        # 1. Retrieve the unique commit subject/message directly from the remote reference
        $CommitSubject = git log $CommitHash -n 1 --format="%s" 2>$null

        # 2. Check if this exact text subject already exists anywhere inside our local branch history
        $IsAlreadyInTree = $false
        if (-not [string]::IsNullOrEmpty($CommitSubject)) {
            $DuplicateCheck = git log -n 100 --format="%s" | Where-Object { $_ -eq $CommitSubject }
            if ($DuplicateCheck) { $IsAlreadyInTree = $true }
        }

        # Extra search safety backup using hash text grepping inside the active branch logs layout
        $CommitInLog = git log -n 100 --format="%H" | Where-Object { $_ -eq $CommitHash }

        if (-not $IsAlreadyInTree -and -not $CommitInLog) {
            Write-Host "Applying runtime custom code patch ($CommitHash) via automated cherry-pick stream..." -ForegroundColor Yellow

            # 3. Trigger the code injection stitching
            git cherry-pick $CommitHash

            if ($LASTEXITCODE -ne 0) {
                # If Git tells us the patch became empty, it means the changes are already fully present.
                # In that case, we gracefully skip the error and don't abort the build workspace context.
                $StatusOutput = git status
                if ($StatusOutput -match "cherry-pick is now empty") {
                    git cherry-pick --skip 2>$null
                    Write-Host " -> [OK] Custom patch $CommitHash was already integrated into the active layout tree." -ForegroundColor Green
                } else {
                    git cherry-pick --abort
                    Pop-Location
                    Stop-Pipeline -Message "CRITICAL: Cherry-pick collision conflict detected on hash $CommitHash. Patch dropped and pipeline stopped - resolve it manually before rebuilding."
                }
            } else {
                Write-Host " -> [OK] Custom patch $CommitHash compiled into active local build history layout." -ForegroundColor Green
            }
        } else {
            Write-Host " -> [SKIP] Custom patch hash $CommitHash (or its content) is already active in current history tree." -ForegroundColor Green
        }
    }

    # 6. Safely return back to the root pipeline workspace
    Pop-Location
}
# ==============================================================================
# PIPELINE STEP (OPTIONAL): CONDITIONAL BACKUP SECTION: EXPORT ENTIRE DATABASE STRUCTURES
# ==============================================================================
# IMPORTANT: this is the pipeline's only protection for the character data. Step 05 imports
# create_databases.sql, which carries `USE tw_char;` plus DROP TABLE for every table in it -
# so tw_char IS wiped further down even with -SkipBotRegen, and only the restore below puts
# it back. An unnoticed bad dump here therefore means permanent data loss, which is why the
# result is verified before the pipeline is allowed to touch anything.
if ($SkipBotRegen -and (Test-Path $MariaDBPath)) {
    Write-PipelineHeader -StepName "(OPTIONAL): Conditional step: export entire database structure and data"
	Write-Host "Parameter -SkipBotRegen is active. Generating full database dumps via mysqldump..."

    # Define the backup dump utility path matching your MariaDB environment
    $MySQLDumpPath = Join-Path (Split-Path $MariaDBPath -Parent) "mysqldump.exe"
    if (-not (Test-Path $MySQLDumpPath)) {
        Stop-Pipeline -Message "-SkipBotRegen requires mysqldump.exe, which is missing at: $MySQLDumpPath"
    }

    # Ensure a temporary backup directory exists on the disk layout
    New-Item -ItemType Directory -Path $BackupFolder -Force | Out-Null

    $TargetDbs = @("tw_char", "tw_logon")

    foreach ($DbName in $TargetDbs) {
        $BackupFile = Join-Path $BackupFolder "${DbName}_backup.sql"
        Write-Host " -> Exporting complete database: $DbName -> $($BackupFile)"

        # --result-file lets mysqldump write the file itself. Piping it through PowerShell
        # into Set-Content re-encoded the dump (ANSI on Windows PowerShell 5.1) and mangled
        # every non-ASCII character name, guild name and mail body in it.
        # --routines and --triggers ensure full feature set preservation.
        & $MySQLDumpPath "--defaults-extra-file=$($script:RootDefaultsFile)" `
                         --default-character-set=utf8mb4 `
                         --routines --triggers `
                         "--result-file=$BackupFile" `
                         $DbName
        Assert-LastExitCode -Message "mysqldump of '$DbName' failed - refusing to continue, your data is still intact"

        # Verify the dump really is a complete one before anything destructive runs.
        # mysqldump closes every successful dump with a '-- Dump completed' trailer.
        if (-not (Test-Path $BackupFile)) {
            Stop-Pipeline -Message "mysqldump reported success but produced no file for '$DbName'. Aborting before any data is dropped."
        }

        $BackupSize = (Get-Item $BackupFile).Length
        $BackupTail = Get-Content -Path $BackupFile -Tail 5 -ErrorAction SilentlyContinue

        if ($BackupSize -lt 1024 -or -not ($BackupTail -match 'Dump completed')) {
            Stop-Pipeline -Message ("Backup of '$DbName' looks truncated ($BackupSize bytes, no 'Dump completed' trailer). " +
                                    "Aborting before any data is dropped. File kept at: $BackupFile")
        }

        Write-Host "   [OK] Database '$DbName' fully backed up and verified ($BackupSize bytes)." -ForegroundColor Green
    }
}

# ==============================================================================
# PIPELINE STEP 04: PIPELINE INITIALIZATION: CLEANUP PREVIOUS ENVIRONMENT
# ==============================================================================
# STEP 1: Environment Cleanup
Write-PipelineHeader -StepName "04: Environment Cleanup"
Write-Host "Stopping active server instances and dropping previous databases..."
# Define the path to the MySQL binary for early cleanup tasks
$BinDir  = Join-Path $InstallDir $MangosBinDir
$EtcDir  = Join-Path $InstallDir $MangosEtcDir
$LibDir  = Join-Path $InstallDir $MangosLibDir
$LogsDir  = Join-Path $InstallDir $MangosLogsDir
$PdumpDir  = Join-Path $InstallDir $MangosPDumpDir
$HonorDir  = Join-Path $InstallDir $MangosHonorDir
$ToolsDir = Join-Path $InstallDir $MangosToolsDir
$LuaDir = Join-Path $InstallDir $MangosLuaDir

Write-Host "Initializing pipeline cleanup sequence..."

# 1. Terminate any running server processes to release database and file locks
Write-Host "Stopping any active server instances..."
Stop-Process -Name "mangosd", "realmd" -ErrorAction SilentlyContinue

# Allow a brief moment for processes to gracefully release network ports and file descriptors
Start-Sleep -Seconds 2

# Clear previously generated server subdirectories.
# Only these subdirectories are removed - the server root itself (and the launcher .bat
# files and the mariadb installation sitting in it) is left untouched.
Write-Host "Clearing previously generated server directories..."
$GeneratedFolders = @($BinDir, $EtcDir, $LibDir, $LogsDir, $PdumpDir, $HonorDir, $ToolsDir, $LuaDir)
foreach ($Folder in $GeneratedFolders) {
    if (Test-Path $Folder) {
        # Recursively remove all contents and the folder itself
        Remove-Item -Path $Folder -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host " -> Removed directory: $Folder"
    }
}

# Drop existing test databases based on the pipeline arguments
Write-Host "Dropping previous testbed databases..."
if (Test-Path $MariaDBPath) {
    # Core infrastructure databases that are ALWAYS dropped and rebuilt
    Invoke-MySqlQuery -Query "DROP DATABASE IF EXISTS tw_world; DROP DATABASE IF EXISTS tw_logon; DROP DATABASE IF EXISTS tw_logs;" `
                      -FailureMessage "Could not drop the tw_world / tw_logon / tw_logs databases"

    if (-not $SkipBotRegen) {
        Write-Host " -> Parameter -SkipBotRegen not active. Dropping characters database..."
        Invoke-MySqlQuery -Query "DROP DATABASE IF EXISTS tw_char;" -FailureMessage "Could not drop the tw_char database"
    } else {
        Write-Host " -> [SKIP] Parameter -SkipBotRegen is active. Retaining existing character and playerbot data." -ForegroundColor Green
    }
    Write-Host "[OK] Target databases cleanup sequence completed." -ForegroundColor Green
} else {
    Stop-Pipeline -Message "Could not locate mysql.exe at the specified path: $MariaDBPath"
}

# ==============================================================================
# PIPELINE STEP 05: DATABASE GENERATION AND IMPORTS
# ==============================================================================
Write-PipelineHeader -StepName "05: Database Generation & Imports"
Write-Host "Starting database structure setup..."

# Create default databases containers (tw_world, tw_char, tw_logon, tw_logs)
if (-not (Test-Path $CreateDatabasesSql)) {
    Stop-Pipeline -Message "Database schema script is missing, cannot create any database: $CreateDatabasesSql"
}

Write-Host "Creating databases..."
Invoke-MySqlFile -Path $CreateDatabasesSql -FailureMessage "Importing create_databases.sql failed"

# Bulk import all base world SQL files with safety filters enabled
if (-not (Test-Path $SqlBaseDirectory)) {
    Stop-Pipeline -Message "Base world SQL directory is missing: $SqlBaseDirectory"
}

Write-Host "Importing base world SQL files..."
Get-ChildItem "$SqlBaseDirectory\*.sql" | Sort-Object Name | ForEach-Object {
    Write-Host "Importing: $($_.Name)"
    Invoke-MySqlFile -Path $_.FullName -Database "tw_world" -FailureMessage "Importing a base world SQL file failed"
}

# ==============================================================================
# PIPELINE STEP 06: DATABASE USER CONFIGURATION
# ==============================================================================
Write-PipelineHeader -StepName "06: The 'mangos' user has been configured"
Write-Host "Configuring database user 'mangos'..."

# Using a PowerShell Here-String (@" ... "@) to send a perfectly formatted SQL block to MariaDB.
# The password comes from $DbPassword rather than a literal, so changing the variable at the
# top of this script cannot leave step 13 authenticating with a password that was never set.
$UserQuery = @"
GRANT ALL PRIVILEGES ON tw_world.* TO 'mangos'@'localhost' IDENTIFIED BY '$DbPassword';
GRANT ALL PRIVILEGES ON tw_char.* TO 'mangos'@'localhost';
GRANT ALL PRIVILEGES ON tw_logon.* TO 'mangos'@'localhost';
GRANT ALL PRIVILEGES ON tw_logs.* TO 'mangos'@'localhost';
FLUSH PRIVILEGES;
"@

Invoke-MySqlQuery -Query $UserQuery -FailureMessage "Could not configure the 'mangos' database user"

# ==============================================================================
# PIPELINE STEP (OPTIONAL): CONDITIONAL RESTORE SECTION (IMPORT FULL DATABASE DUMPS)
# ==============================================================================
if ($SkipBotRegen -and (Test-Path $MariaDBPath)) {
    Write-PipelineHeader -StepName "(OPTIONAL): Restoring original character and logon datasets"
    Write-Host "Restoring preserved production databases from mysqldump files..."

    $TargetDbs    = @("tw_char", "tw_logon")

    foreach ($DbName in $TargetDbs) {
        $BackupFile = Join-Path $BackupFolder "${DbName}_backup.sql"

        if (Test-Path $BackupFile) {
            Write-Host " -> Re-importing complete database dump: $DbName..."

            # 1. Clean out the temporary blank pipeline database and build a fresh container.
            #    utf8mb4 to match create_databases.sql - recreating these as plain utf8 left
            #    the database default out of step with every other database in the set.
            Invoke-MySqlQuery -Query "DROP DATABASE IF EXISTS $DbName; CREATE DATABASE $DbName DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;" `
                              -FailureMessage "Could not recreate the '$DbName' database for the restore"

            # 2. Stream the backup file back into active service database
            Invoke-MySqlFile -Path $BackupFile -Database $DbName -FailureMessage "Restoring '$DbName' failed - the backup file is kept"

            # 3. Clean up the backup file from the disk to keep the environment tidy.
            #    Only reached when the import above succeeded; on failure Invoke-MySqlFile
            #    stops the pipeline and the dump stays on disk.
            Remove-Item -Path $BackupFile -Force

            Write-Host "   [OK] Database '$DbName' (including all triggers/routines) successfully restored." -ForegroundColor Green
        } else {
            Stop-Pipeline -Message "Backup file for '$DbName' is missing at $BackupFile - the database was already dropped, so stopping here rather than leaving it empty."
        }
    }

    # Remove the temporary backup folder if empty
    if (Test-Path $BackupFolder) { Remove-Item -Path $BackupFolder -Force -ErrorAction SilentlyContinue }
}

# ==============================================================================
# PIPELINE STEP 07: INJECT MISSING HONOR MAINTENANCE TABLES
# ==============================================================================
Write-PipelineHeader -StepName "07: PIPELINE HOTFIX: inject missing honor maintenance tables"
Write-Host "Applying database hotfixes for Honor Maintenance system..."
# We dynamically clone the structure of character_inventory to ensure compatibility
# and prevent mangosd.exe from crashing due to the missing copy table artifact.
$HonorHotfixQuery = @"
    USE tw_char;
    CREATE TABLE IF NOT EXISTS character_inventory_copy LIKE character_inventory;
"@

Invoke-MySqlQuery -Query $HonorHotfixQuery -FailureMessage "Could not create the character_inventory_copy hotfix table"
Write-Host "[OK] Honor maintenance hotfix table 'character_inventory_copy' successfully deployed." -ForegroundColor Green

# ==============================================================================
# PIPELINE STEP 08: CMAKE CONFIGURATION AND COMPILATION
# ==============================================================================
Write-PipelineHeader -StepName "08: Compilation and Build"
Write-Host "Initializing project build and compilation sequence..."

# Define local vcpkg absolute paths for dependencies mapping
$VcpkgInstalledPath = "$VcpkgDirectory\installed\x64-windows" # Adjust to your exact vcpkg route

# The build log this step promises the user. Everything MSBuild prints is teed into it, so
# the "check server_build.log" advice on a failure actually leads somewhere.
$BuildLogPath = Join-Path $ScriptDirectory "server_build.log"

# 1. Clean previous build configuration to ensure fresh static linking
if (Test-Path $BuildDir) {
    Write-Host "Cleaning up previous build directories..."
    Remove-Item -Path $BuildDir -Recurse -Force -ErrorAction SilentlyContinue
}

# 2. Configure project generators using CMake with playerbots and extractors flags
#
# USE_PCH_OLD=OFF is required alongside USE_PCH=OFF: the /FI fallback in
# src/game/CMakeLists.txt only runs when USE_PCH_OLD is off, and it defaults to ON for MSVC.
# BUILD_PLAYERBOTS=ON is required alongside MODULE_MOD_PLAYERBOTS=static: the module's
# sources compile either way, but mod-playerbots.cmake returns early without it and the
# module never receives its compile definitions or the botpch.h force-include.
cmake -B $BuildDir -S $SourceDir -A x64 `
    "-DCMAKE_INSTALL_PREFIX=$InstallDir" `
    "-DUSE_EXTRACTORS=ON" `
    "-DBUILD_MODULES=ON" `
    "-DBUILD_EXTENSIONS=ON" `
    "-DBUILD_MODS=ON" `
    "-DBUILD_PLAYERBOTS=ON" `
    "-DUSE_PCH=OFF" `
    "-DUSE_PCH_OLD=OFF" `
    "-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON" `
    "-DMODULE_MOD_PLAYERBOTS=static" `
    "-DMODULE_MOD_DUNGEON_CLEAR=static" `
    "-DACE_ROOT=$VcpkgInstalledPath" `
    "-DBOOST_ROOT=$VcpkgInstalledPath"
Assert-LastExitCode -Message "CMake configuration failed - the build was never started"

Write-Host "Compiling server binaries via MSBuild Release configuration..."
Write-Host " -> Compilation log written to: $BuildLogPath (Please wait, this takes a few minutes...)"

cmake --build $BuildDir --config Release 2>&1 | Tee-Object -FilePath $BuildLogPath

# $LASTEXITCODE survives the Tee-Object pipeline and still reports cmake's own result.
if ($LASTEXITCODE -ne 0) {
    Stop-Pipeline -Message "Compilation step failed. Check $BuildLogPath for detailed compiler error codes." -ExitCode $LASTEXITCODE
}
Write-Host "[OK] Binary compilation successfully completed." -ForegroundColor Green

# ==============================================================================
# PIPELINE STEP 09: INSTALLATION AND DIRECTORY RESTRUCTURING
# ==============================================================================
Write-PipelineHeader -StepName "09: Installation and directory restructuring"
Write-Host "Installing compiled modules into production environment..."

# 1. Execute default CMake install into temporary root folder
cmake --install $BuildDir --config Release
Assert-LastExitCode -Message "CMake install step failed"

# 2. Ensure all structured directories exist
New-Item -ItemType Directory -Path $BinDir -Force | Out-Null
New-Item -ItemType Directory -Path $LibDir -Force | Out-Null
New-Item -ItemType Directory -Path $EtcDir -Force | Out-Null
New-Item -ItemType Directory -Path $ToolsDir -Force | Out-Null

Write-Host "Restructuring server directory into $MangosBinDir/ and $MangosLibDir/ layouts..."

# 3. Delete all debug symbol files (.pdb) to save disk space and clean up the deployment
Get-ChildItem -Path $InstallDir -Filter "*.pdb" | Remove-Item -Force -ErrorAction SilentlyContinue

# 4. Sort and isolate executable binaries (.exe) into $MangosBinDir/ and $MangosToolsDir/
Get-ChildItem -Path $InstallDir -Filter "*.exe" | ForEach-Object {
    if ($_.Name -eq "mangosd.exe" -or $_.Name -eq "realmd.exe") {
        # Keep core server engine executables inside the primary bin folder
        Move-Item -Path $_.FullName -Destination $BinDir -Force
        Write-Host " -> Deployed Core: $($_.Name) -> $MangosBinDir/"
    } else {
        # Move map extractors and other auxiliary binaries into the tools folder
        Move-Item -Path $_.FullName -Destination $ToolsDir -Force
        Write-Host " -> Deployed Tool: $($_.Name) -> $MangosToolsDir/"
    }
}

# 5. Move configuration templates (.conf.dist) into $MangosInstalationDir/$MangosEtcDir for cleaner management
Get-ChildItem -Path $InstallDir -Filter "*.conf.dist" | Move-Item -Destination $EtcDir -Force

# 6. Generate working configuration files from templates (.dist)
if (Test-Path $EtcDir) {
    Write-Host "Generating working configuration files from templates inside $MangosInstalationDir/$MangosEtcDir..."
    Get-ChildItem -Path $EtcDir -Filter "*.conf.dist" | ForEach-Object {
        # Determine the target filename by stripping '.dist' extension
        $TargetConfigName = $_.Name -replace '\.dist$', ''
        $DestinationPath  = Join-Path $EtcDir $TargetConfigName

        # Copy template to the final configuration file
        Copy-Item -Path $_.FullName -Destination $DestinationPath -Force
        Write-Host " -> Generated: $TargetConfigName"
    }
    Write-Host "[OK] Configuration files successfully generated." -ForegroundColor Green
}

# 7. Deploy every runtime DLL into $MangosLibDir.
#
# The executables live in bin/ and Windows does not search a sibling lib/ folder on its own -
# the generated launcher scripts (step 15) are what makes this work, by prepending
# "%~dp0lib" to PATH before starting the server. Keep the two in sync: moving these DLLs
# elsewhere without updating the launchers leaves mangosd.exe unable to load ACE.dll.
Write-Host "Deploying runtime dependency libraries (.dll) into $MangosInstalationDir/$MangosLibDir..."

# Bundled OpenSSL and MySQL DLLs generated by the install step
Get-ChildItem -Path $InstallDir -Filter "*.dll" | Move-Item -Destination $LibDir -Force

# External ACE and Boost dependencies from the vcpkg toolchain
$VcpkgBinDir = Join-Path $VcpkgInstalledPath "bin"
foreach ($DependencyPattern in @("ACE.dll", "boost_*.dll")) {
    $SourcePattern = Join-Path $VcpkgBinDir $DependencyPattern
    if (-not (Get-ChildItem -Path $SourcePattern -ErrorAction SilentlyContinue)) {
        Stop-Pipeline -Message "Required runtime dependency is missing from the vcpkg toolchain: $SourcePattern"
    }
    Copy-Item -Path $SourcePattern -Destination $LibDir -Force
}

Write-Host "[OK] Run-time dependency environments fully deployed to $MangosInstalationDir/$MangosLibDir." -ForegroundColor Green
Write-Host "[OK] Files were set up." -ForegroundColor Green

# ==============================================================================
# PIPELINE STEP 10: CONFIGURATION INJECTION & TUNING
# ==============================================================================
Write-PipelineHeader -StepName "10: CONFIGURATION INJECTION & TUNING"
Write-Host "Applying automated modifications to configuration files..."

$MangosdConf = Join-Path $EtcDir "mangosd.conf"

if (Test-Path $MangosdConf) {
    Write-Host "Injecting custom database, maps, and system directory configurations..."

    # 1. Database AutoUpdate Path regex pattern and replacement
    $OldUpdatePath     = 'Database\.AutoUpdate\.Path\s*=\s*"\.\./\.\./sql/database_updates/"'
    $NewUpdatePath = "Database.AutoUpdate.Path = `"../" + $MangosTortoiseSourceDir + "/sql/database_updates/`""

    # 2. System directories regex patterns (capturing whatever is currently inside quotes)
    $OldDataDirPattern  = '^DataDir\s*=\s*".*"'
    $OldLogsDirPattern  = '^LogsDir\s*=\s*".*"'
    $OldHonorDirPattern = '^HonorDir\s*=\s*".*"'
    $OldPDumpDirPattern  = '^PDumpDir\s*=\s*".*"'
	$OldLuaDirPattern  = '^Eluna\.ScriptPath\s*=\s*".*"'

    # 3. Targeted system directories replacement values
    $NewDataDirSetting  = "DataDir = `"$MangosDataDir`""
    $NewLogsDirSetting  = "LogsDir = `"$MangosLogsDir`""
    $NewHonorDirSetting = "HonorDir = `"$MangosHonorDir`""
    $NewPDumpDirSetting  = "PDumpDir = `"$MangosPDumpDir`""
    $NewLuaDirSetting   = "Eluna.ScriptPath = `"$MangosLuaDir`""

    # 4. Read content, execute all 6 replacements sequentially, and stream back to file
    (Get-Content $MangosdConf) `
        -replace $OldUpdatePath, $NewUpdatePath `
        -replace $OldDataDirPattern, $NewDataDirSetting `
        -replace $OldLogsDirPattern, $NewLogsDirSetting `
        -replace $OldHonorDirPattern, $NewHonorDirSetting `
        -replace $OldPDumpDirPattern, $NewPDumpDirSetting `
		-replace $OldLuaDirPattern, $NewLuaDirSetting `
        | Set-Content $MangosdConf

    Write-Host "[OK] mangosd.conf successfully updated with all production layout directories." -ForegroundColor Green
} else {
    Stop-Pipeline -Message "Configuration injection failed: Could not locate mangosd.conf inside $MangosEtcDir"
}

# ==============================================================================
# PIPELINE STEP 11: PLAYERBOTS MODULE DATA IMPORT
# ==============================================================================
Write-PipelineHeader -StepName "11: Importing PlayerBots module SQL data..."
Write-Host "Importing PlayerBots module SQL data..."

$PlayerBotSqlDir = Join-Path $ScriptDirectory "$MangosTortoiseSourceDir\modules\mod-playerbots\sql"
$WorldSqlPath    = Join-Path $PlayerBotSqlDir "world"
$ClassicSqlPath  = Join-Path $PlayerBotSqlDir "world\classic"
$CharSqlPath     = Join-Path $PlayerBotSqlDir "characters"

# Import PlayerBot world modifications if directories exist
if (Test-Path $WorldSqlPath) {
    Get-ChildItem (Join-Path $WorldSqlPath "*.sql"), (Join-Path $ClassicSqlPath "*.sql") -ErrorAction SilentlyContinue | ForEach-Object {
        Write-Host "Importing PlayerBot World: $($_.Name)"
        Invoke-MySqlFile -Path $_.FullName -Database "tw_world" -FailureMessage "Importing a PlayerBot world SQL file failed"
    }
} else {
    Write-Warning "PlayerBot world SQL directory not found at: $WorldSqlPath"
}

# Import PlayerBot character modifications if directory exists AND regeneration is not skipped
if ($SkipBotRegen) {
    Write-Host " -> [SKIP] Skipping PlayerBot Characters database import due to -SkipBotRegen parameter." -ForegroundColor Yellow
} elseif (Test-Path $CharSqlPath) {
    Get-ChildItem (Join-Path $CharSqlPath "*.sql") | ForEach-Object {
        Write-Host "Importing PlayerBot Characters: $($_.Name)"
        Invoke-MySqlFile -Path $_.FullName -Database "tw_char" -FailureMessage "Importing a PlayerBot characters SQL file failed"
    }
} else {
    Write-Warning "PlayerBot characters SQL directory not found at: $CharSqlPath"
}

# ==============================================================================
# PIPELINE STEP 12: PLAYERBOT CONFIGURATION TUNING
# ==============================================================================
$AiPlayerbotConf = Join-Path $EtcDir "aiplayerbot.conf"
Write-PipelineHeader -StepName "12: PLAYERBOT CONFIGURATION TUNING"
if (Test-Path $AiPlayerbotConf) {
	Write-Host "Injecting optimized testlab scaling parameters into aiplayerbot..."

    # 1. Define regex patterns to capture the default/existing configuration values.
    #    There is no 'AiPlayerbot.RandomBotAccount' key in aiplayerbot.conf.dist - the real
    #    ones are RandomBotAccountPrefix and RandomBotAccountCount - so the replacement that
    #    used to target it (with an undefined $NewBotAccSetting) was removed rather than
    #    given a value: the prefix is left at whatever the shipped template defines.
    $OldMinBotsPattern = '^AiPlayerbot\.MinRandomBots\s*=\s*.*'
    $OldMaxBotsPattern = '^AiPlayerbot\.MaxRandomBots\s*=\s*.*'
	$OldRandomBotMinLevelPattern = '^AiPlayerbot\.RandomBotMinLevel\s*=\s*.*'
	$OldRandomBotMaxLevelPattern = '^AiPlayerbot\.RandomBotMaxLevel\s*=\s*.*'
	$OldRandomBotAccountCountPattern  = '^AiPlayerbot\.RandomBotAccountCount\s*=\s*.*'

    # 2. Define the new optimized target settings for fast testbed scaling
    $NewMinBotsSetting = "AiPlayerbot.MinRandomBots = $MinRandomBots"
    $NewMaxBotsSetting = "AiPlayerbot.MaxRandomBots = $MaxRandomBots"
	$NewRandomBotMinLevelSetting = "AiPlayerbot.RandomBotMinLevel = $RandomBotMinLevel"
	$NewRandomBotMaxLevelSetting = "AiPlayerbot.RandomBotMaxLevel = $RandomBotMaxLevel"
	$NewRandomBotAccountCountSetting  = "AiPlayerbot.RandomBotAccountCount = $RandomBotAccountsCount"

    # 3. Read content, execute chained text replacements, and save back to file
    (Get-Content $AiPlayerbotConf) `
        -replace $OldMinBotsPattern, $NewMinBotsSetting `
        -replace $OldMaxBotsPattern, $NewMaxBotsSetting `
		-replace $OldRandomBotMinLevelPattern,  $NewRandomBotMinLevelSetting  `
		-replace $OldRandomBotMaxLevelPattern,  $NewRandomBotMaxLevelSetting  `
		-replace $OldRandomBotAccountCountPattern,  $NewRandomBotAccountCountSetting  `
        | Set-Content $AiPlayerbotConf

    Write-Host "[OK] aiplayerbot.conf successfully downscaled using global variables." -ForegroundColor Green
} else {
    Stop-Pipeline -Message "Configuration injection failed: Could not locate aiplayerbot.conf inside $EtcDir"
}


# ==============================================================================
# PIPELINE STEP 13: REALMLIST AND CONFIGURATION SETUP
# ==============================================================================
Write-PipelineHeader -StepName "13: Configuring local realmlist DB options..."
Write-Host "Configuring local realmlist options..."

# Update realm configuration to point to local testbed environment on port $RealmlistPort
$RealmlistQuery = @"
    INSERT INTO realmlist (id, name, address, port, icon, realmflags, timezone, allowedSecurityLevel)
    VALUES (1, 'TurtleWoW Local', '$RealmlistIPAddress', $RealmlistPort, 0, 0, 1, 0)
    ON DUPLICATE KEY UPDATE address='$RealmlistIPAddress', port=$RealmlistPort;
"@

Invoke-MySqlQuery -Query $RealmlistQuery `
                  -Database "tw_logon" `
                  -DefaultsFile $script:DbDefaultsFile `
                  -FailureMessage "Could not register the local realm in tw_logon.realmlist"

Write-Host "[OK] Realmlist points at ${RealmlistIPAddress}:$RealmlistPort." -ForegroundColor Green

# ==============================================================================
# PIPELINE STEP 14: RUNTIME APPLICATION DIRECTORY FACTORY
# ==============================================================================
# This step MUST run after 'cmake --install' and config edits to prevent CMake
# from wiping the newly created directories during its clean deployment phase.
Write-PipelineHeader -StepName "14: RUNTIME APPLICATION DIRECTORY FACTORY"
Write-Host "Creating missing runtime directories in the server root..."

# Explicitly map the production paths matching your updated mangosd.conf parameters
$RuntimeLogsDir  = Join-Path $InstallDir $MangosLogsDir
$RuntimeHonorDir = Join-Path $InstallDir $MangosHonorDir
$RuntimeDumpDir  = Join-Path $InstallDir $MangosPDumpDir
$RuntimeLuaDir   = Join-Path $InstallDir $MangosLuaDir

$RequiredRuntimeFolders = @($RuntimeLogsDir, $RuntimeHonorDir, $RuntimeDumpDir, $RuntimeLuaDir)

foreach ($Folder in $RequiredRuntimeFolders) {
    if (-not (Test-Path $Folder)) {
        # Generate the folders and suppress empty terminal return objects via Out-Null
        New-Item -ItemType Directory -Path $Folder -Force | Out-Null
        Write-Host " -> Created runtime directory: $Folder"
    } else {
        Write-Host " -> Directory already exists: $Folder"
    }
}

# ==============================================================================
# PIPELINE STEP 15: SERVER LAUNCHER SCRIPTS
# ==============================================================================
# The three .bat files an operator actually double-clicks. They are generated only when
# missing, so a fresh clone gets a working set and an existing, hand-tuned one is preserved.
#
# The realm/world launchers put "%~dp0lib" on PATH first: the executables are in bin/ while
# their runtime DLLs are in lib/ (step 09), and Windows would not find them otherwise.
Write-PipelineHeader -StepName "15: SERVER LAUNCHER SCRIPTS"
Write-Host "Verifying server launcher scripts..."

$MysqlLauncherContent = @"
cd $MariaDbFolderName\bin
start "mysql" mysqld.exe --console
"@

$RealmLauncherContent = @"
@echo off
:: Prepend the local 'lib' folder to PATH for this process only, so realmd.exe
:: finds the runtime DLLs the pipeline deploys there.
set PATH=%~dp0lib;%PATH%

:: Run the realm (login) server against its configuration file
bin\realmd.exe -c etc\realmd.conf

pause
"@

$WorldLauncherContent = @"
@echo off
:: Prepend the local 'lib' folder to PATH for this process only, so mangosd.exe
:: finds the runtime DLLs the pipeline deploys there.
set PATH=%~dp0lib;%PATH%

:: Run the world server against its configuration file
bin\mangosd.exe -c etc\mangosd.conf

pause
"@

New-ServerLauncherScript -Path (Join-Path $InstallDir "1.Start mysql.bat")  -Content $MysqlLauncherContent
New-ServerLauncherScript -Path (Join-Path $InstallDir "2.Realm server.bat") -Content $RealmLauncherContent
New-ServerLauncherScript -Path (Join-Path $InstallDir "3.World server.bat") -Content $WorldLauncherContent

Write-Host "[OK] Server launcher scripts are in place." -ForegroundColor Green

# Release the run lock, drop the temporary credential files and close the transcript on the
# success path too.
Remove-PipelineLock
Remove-PipelineCredentialFiles
Write-Host "[OK] Pipeline execution fully completed! Server environment is ready." -ForegroundColor Green
try { Stop-Transcript | Out-Null } catch { }
