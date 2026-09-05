<#
.SYNOPSIS
    Automated testlab pipeline for Tortoise-WoW and Playerbots compilation and deployment.
.DESCRIPTION
    This script automates the full deployment workflow, including client data verification,
    vcpkg dependency check, Git synchronization with submodules, CMake/MSBuild compilation,
    directory restructuring, configuration tuning, and automated database generation.

    Every parameter is optional and every default is the one this testlab uses, so a plain
    run needs no arguments at all. PowerShell renders the SYNTAX block above as one long
    line whatever the source looks like; the same parameters, grouped by what they do:

      Run mode
        -SkipBotRegen             keep accounts, characters and playerbot data
        -applyPatches             "hash1;hash2" to cherry-pick before building

      Where things live
        -WorkspaceRoot            testlab root            (default: this script's folder)
        -VcpkgDirectory           vcpkg                   (default: discovered)
        -VcpkgTriplet             vcpkg triplet           (default: x64-windows)

      What to build
        -RepoUrl                  repository              (default: Shyalya/tortoise-wow)
        -BranchName               branch                  (default: playerbots-integration-gh)
        -PatchRemoteUrl           remote for -applyPatches

      Which database server
        -DbFlavor                 Auto | MariaDB | MySQL  (default: Auto)
        -MariaDbFolderName        portable server folder inside server\
        -MariaDbClientPath        explicit mariadb.exe / mysql.exe
        -DbHost  -DbPort          connection target       (default: the client's own)
        -DbStartupTimeoutSeconds  wait for it to start    (default: 30)

      Database identity
        -RootPassword             root password           (default: mangos)
        -DbUser  -DbPassword      the server's account    (default: mangos / mangos)
        -DbPrefix                 names all four          (default: tw_)
        -WorldDatabaseName  -CharacterDatabaseName
        -LoginDatabaseName  -LogsDatabaseName
                                  override one name       (default: from the prefix)

      Realm and bots
        -RealmlistIPAddress  -RealmlistPort               (default: 127.0.0.1 / 8090)
        -MinRandomBots  -MaxRandomBots                    (default: 5 / 10)
        -RandomBotMinLevel  -RandomBotMaxLevel            (default: 1 / 20)
        -RandomBotAccountsCount                           (default: 10)

    Use "Get-Help .\Setup-Testlab.ps1 -Parameter <name>" for the detail on any one of them,
    or -Examples for the common combinations.
.PARAMETER SkipBotRegen
    Preserves existing accounts, GM characters and playerbot data. 'tw_char' and 'tw_logon'
    are dumped before the run and restored at the end; the dump is verified before anything
    destructive happens, so a failed backup stops the pipeline with the data still intact.
    Without it every database is dropped and rebuilt from scratch.
.PARAMETER applyPatches
    Semicolon-separated git commit hashes to cherry-pick onto the branch before building,
    fetched from -PatchRemoteUrl. Example: "0ee0748;abc1234". Uncommitted local changes are
    stashed first, never discarded.
.PARAMETER WorkspaceRoot
    The testlab root: the folder holding 'server\' and the 'tortoise-wow\' checkout.
    Defaults to the folder this script sits in. Relative paths are resolved against your
    current directory. Everything else the pipeline touches is derived from this one path.
.PARAMETER VcpkgDirectory
    vcpkg installation providing ACE and Boost. Left empty it is discovered: VCPKG_ROOT,
    then vcpkg.exe on PATH, then conventional locations. Given explicitly it is used or the
    run fails - never silently replaced by a discovered one.
.PARAMETER VcpkgTriplet
    vcpkg triplet the dependencies are installed for. Defaults to x64-windows; the build
    itself is always -A x64.
.PARAMETER RootPassword
    Password for the database 'root' account, used for schema creation and imports.
.PARAMETER DbPassword
    Password for the 'mangos' service account this script creates and the server logs in
    with. Written to the database, not to any configuration file.
.PARAMETER DbUser
    Account the server logs in with. Created and granted in step 06, and written into the
    connection strings in mangosd.conf and realmd.conf.
.PARAMETER DbPrefix
    Prefix for the four database names, default "tw_". Change it to run several testlabs
    against one database server without them overwriting each other - -DbPrefix "lab2_"
    gives lab2_world, lab2_char, lab2_logon and lab2_logs. The schema is renamed on import
    and the server's connection strings are written to match.
.PARAMETER WorldDatabaseName
    Overrides the world database name. Empty (default) means "<prefix>world".
.PARAMETER CharacterDatabaseName
    Overrides the characters database name. Empty (default) means "<prefix>char".
.PARAMETER LoginDatabaseName
    Overrides the login/realm database name. Empty (default) means "<prefix>logon".
.PARAMETER LogsDatabaseName
    Overrides the logs database name. Empty (default) means "<prefix>logs".
.PARAMETER DbFlavor
    Which engine to look for: Auto (default), MariaDB or MySQL. Only narrows discovery -
    useful on a machine that has both installed.
.PARAMETER MariaDbFolderName
    Name of the portable MariaDB directory inside 'server\', tried before PATH and the
    conventional install locations.
.PARAMETER MariaDbClientPath
    Explicit path to mariadb.exe or mysql.exe. Given, it is used or the run fails, because
    connecting to a different server than intended means dropping databases on the wrong
    instance.
.PARAMETER DbHost
    Host to connect to. Empty (default) means the client's own default, which is what the
    bundled portable server wants.
.PARAMETER DbPort
    Port to connect to. 0 (default) means the client's own default.
.PARAMETER DbStartupTimeoutSeconds
    How long the preflight waits for the server to start answering, in seconds. Default 30.
    'server\1.Start mysql.bat' launches mysqld asynchronously, so a run started right after
    it needs a moment. A wrong password is never retried.
.PARAMETER RepoUrl
    Source repository to clone or pull.
.PARAMETER BranchName
    Branch to build. Point it at a topic branch to test one without editing anything.
.PARAMETER PatchRemoteUrl
    Remote the -applyPatches commits are fetched from.
.PARAMETER RealmlistIPAddress
    Address written into tw_logon.realmlist, and the one your client's realmlist.wtf has to
    point at.
.PARAMETER RealmlistPort
    Port written into tw_logon.realmlist. Must match WorldServerPort in mangosd.conf, which
    ships as 8090; a mismatch lets login succeed and then hangs the client before character
    selection.
.PARAMETER MinRandomBots
    Lower bound of the playerbot population written into aiplayerbot.conf.
.PARAMETER MaxRandomBots
    Upper bound of the playerbot population. The shipped template asks for a thousand, which
    turns the first start into a long wait for no benefit.
.PARAMETER RandomBotMinLevel
    Lowest level random bots are generated at.
.PARAMETER RandomBotMaxLevel
    Highest level random bots are generated at.
.PARAMETER RandomBotAccountsCount
    Number of bot accounts to create.
.EXAMPLE
    .\Run-Testlab.bat
    The normal run: builds everything and rebuilds every database from scratch. Use the .bat
    rather than the .ps1 so no execution-policy change is needed.
.EXAMPLE
    .\Run-Testlab.bat -SkipBotRegen
    Rebuilds the server but keeps your accounts, GM characters and playerbot data.
.EXAMPLE
    .\Run-Testlab.bat -RepoUrl https://github.com/me/tortoise-wow.git -BranchName my-fix
    Builds a fork or topic branch without editing the script.
.EXAMPLE
    .\Run-Testlab.bat -WorkspaceRoot C:\WOW\testlab -VcpkgDirectory D:\vcpkg
    Runs the script straight out of the repository against a testlab folder elsewhere.
.EXAMPLE
    .\Run-Testlab.bat -DbFlavor MySQL -DbPort 3307 -RootPassword "hunter2"
    Uses an installed MySQL on a non-default port instead of the bundled portable MariaDB.
.EXAMPLE
    .\Run-Testlab.bat -applyPatches "0ee0748;abc1234" -SkipBotRegen
    Cherry-picks two hotfixes onto the branch, then rebuilds while preserving character data.
.EXAMPLE
    .\Run-Testlab.bat -WorkspaceRoot C:\WOW\lab2 -DbPrefix "lab2_" -RealmlistPort 8091
    A second, independent testlab on the same machine and the same database server.
.NOTES
    Windows only (PowerShell 5.1+, Visual Studio 2022, CMake). See README.md next to this
    script for the folder layout it expects and what you have to supply yourself.

    Everything printed is also written to 'pipeline_console.log' in the workspace root, and
    the compiler output additionally to 'server_build.log'. No shell redirection needed.
.LINK
    https://github.com/Shyalya/tortoise-wow
.LINK
    https://github.com/Shyalya/tortoise-wow/blob/playerbots-integration-gh/INSTALL-WINDOWS.md
.LINK
    https://github.com/Shyalya/tortoise-wow/blob/playerbots-integration-gh/INSTALL-LINUX.md
#>
[CmdletBinding(PositionalBinding = $false)]
param (
    # PositionalBinding=$false above: with this many parameters, positional binding is a
    # liability rather than a convenience. It also shortens every entry in the generated
    # SYNTAX block from "[[-Name] <String>]" to "[-Name <String>]" - and without it a stray
    # unnamed argument would silently bind to whichever parameter sits in that position,
    # which here is -applyPatches, the one that triggers a cherry-pick.
    #
    # (This comment lives inside param() deliberately: line comments left between the help
    # block and the parameters are absorbed into the last help section - they surfaced
    # under RELATED LINKS.)

    # ---- run mode ----------------------------------------------------------------------

    # Switch to bypass character database drop, playerbot data import, and configuration wipe
    [switch]$SkipBotRegen,

	# Dynamic string sequence containing commit hashes separated by semicolons (e.g. "0ee0748;abc1234")
    [string]$applyPatches,

    # ---- where things live -------------------------------------------------------------

    # Testlab root: the folder holding 'server\' and the 'tortoise-wow\' checkout. Defaults to
    # the directory this script sits in, which is the layout you get by copying the script out
    # of the repository into an empty working folder. Point it elsewhere to run the script
    # straight out of a checkout: -WorkspaceRoot C:\WOW\testlab
    # Deliberately defaulted in the body rather than here. $PSScriptRoot is empty while an
    # advanced script's parameter defaults are being evaluated - [CmdletBinding()] above is
    # what makes this script advanced - even though it holds the right path everywhere
    # else. Written as "= $PSScriptRoot" this silently became "", and the run died in
    # Start-Transcript before printing anything useful.
    [string]$WorkspaceRoot = "",

    # vcpkg installation providing ACE and Boost. Left empty it is discovered: VCPKG_ROOT,
    # then vcpkg.exe on PATH, then a few conventional locations. Give it explicitly only
    # when you have several and want a particular one.
    [string]$VcpkgDirectory = "",

    # vcpkg triplet the dependencies are installed for. The build itself is -A x64.
    [string]$VcpkgTriplet = "x64-windows",

    # ---- what to build -----------------------------------------------------------------

    # Source to build. Point these at a fork or a topic branch to test one without
    # touching the script: -RepoUrl https://github.com/me/tortoise-wow.git -BranchName my-fix
    [string]$RepoUrl    = "https://github.com/Shyalya/tortoise-wow.git",
    [string]$BranchName = "playerbots-integration-gh",

    # Remote the -applyPatches commits are fetched from
    [string]$PatchRemoteUrl = "https://github.com/Penqle/tortoise-wow.git",

    # ---- which database server ---------------------------------------------------------

    # Which database engine to look for. Auto takes the first client it finds in the search
    # order; MariaDB or MySQL restricts discovery to that engine's client names, for a
    # machine that has both installed.
    [ValidateSet("Auto", "MariaDB", "MySQL")]
    [string]$DbFlavor = "Auto",

    # Name of the portable MariaDB directory inside server\. Used first when present; if it
    # is not there the client is looked for on PATH and in the usual install locations.
    [string]$MariaDbFolderName = "mariadb-10.3.39-winx64",

    # Explicit path to the client (mariadb.exe or mysql.exe). Given, it is used or the run
    # fails - never silently replaced by a discovered one, because connecting to a different
    # server than intended means dropping databases on the wrong instance.
    [string]$MariaDbClientPath = "",

    # Connection target. Both empty/0 means "whatever the client defaults to", which is what
    # the bundled portable server wants; set them for a non-default port or a remote host.
    [string]$DbHost = "",
    [int]$DbPort    = 0,

    # How long the preflight waits for the server to start answering. 1.Start mysql.bat
    # launches mysqld asynchronously, so a run kicked off right after it needs a moment.
    [int]$DbStartupTimeoutSeconds = 30,

    # ---- database identity -------------------------------------------------------------

    # Credentials. Defaults match the portable MariaDB this testlab ships with; override
    # them rather than editing the script body.
    [string]$RootPassword = "mangos",
    [string]$DbPassword   = "mangos",

    # Account the server logs in with. Created and granted by step 06.
    [string]$DbUser = "mangos",

    # Prefix for the four database names. Change it to run several testlabs against one
    # server without them overwriting each other: -DbPrefix "lab2_" gives lab2_world,
    # lab2_char, lab2_logon and lab2_logs.
    [string]$DbPrefix = "tw_",

    # Individual database names. Empty means "<prefix>world" and so on; set one only to
    # break out of the prefix scheme for a single database.
    [string]$WorldDatabaseName     = "",
    [string]$CharacterDatabaseName = "",
    [string]$LoginDatabaseName     = "",
    [string]$LogsDatabaseName      = "",

    # ---- realm and bots ----------------------------------------------------------------

    # Realm registered in the login database's realmlist. The port has to match
    # WorldServerPort in mangosd.conf, which ships as 8090.
    [string]$RealmlistIPAddress = "127.0.0.1",
    [int]$RealmlistPort = 8090,

    # Playerbot population for the testlab. The shipped template asks for a thousand bots,
    # which turns the first start into a long wait for no benefit.
    [int]$MinRandomBots          = 5,
    [int]$MaxRandomBots          = 10,
    [int]$RandomBotMinLevel      = 1,
    [int]$RandomBotMaxLevel      = 20,
    [int]$RandomBotAccountsCount = 10
)

# StrictMode turns a typo'd or never-assigned variable into a hard error instead of an
# empty string. Without it, '$NewDumpPDirSetting' (a typo for $NewPDumpDirSetting) and an
# undefined $ElunaScriptPath silently rewrote mangosd.conf with a blank PDumpDir and an
# empty Eluna.ScriptPath - the pipeline reported success and the server lost both settings.
Set-StrictMode -Version Latest

[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

# The -WorkspaceRoot default, applied here because it cannot be applied in param(): see the
# comment on that parameter. $PSScriptRoot is correct from this point on.
if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) { $WorkspaceRoot = $PSScriptRoot }

# Credential files are created in step 00 and removed by Stop-Pipeline / the final cleanup.
# Declared up front so the cleanup helper can always test them under StrictMode.
$script:RootDefaultsFile = $null
$script:DbDefaultsFile   = $null

# Run lock. $LockOwned stays false until THIS run creates the file, so aborting because
# somebody else's run holds the lock can never delete their lock on the way out.
$script:LockFile  = $null
$script:LockOwned = $false

# The singleton handle itself. A named mutex is the authoritative guard - see
# Assert-SingleInstance - and the lock file beside it only carries the human-readable
# "who and since when".
$script:SingletonMutex = $null

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

# Releases the singleton mutex. Safe to call when we never acquired one.
function Remove-PipelineSingleton {
    if ($script:SingletonMutex) {
        try { [void]$script:SingletonMutex.ReleaseMutex() } catch { }
        $script:SingletonMutex.Dispose()
        $script:SingletonMutex = $null
    }
}

# Enforces one pipeline run per machine.
#
# The lock file below records who is running and since when, but it cannot be the guard on
# its own: a run killed with Ctrl+C or Task Manager leaves the file behind, and deciding
# whether it is stale means guessing from a PID that Windows may already have recycled.
# A named mutex has no such problem - the kernel drops it the instant the owning process
# ends, however it ends.
#
# "Global\" is the machine-wide namespace, so a run started from another session (a second
# console, RDP, a scheduled task) is seen as well. Creating an object there needs
# SeCreateGlobalPrivilege, which an ordinary non-elevated user does not have, so a failure
# falls back to the per-session "Local\" namespace rather than aborting: the lock file
# still covers the cross-session case, just with the weaker stale-PID heuristic.
function Assert-SingleInstance {
    $CreatedNew = $false

    foreach ($MutexName in @("Global\TortoiseWoW-Testlab-Pipeline", "Local\TortoiseWoW-Testlab-Pipeline")) {
        try {
            $script:SingletonMutex = New-Object System.Threading.Mutex($true, $MutexName, [ref]$CreatedNew)
            break
        } catch {
            # Access denied on Global\ (no privilege), or the name already exists with an
            # ACL we may not open. Try the next namespace.
            $script:SingletonMutex = $null
        }
    }

    if (-not $script:SingletonMutex) {
        Stop-Pipeline -Message "Could not create the singleton mutex - refusing to run rather than risk a second concurrent pipeline." -ExitCode 2
    }

    if (-not $CreatedNew) {
        # Somebody else owns it. Drop our handle without releasing a mutex we never owned.
        $script:SingletonMutex.Dispose()
        $script:SingletonMutex = $null
        Stop-Pipeline -Message ("Another pipeline run is already in progress on this machine. " +
                                "Only one run may execute at a time - it drops databases and wipes the server directory. " +
                                "Wait for it to finish, then start again.") -ExitCode 2
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
    Remove-PipelineSingleton
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

# Runs a native command so that its output reaches the transcript as well as the console.
#
# Start-Transcript only records what travels through PowerShell's own streams. A native
# command left to write straight to the console - "git pull", "cmake -B ..." - and anything
# launched through Start-Process bypass it completely, which is why pipeline_console.log
# used to contain the pipeline's own messages and almost nothing from the tools it drives.
# Verified: of Write-Host, a direct native call, a Start-Process child and a piped call,
# only the first and last were captured.
#
# Routing the output through ForEach-Object puts it back in the pipeline, so the transcript
# sees it and the operator still watches it live. 2>&1 folds stderr in - PowerShell 5.1
# wraps those lines in ErrorRecords, hence the explicit ToString().
function Invoke-NativeLogged {
    param (
        [Parameter(Mandatory = $true)][string]$Executable,
        [string[]]$Arguments = @()
    )

    & $Executable @Arguments 2>&1 | ForEach-Object { Write-Host $_.ToString() }
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

    # host/port are written only when asked for. Emitting host=localhost unconditionally is
    # not a no-op on Windows - it can move the client between TCP and a named pipe - and the
    # bundled portable server is happiest with the client's own defaults.
    $Content = "[client]`r`nuser=$User`r`npassword=$Password`r`n"
    if (-not [string]::IsNullOrWhiteSpace($DbHost)) { $Content += "host=$DbHost`r`n" }
    if ($DbPort -gt 0)                              { $Content += "port=$DbPort`r`n" }
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

    Invoke-NativeLogged -Executable $MariaDBPath -Arguments $Arguments
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
        [switch]$AllowFailure,

        # Rewrites backticked database identifiers on the way through - used for
        # create_databases.sql, whose CREATE DATABASE and USE statements name the stock
        # tw_* databases directly.
        [System.Collections.IDictionary]$RenameDatabases
    )

    if (-not $DefaultsFile) { $DefaultsFile = $script:RootDefaultsFile }

    $FilteredPath = Join-Path $env:TEMP ("tw_import_{0}.sql" -f [guid]::NewGuid().ToString('N'))
    $Writer = New-Object System.IO.StreamWriter($FilteredPath, $false, (New-Object System.Text.UTF8Encoding($false)))
    try {
        foreach ($Line in [System.IO.File]::ReadLines($Path)) {
            if ($Line -match 'sandbox mode') { continue }

            if ($RenameDatabases) {
                # Only the backticked form is touched. Every CREATE DATABASE and USE in
                # create_databases.sql is backticked; the one bare occurrence is a comment
                # header, which is left alone rather than risking a substring match inside
                # table data.
                foreach ($StockName in $RenameDatabases.Keys) {
                    $NewName = $RenameDatabases[$StockName]
                    if ($NewName -ne $StockName) {
                        $Line = $Line.Replace("``$StockName``", "``$NewName``")
                    }
                }
            }

            $Writer.WriteLine($Line)
        }
    } finally {
        $Writer.Dispose()
    }

    $Arguments = @("--defaults-extra-file=$DefaultsFile", "--default-character-set=utf8mb4")
    if ($Database) { $Arguments += $Database }

    # Redirecting stdin is the one thing the call operator cannot do, so this stays on
    # Start-Process - but its output would then bypass the transcript entirely, so stdout
    # and stderr are captured to files and replayed through Write-Host. That matters: a
    # failed import's only explanation is the message the client printed.
    $OutFile = Join-Path $env:TEMP ("tw_import_out_{0}.txt" -f [guid]::NewGuid().ToString('N'))
    $ErrFile = Join-Path $env:TEMP ("tw_import_err_{0}.txt" -f [guid]::NewGuid().ToString('N'))

    try {
        $ImportProcess = Start-Process -FilePath $MariaDBPath `
                                       -ArgumentList $Arguments `
                                       -RedirectStandardInput $FilteredPath `
                                       -RedirectStandardOutput $OutFile `
                                       -RedirectStandardError $ErrFile `
                                       -NoNewWindow `
                                       -PassThru `
                                       -Wait

        foreach ($CapturedFile in @($OutFile, $ErrFile)) {
            if (Test-Path -LiteralPath $CapturedFile) {
                Get-Content -Path $CapturedFile -ErrorAction SilentlyContinue |
                    Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
                    ForEach-Object { Write-Host $_ }
            }
        }

        if ($ImportProcess.ExitCode -ne 0 -and -not $AllowFailure) {
            Stop-Pipeline -Message "$FailureMessage : $Path (exit code $($ImportProcess.ExitCode))" `
                          -ExitCode $ImportProcess.ExitCode
        }
    } finally {
        Remove-Item -Path $FilteredPath, $OutFile, $ErrFile -Force -ErrorAction SilentlyContinue
    }
}

# Finds the MariaDB/MySQL command line client, and the matching dump tool.
#
# The testlab's own portable server comes first: it is deliberately self-contained, and a
# run that silently used a system-wide instance instead would drop tw_world / tw_char /
# tw_logon / tw_logs on THAT server. Only when the portable copy is absent does this fall
# back to PATH and the conventional install locations.
#
# Both naming generations are handled. MariaDB renamed its client to mariadb.exe in 10.6
# and newer builds may ship no mysql.exe at all, while the 10.3 portable build in this
# testlab has only mysql.exe. sql/setup_databases.bat in this repository prefers 'mariadb'
# and falls back to 'mysql'; the same order is used here. The dump tool is paired from the
# same directory, mariadb-dump.exe or mysqldump.exe.
function Resolve-MariaDbClient {
    param (
        [string]$Explicit,
        [string]$PortableBinDir
    )

    # -DbFlavor narrows the names when a machine has both engines installed. MariaDB uses
    # mariadb.exe from 10.6 on (and older builds, like the 10.3 portable one this testlab
    # ships with, only have mysql.exe), so MariaDB has to accept both spellings; MySQL only
    # ever ships mysql.exe.
    switch ($DbFlavor) {
        "MariaDB" { $ClientNames = @("mariadb.exe", "mysql.exe"); $DumpNames = @("mariadb-dump.exe", "mysqldump.exe") }
        "MySQL"   { $ClientNames = @("mysql.exe");                $DumpNames = @("mysqldump.exe") }
        default   { $ClientNames = @("mariadb.exe", "mysql.exe"); $DumpNames = @("mariadb-dump.exe", "mysqldump.exe") }
    }

    # Given a directory, return a resolved pair or $null.
    function Resolve-FromDirectory {
        param([string]$Directory, [string]$Source)

        if ([string]::IsNullOrWhiteSpace($Directory) -or -not (Test-Path -LiteralPath $Directory)) { return $null }

        foreach ($ClientName in $ClientNames) {
            $ClientCandidate = Join-Path $Directory $ClientName
            if (-not (Test-Path -LiteralPath $ClientCandidate)) { continue }

            $DumpCandidate = $null
            foreach ($DumpName in $DumpNames) {
                $Probe = Join-Path $Directory $DumpName
                if (Test-Path -LiteralPath $Probe) { $DumpCandidate = $Probe; break }
            }

            return @{
                Client = [System.IO.Path]::GetFullPath($ClientCandidate)
                Dump   = $DumpCandidate
                Source = $Source
            }
        }

        return $null
    }

    # 1. An explicit answer is honoured or the run stops - see the parameter comment.
    if (-not [string]::IsNullOrWhiteSpace($Explicit)) {
        if (-not (Test-Path -LiteralPath $Explicit)) {
            Stop-Pipeline -Message "No database client at the -MariaDbClientPath given: $Explicit"
        }

        $ExplicitDir = Split-Path $Explicit -Parent
        $Dump = $null
        foreach ($DumpName in $DumpNames) {
            $Probe = Join-Path $ExplicitDir $DumpName
            if (Test-Path -LiteralPath $Probe) { $Dump = $Probe; break }
        }

        return @{
            Client = [System.IO.Path]::GetFullPath($Explicit)
            Dump   = $Dump
            Source = "-MariaDbClientPath"
        }
    }

    # 2. The testlab's own portable server.
    $Portable = Resolve-FromDirectory -Directory $PortableBinDir -Source "portable server in $MangosInstalationDir\$MariaDbFolderName"
    if ($Portable) { return $Portable }

    # 3. Whatever is on PATH, in the repository's own order of preference.
    foreach ($ClientName in $ClientNames) {
        $OnPath = Get-Command $ClientName -ErrorAction SilentlyContinue
        if ($OnPath) {
            $Resolved = Resolve-FromDirectory -Directory (Split-Path $OnPath.Source -Parent) -Source "PATH"
            if ($Resolved) { return $Resolved }
        }
    }

    # 4. Conventional install locations, built from environment variables rather than a
    #    literal drive letter. Newest first, so a machine with several picks the current one.
    foreach ($ProgramDir in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        if ([string]::IsNullOrWhiteSpace($ProgramDir)) { continue }

        $InstallationPattern = switch ($DbFlavor) {
            "MariaDB" { '^MariaDB' }
            "MySQL"   { '^MySQL' }
            default   { '^(MariaDB|MySQL)' }
        }

        $Installations = Get-ChildItem -Path $ProgramDir -Directory -ErrorAction SilentlyContinue |
                         Where-Object { $_.Name -match $InstallationPattern } |
                         Sort-Object Name -Descending

        foreach ($Installation in $Installations) {
            # MySQL nests one level deeper: "MySQL\MySQL Server 8.0\bin".
            $BinDirectories = @((Join-Path $Installation.FullName "bin"))
            $BinDirectories += (Get-ChildItem -Path $Installation.FullName -Directory -ErrorAction SilentlyContinue |
                                ForEach-Object { Join-Path $_.FullName "bin" })

            foreach ($BinDirectory in $BinDirectories) {
                $Resolved = Resolve-FromDirectory -Directory $BinDirectory -Source $Installation.Name
                if ($Resolved) { return $Resolved }
            }
        }
    }

    Stop-Pipeline -Message ("No MariaDB/MySQL client found. Put a portable MariaDB in " +
                            "$MangosInstalationDir\$MariaDbFolderName\, or install one and put its bin\ on PATH, " +
                            "or pass -MariaDbClientPath <path to mariadb.exe or mysql.exe>.")
}

# Runs a trivial query and reports back rather than aborting, so the caller can tell the
# failure modes apart. stdout and stderr go to files because PowerShell 5.1 wraps a native
# command's redirected stderr in ErrorRecords, which makes the text awkward to match on.
function Test-MariaDbConnection {
    param (
        [string]$DefaultsFile
    )

    $OutFile = Join-Path $env:TEMP ("tw_dbprobe_out_{0}.txt" -f [guid]::NewGuid().ToString('N'))
    $ErrFile = Join-Path $env:TEMP ("tw_dbprobe_err_{0}.txt" -f [guid]::NewGuid().ToString('N'))

    try {
        # --connect-timeout bounds each attempt. Without it a dead port costs the client's
        # own ~2 s TCP timeout per try, and -DbStartupTimeoutSeconds overshot by 3x.
        #
        # The statement is quoted by hand. Start-Process joins -ArgumentList with spaces and
        # does NOT quote an element that contains one, so "SELECT 1;" arrived as two
        # arguments: the client took SELECT as the statement and "1;" as a database name and
        # answered "ERROR 1049 (42000): Unknown database '1;'" - from a server that was up
        # and answering the whole time. Probing a dead port never showed it, because the
        # connection fails before the arguments are parsed.
        $Probe = Start-Process -FilePath $MariaDBPath `
                               -ArgumentList @("--defaults-extra-file=`"$DefaultsFile`"", "--connect-timeout=3", "-e", "`"SELECT 1`"") `
                               -RedirectStandardOutput $OutFile `
                               -RedirectStandardError $ErrFile `
                               -NoNewWindow -PassThru -Wait

        $ErrorText = ""
        if (Test-Path -LiteralPath $ErrFile) { $ErrorText = (Get-Content -Path $ErrFile -Raw -ErrorAction SilentlyContinue) }

        return @{ ExitCode = $Probe.ExitCode; Error = ("" + $ErrorText).Trim() }
    } finally {
        Remove-Item -Path $OutFile, $ErrFile -Force -ErrorAction SilentlyContinue
    }
}

# Waits for the server to start answering, then reports what it is.
#
# One-shot checking was a race: 1.Start mysql.bat launches mysqld asynchronously, so a run
# started straight afterwards saw "not answering" from a server that was seconds from ready.
# Bad credentials are NOT retried - waiting cannot fix a wrong password, and doing so would
# just turn an instant, clear error into a slow one.
function Wait-ForMariaDb {
    param (
        [int]$TimeoutSeconds
    )

    $Deadline  = (Get-Date).AddSeconds($TimeoutSeconds)
    $Announced = $false
    $LastError = ""

    while ($true) {
        $Probe = Test-MariaDbConnection -DefaultsFile $script:RootDefaultsFile
        if ($Probe.ExitCode -eq 0) { return }

        $LastError = $Probe.Error

        # Only a connection-level failure is worth waiting out. Anything else means the
        # server answered - it is up, and retrying for another half minute just delays a
        # report of a problem that will not fix itself. ERROR 2002/2003 (and the "Can't
        # connect" text) are the not-listening-yet cases; 1045 is a bad password, 1049 a
        # bad database name, and so on.
        $IsStillStarting = $LastError -match "Can't connect|ERROR 200[23]"

        if (-not $IsStillStarting -and -not [string]::IsNullOrWhiteSpace($LastError)) {
            if ($LastError -match 'Access denied') {
                Stop-Pipeline -Message ("The database server is running but rejected the 'root' credentials. " +
                                        "Check -RootPassword. Server said: $LastError")
            }

            Stop-Pipeline -Message ("The database server is running but refused the connection check. " +
                                    "Server said: $LastError")
        }

        if ((Get-Date) -ge $Deadline) { break }

        if (-not $Announced) {
            Write-Host " -> Waiting up to $TimeoutSeconds s for MariaDB to accept connections..."
            $Announced = $true
        }

        Start-Sleep -Seconds 2
    }

    Stop-Pipeline -Message ("MariaDB did not answer within $TimeoutSeconds s. Start it first " +
                            "($MangosInstalationDir\1.Start mysql.bat), or raise -DbStartupTimeoutSeconds. " +
                            "Last error: $LastError")
}

# Finds the vcpkg installation instead of assuming where it lives.
#
# Order of preference: what the caller asked for, then VCPKG_ROOT, then vcpkg.exe on PATH,
# then a couple of conventional spots. The conventional ones are built from environment
# variables rather than a literal drive letter, and every candidate has to actually contain
# vcpkg.exe before it is accepted - so this probes, it never assumes.
function Resolve-VcpkgDirectory {
    param (
        [string]$Explicit
    )

    # An explicit answer is used or it fails - never quietly replaced by a discovered one.
    # Building against a different vcpkg than the one that was asked for is the kind of
    # surprise that costs an evening to notice.
    if (-not [string]::IsNullOrWhiteSpace($Explicit)) {
        if (Test-Path -LiteralPath (Join-Path $Explicit "vcpkg.exe")) {
            return [System.IO.Path]::GetFullPath($Explicit)
        }
        Stop-Pipeline -Message "No vcpkg.exe under the -VcpkgDirectory given: $Explicit"
    }

    $Candidates = New-Object System.Collections.Generic.List[string]

    if (-not [string]::IsNullOrWhiteSpace($env:VCPKG_ROOT)) { $Candidates.Add($env:VCPKG_ROOT) }

    $OnPath = Get-Command "vcpkg.exe" -ErrorAction SilentlyContinue
    if ($OnPath) { $Candidates.Add((Split-Path $OnPath.Source -Parent)) }

    foreach ($Base in @($env:SystemDrive, $env:USERPROFILE)) {
        if ([string]::IsNullOrWhiteSpace($Base)) { continue }
        $Candidates.Add((Join-Path $Base "vcpkg"))
        $Candidates.Add((Join-Path $Base "WOW\vcpkg"))
    }

    foreach ($Candidate in $Candidates) {
        if (Test-Path -LiteralPath (Join-Path $Candidate "vcpkg.exe")) {
            return [System.IO.Path]::GetFullPath($Candidate)
        }
    }

    Stop-Pipeline -Message ("Could not locate vcpkg. Set VCPKG_ROOT, put vcpkg.exe on PATH, " +
                            "or pass -VcpkgDirectory <path>. Looked in: " + ($Candidates -join "; "))
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

# Credentials, the source to build, the vcpkg location, the realm entry and the bot
# population all arrive through param() at the top of this script. Override them on the
# command line - nothing in this file needs editing to run it against a different
# environment, fork or branch.

# CORE ENGINE SYSTEM DIRECTORIES PARAMETERS
# Relative segments only. Every one of them is joined onto the workspace root below, so the
# whole testlab can be moved or renamed without touching anything here.
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

# STEP variable definitions
Write-PipelineHeader -StepName "00: Starting variable definitions"
Write-Host "Setting up variables"

# The workspace root is the single anchor everything else is derived from, so it has to be
# an absolute path before anything uses it.
#
# Not a formality: this script mixes PowerShell cmdlets with .NET file APIs
# ([System.IO.File]::ReadAllText, StreamWriter, Start-Process -RedirectStandardInput), and
# .NET keeps its OWN current directory which PowerShell's Set-Location / Push-Location does
# not update. With a relative root - "-WorkspaceRoot ." or "..\testlab" - the two resolve
# against different directories, and one of the things resolved that way is the
# Remove-Item -Recurse over the server folders.
if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
    # Single-quoted: PowerShell escapes with a backtick, not a backslash, so "\$PSScriptRoot"
    # printed the expanded path instead of the variable name.
    Stop-Pipeline -Message ('Workspace root is empty and $PSScriptRoot gave nothing to fall back on. ' +
                            'Pass -WorkspaceRoot explicitly when dot-sourcing or piping this script.')
}

# GetFullPath's two-argument overload does not exist on .NET Framework (Windows PowerShell
# 5.1), so a relative root is resolved against the caller's directory by hand first.
if ([System.IO.Path]::IsPathRooted($WorkspaceRoot)) {
    $ScriptDirectory = [System.IO.Path]::GetFullPath($WorkspaceRoot)
} else {
    $ScriptDirectory = [System.IO.Path]::GetFullPath((Join-Path (Get-Location).ProviderPath $WorkspaceRoot))
}

if (-not (Test-Path -LiteralPath $ScriptDirectory -PathType Container)) {
    Stop-Pipeline -Message "Workspace root does not exist (or is not a directory): $ScriptDirectory"
}

Write-Host "Workspace root: $ScriptDirectory"

# The four database names, from the prefix unless one was named explicitly.
if ([string]::IsNullOrWhiteSpace($WorldDatabaseName))     { $WorldDatabaseName     = "${DbPrefix}world" }
if ([string]::IsNullOrWhiteSpace($CharacterDatabaseName)) { $CharacterDatabaseName = "${DbPrefix}char" }
if ([string]::IsNullOrWhiteSpace($LoginDatabaseName))     { $LoginDatabaseName     = "${DbPrefix}logon" }
if ([string]::IsNullOrWhiteSpace($LogsDatabaseName))      { $LogsDatabaseName      = "${DbPrefix}logs" }

# create_databases.sql hard-codes the stock tw_* names in CREATE DATABASE and USE
# statements, and the shipped configs point the server at the same four. Renaming therefore
# has to reach both: this map rewrites the schema on import (see Invoke-MySqlFile), and
# step 10 writes the matching connection strings into mangosd.conf and realmd.conf.
$DatabaseNameMap = [ordered]@{
    "tw_world" = $WorldDatabaseName
    "tw_char"  = $CharacterDatabaseName
    "tw_logon" = $LoginDatabaseName
    "tw_logs"  = $LogsDatabaseName
}

$DatabasesAreRenamed = $false
foreach ($StockName in $DatabaseNameMap.Keys) {
    if ($DatabaseNameMap[$StockName] -ne $StockName) { $DatabasesAreRenamed = $true }
}

Write-Host "Databases: $WorldDatabaseName, $CharacterDatabaseName, $LoginDatabaseName, $LogsDatabaseName (user '$DbUser')"

# Singleton first (the authoritative machine-wide guard), then the descriptive lock file.
Assert-SingleInstance
$script:LockFile = Join-Path $ScriptDirectory "pipeline_running.lock"
Assert-NoConcurrentRun
New-PipelineLock
Write-Host "Single-instance lock acquired: $($script:LockFile) (PID $PID)"

# Sweep credential files left by an earlier run before writing new ones.
#
# Stop-Pipeline removes them on every exit the script controls, but Ctrl+C or Task Manager
# kills the process outright and nothing gets the chance - leaving a plaintext password in
# TEMP until something else cleans it up. Observed after an interrupted run on 2026-09-05.
# Safe to sweep unconditionally: TEMP is per-user, and the singleton acquired above means
# no other run of this script is alive to own them.
$StaleCredentials = @(Get-ChildItem -Path $env:TEMP -Filter "tw_pipeline_*.cnf" -File -ErrorAction SilentlyContinue)
if ($StaleCredentials.Count -gt 0) {
    Write-Host "Removing $($StaleCredentials.Count) credential file(s) left by an interrupted run."
    $StaleCredentials | Remove-Item -Force -ErrorAction SilentlyContinue
}


# Define absolute paths based on the workspace root
# Resolved just below by Resolve-MariaDbClient; the portable server's bin\ is only the
# first place it looks.
$PortableMariaDbBin = "$ScriptDirectory\$MangosInstalationDir\$MariaDbFolderName\bin"
$SqlBaseDirectory = "$ScriptDirectory\$MangosTortoiseSourceDir\sql\base"
$CreateDatabasesSql = "$ScriptDirectory\$MangosTortoiseSourceDir\sql\create_databases.sql"
# Define local build artifacts directories
$SourceDir  = Join-Path $ScriptDirectory $MangosTortoiseSourceDir
$BuildDir   = Join-Path $SourceDir $MangosBuildDir
$InstallDir = Join-Path $ScriptDirectory $MangosInstalationDir # Targeted server binaries directory
$BackupFolder  = Join-Path $ScriptDirectory $MangosPipelineBackupDir

# Locate the database client before anything needs it (see Resolve-MariaDbClient).
$MariaDbTools  = Resolve-MariaDbClient -Explicit $MariaDbClientPath -PortableBinDir $PortableMariaDbBin
$MariaDBPath   = $MariaDbTools.Client
$MySQLDumpPath = $MariaDbTools.Dump
Write-Host "Database client: $MariaDBPath (found via: $($MariaDbTools.Source))"

# Materialise the credentials into option files (see New-MySqlDefaultsFile).
$script:RootDefaultsFile = New-MySqlDefaultsFile -User "root"   -Password $RootPassword
$script:DbDefaultsFile   = New-MySqlDefaultsFile -User $DbUser -Password $DbPassword

# Locate vcpkg (see Resolve-VcpkgDirectory) and derive the installed-packages path from it.
$VcpkgDirectory     = Resolve-VcpkgDirectory -Explicit $VcpkgDirectory
$VcpkgExecutable    = Join-Path $VcpkgDirectory "vcpkg.exe"
$VcpkgInstalledPath = Join-Path $VcpkgDirectory "installed\$VcpkgTriplet"
Write-Host "vcpkg: $VcpkgDirectory (triplet $VcpkgTriplet)"

Write-Host "[OK] Variables were set up."  -ForegroundColor Green

# ==============================================================================
# PIPELINE STEP 00b: PREFLIGHT
# ==============================================================================
# Everything the run depends on, verified in one place while the testlab is still intact.
#
# The ordering matters more than the checks do. Step 04 drops the databases and wipes the
# server directory; a missing cmake used to surface in step 08, four steps AFTER that, so
# the failure mode was "your data is gone and the build never started". Anything that can
# be established up front is established here instead.
Write-PipelineHeader -StepName "00b: Preflight"
Write-Host "Verifying the tools and services this run depends on..."

foreach ($Requirement in @(
        @{ Name = "git";   Hint = "install Git for Windows and make sure it is on PATH" },
        @{ Name = "cmake"; Hint = "install CMake 3.16 or newer and tick 'add to PATH' in its installer" })) {

    $Found = Get-Command $Requirement.Name -ErrorAction SilentlyContinue
    if (-not $Found) {
        Stop-Pipeline -Message "Required tool '$($Requirement.Name)' is not on PATH - $($Requirement.Hint)."
    }
    Write-Host " -> $($Requirement.Name): $($Found.Source)"
}

if (-not (Test-Path -LiteralPath $VcpkgExecutable)) {
    Stop-Pipeline -Message "Critical component missing: could not locate vcpkg executable at $VcpkgExecutable"
}
Write-Host " -> vcpkg: $VcpkgExecutable"

Write-Host " -> client: $MariaDBPath"

# The dump tool is only reached on the -SkipBotRegen path, but finding out it is missing
# after the backup was supposed to happen would be far too late.
if ($SkipBotRegen) {
    if (-not $MySQLDumpPath -or -not (Test-Path -LiteralPath $MySQLDumpPath)) {
        Stop-Pipeline -Message ("-SkipBotRegen needs mysqldump.exe / mariadb-dump.exe to back your data up first, " +
                                "and neither is next to $MariaDBPath.")
    }
    Write-Host " -> dump tool: $MySQLDumpPath"
}

# The server has to be running, not merely installed - and it may still be starting.
Wait-ForMariaDb -TimeoutSeconds $DbStartupTimeoutSeconds

# Say which server this actually is. The pipeline is about to drop four databases on it, so
# "which instance am I pointed at" is worth one line in the log.
$ServerIdentity = & $MariaDBPath "--defaults-extra-file=$($script:RootDefaultsFile)" -N -B `
                                 -e "SELECT CONCAT(VERSION(), ' on ', @@hostname, ':', @@port);" 2>$null
Write-Host " -> connected: $ServerIdentity"

# Best effort only: no Visual Studio means CMake has no generator, but vswhere is not
# guaranteed to be present and its absence proves nothing either way.
$VsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path -LiteralPath $VsWhere) {
    $VsInstall = & $VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    if ($VsInstall) {
        Write-Host " -> Visual Studio C++ toolset: $VsInstall"
    } else {
        Write-Warning "No Visual Studio installation with the C++ toolset found. The build in step 08 will fail without it."
    }
}

Write-Host "[OK] Preflight passed - the run has everything it needs." -ForegroundColor Green

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

# $VcpkgExecutable and $VcpkgInstalledPath were resolved and verified in the preflight.

Write-Host "Starting library deployment (ACE and Boost modules) for $VcpkgTriplet..."

# The packages the playerbots module actually includes. Deliberately not the 'boost'
# meta-package: that drags in boost-cobalt, which needs C++20 and does not build under
# Visual Studio 2019.
$VcpkgPackages = @(
    "ace",
    "boost-algorithm",
    "boost-asio",
    "boost-bimap",
    "boost-bind",
    "boost-filesystem",
    "boost-functional",
    "boost-smart-ptr",
    "boost-stacktrace",
    "boost-thread",
    "boost-system"
)

$VcpkgArguments = @("install") + ($VcpkgPackages | ForEach-Object { "${_}:$VcpkgTriplet" })

# vcpkg wants to run from its own directory. Push-Location rather than Start-Process
# -WorkingDirectory on purpose: a Start-Process child writes straight to the console and
# its output never reaches the transcript, and vcpkg's output is exactly what you want in
# the log when a dependency fails to build.
Push-Location $VcpkgDirectory
try {
    Invoke-NativeLogged -Executable $VcpkgExecutable -Arguments $VcpkgArguments
} finally {
    Pop-Location
}

Assert-LastExitCode -Message "Vcpkg deployment sequence failed"

Write-Host "[OK] All required libraries (ACE and Boost modules) are fully verified and deployed." -ForegroundColor Green


# ==============================================================================
# PIPELINE STEP 03: GIT SOURCE MANAGEMENT (WITH SUBMODULES SUPPORT)
# ==============================================================================
Write-PipelineHeader -StepName "03: Git Source Management"
Write-Host "Managing repository source files and submodules..."

if (-not (Test-Path $SourceDir)) {
    Write-Host "Repository not found. Cloning branch '$BranchName' with all submodules..."

    # --recurse-submodules forces Git to automatically clone Eluna and any other nested modules
    Invoke-NativeLogged -Executable "git" -Arguments @("clone", "--branch", $BranchName, "--recurse-submodules", $RepoUrl, $SourceDir)
    Assert-LastExitCode -Message "git clone of '$BranchName' failed"

    Write-Host "[OK] Repository and all nested submodules successfully cloned." -ForegroundColor Green
} else {
    Write-Host "Repository found. Syncing latest code changes and submodules layout..."

    # Move context temporarily to repository folder to execute git updates securely
    Push-Location $SourceDir

    # Make -RepoUrl authoritative for an existing checkout too. Without this it only ever
    # applied to the first clone: every later run pulled from whatever 'origin' happened to
    # be, so pointing the pipeline at a fork silently built the original repository instead.
    Invoke-NativeLogged -Executable "git" -Arguments @("remote", "set-url", "origin", $RepoUrl)
    Assert-LastExitCode -Message "Could not point 'origin' at $RepoUrl"

    Invoke-NativeLogged -Executable "git" -Arguments @("fetch", "origin", "--prune")
    Assert-LastExitCode -Message "git fetch from $RepoUrl failed"

    # Refuse to switch branches over uncommitted work rather than letting git's own
    # "Your local changes would be overwritten by checkout" be the only explanation.
    # Not stashed automatically: a plain build run has no business moving someone's edits
    # out from under them, and unlike the -applyPatches path below there is nothing here
    # that requires a clean tree.
    $CurrentBranch = (git rev-parse --abbrev-ref HEAD 2>$null)
    if ($CurrentBranch -ne $BranchName) {
        $DirtyFiles = @(git status --porcelain --untracked-files=no)
        if ($DirtyFiles.Count -gt 0) {
            Pop-Location
            Stop-Pipeline -Message ("The source tree has uncommitted changes, so it cannot be switched from " +
                                    "'$CurrentBranch' to '$BranchName':`n  " + (($DirtyFiles | Select-Object -First 10) -join "`n  ") +
                                    "`nCommit or stash them in $SourceDir first, or run with -BranchName $CurrentBranch.")
        }
    }

    # Check out the branch, creating it from origin when it is not here yet.
    git rev-parse --verify --quiet "refs/heads/$BranchName" > $null 2>&1
    if ($LASTEXITCODE -eq 0) {
        Invoke-NativeLogged -Executable "git" -Arguments @("checkout", $BranchName)
    } else {
        Invoke-NativeLogged -Executable "git" -Arguments @("checkout", "-b", $BranchName, "--track", "origin/$BranchName")
    }
    Assert-LastExitCode -Message "git checkout of '$BranchName' failed"

    # Pull the remote and branch by name rather than relying on tracking configuration.
    # A bare "git pull" needs an upstream, and a branch created locally - or checked out
    # from a different remote - has none: "There is no tracking information for the current
    # branch", and the run stopped before it built anything.
    Invoke-NativeLogged -Executable "git" -Arguments @("pull", "origin", $BranchName)
    Assert-LastExitCode -Message "git pull of '$BranchName' from $RepoUrl failed"

    Write-Host "Synchronizing and updating git submodules (Eluna engine)..."
    # Update and initialize any new or existing submodules recursively
    Invoke-NativeLogged -Executable "git" -Arguments @("submodule", "update", "--init", "--recursive")
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
    $TargetRemoteUrl = $PatchRemoteUrl
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
if ($SkipBotRegen) {
    Write-PipelineHeader -StepName "(OPTIONAL): Conditional step: export entire database structure and data"
	Write-Host "Parameter -SkipBotRegen is active. Generating full database dumps..."

    # $MySQLDumpPath was resolved next to the client and verified in the preflight.

    # Ensure a temporary backup directory exists on the disk layout
    New-Item -ItemType Directory -Path $BackupFolder -Force | Out-Null

    $TargetDbs = @($CharacterDatabaseName, $LoginDatabaseName)

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

# Drop existing test databases based on the pipeline arguments.
# The client was resolved and the server proved reachable in the preflight, so there is
# nothing left to test for here.
Write-Host "Dropping previous testbed databases..."

# Core infrastructure databases that are ALWAYS dropped and rebuilt
Invoke-MySqlQuery -Query "DROP DATABASE IF EXISTS $WorldDatabaseName; DROP DATABASE IF EXISTS $LoginDatabaseName; DROP DATABASE IF EXISTS $LogsDatabaseName;" `
                  -FailureMessage "Could not drop the $WorldDatabaseName / $LoginDatabaseName / $LogsDatabaseName databases"

if (-not $SkipBotRegen) {
    Write-Host " -> Parameter -SkipBotRegen not active. Dropping characters database..."
    Invoke-MySqlQuery -Query "DROP DATABASE IF EXISTS $CharacterDatabaseName;" -FailureMessage "Could not drop the $CharacterDatabaseName database"
} else {
    Write-Host " -> [SKIP] Parameter -SkipBotRegen is active. Retaining existing character and playerbot data." -ForegroundColor Green
}
Write-Host "[OK] Target databases cleanup sequence completed." -ForegroundColor Green

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
Invoke-MySqlFile -Path $CreateDatabasesSql -RenameDatabases $DatabaseNameMap -FailureMessage "Importing create_databases.sql failed"

# Bulk import all base world SQL files with safety filters enabled
if (-not (Test-Path $SqlBaseDirectory)) {
    Stop-Pipeline -Message "Base world SQL directory is missing: $SqlBaseDirectory"
}

Write-Host "Importing base world SQL files..."
Get-ChildItem "$SqlBaseDirectory\*.sql" | Sort-Object Name | ForEach-Object {
    Write-Host "Importing: $($_.Name)"
    Invoke-MySqlFile -Path $_.FullName -Database $WorldDatabaseName -FailureMessage "Importing a base world SQL file failed"
}

# ==============================================================================
# PIPELINE STEP 06: DATABASE USER CONFIGURATION
# ==============================================================================
Write-PipelineHeader -StepName "06: The 'mangos' user has been configured"
Write-Host "Configuring database user 'mangos'..."

# The password comes from $DbPassword rather than a literal, so changing the parameter
# cannot leave step 13 authenticating with a password that was never set.
#
# Spelled as CREATE USER + ALTER USER + plain GRANTs rather than the shorter
# "GRANT ... IDENTIFIED BY", which MySQL 8 removed: creating a user implicitly through
# GRANT is a syntax error there (ERROR 1064), while MariaDB still accepts it. This form
# works on MariaDB 10.1+ and MySQL 5.7+ alike. ALTER USER follows CREATE USER IF NOT
# EXISTS so an existing account picks up the current password instead of silently keeping
# an old one.
$UserQuery = @"
CREATE USER IF NOT EXISTS '$DbUser'@'localhost' IDENTIFIED BY '$DbPassword';
ALTER USER '$DbUser'@'localhost' IDENTIFIED BY '$DbPassword';
GRANT ALL PRIVILEGES ON $WorldDatabaseName.* TO '$DbUser'@'localhost';
GRANT ALL PRIVILEGES ON $CharacterDatabaseName.* TO '$DbUser'@'localhost';
GRANT ALL PRIVILEGES ON $LoginDatabaseName.* TO '$DbUser'@'localhost';
GRANT ALL PRIVILEGES ON $LogsDatabaseName.* TO '$DbUser'@'localhost';
FLUSH PRIVILEGES;
"@

Invoke-MySqlQuery -Query $UserQuery -FailureMessage "Could not configure the 'mangos' database user"

# ==============================================================================
# PIPELINE STEP (OPTIONAL): CONDITIONAL RESTORE SECTION (IMPORT FULL DATABASE DUMPS)
# ==============================================================================
if ($SkipBotRegen) {
    Write-PipelineHeader -StepName "(OPTIONAL): Restoring original character and logon datasets"
    Write-Host "Restoring preserved production databases from mysqldump files..."

    $TargetDbs    = @($CharacterDatabaseName, $LoginDatabaseName)

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
    USE $CharacterDatabaseName;
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
# $VcpkgInstalledPath was derived from the resolved vcpkg directory in step 00.

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
Invoke-NativeLogged -Executable "cmake" -Arguments @(
    "-B", $BuildDir,
    "-S", $SourceDir,
    "-A", "x64",
    "-DCMAKE_INSTALL_PREFIX=$InstallDir",
    "-DUSE_EXTRACTORS=ON",
    "-DBUILD_MODULES=ON",
    "-DBUILD_EXTENSIONS=ON",
    "-DBUILD_MODS=ON",
    "-DBUILD_PLAYERBOTS=ON",
    "-DUSE_PCH=OFF",
    "-DUSE_PCH_OLD=OFF",
    "-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON",
    "-DMODULE_MOD_PLAYERBOTS=static",
    "-DMODULE_MOD_DUNGEON_CLEAR=static",
    "-DACE_ROOT=$VcpkgInstalledPath",
    "-DBOOST_ROOT=$VcpkgInstalledPath")
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
Invoke-NativeLogged -Executable "cmake" -Arguments @("--install", $BuildDir, "--config", "Release")
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

    # 4. Database connection strings, in the "host;port;user;password;database" form the
    #    server parses.
    #
    #    The shipped templates hard-code 127.0.0.1;3306;mangos;mangos;tw_* and nothing used
    #    to rewrite them. That was wrong in three ways at once even before the databases
    #    became renameable: -DbPassword, -DbUser, -DbHost and -DbPort all changed what the
    #    pipeline connected with while the server kept being told the template's values, so
    #    anything but the defaults produced a server that could not log in to its own
    #    databases. All four lines are now written from the settings actually in use.
    $ConfHost = if ([string]::IsNullOrWhiteSpace($DbHost)) { "127.0.0.1" } else { $DbHost }
    $ConfPort = if ($DbPort -gt 0) { $DbPort } else { 3306 }

    $OldLoginInfoPattern     = '^LoginDatabase\.Info\s*=\s*".*"'
    $OldWorldInfoPattern     = '^WorldDatabase\.Info\s*=\s*".*"'
    $OldCharacterInfoPattern = '^CharacterDatabase\.Info\s*=\s*".*"'
    $OldLogsInfoPattern      = '^LogsDatabase\.Info\s*=\s*".*"'

    $NewLoginInfoSetting     = "LoginDatabase.Info = `"$ConfHost;$ConfPort;$DbUser;$DbPassword;$LoginDatabaseName`""
    $NewWorldInfoSetting     = "WorldDatabase.Info = `"$ConfHost;$ConfPort;$DbUser;$DbPassword;$WorldDatabaseName`""
    $NewCharacterInfoSetting = "CharacterDatabase.Info = `"$ConfHost;$ConfPort;$DbUser;$DbPassword;$CharacterDatabaseName`""
    $NewLogsInfoSetting      = "LogsDatabase.Info = `"$ConfHost;$ConfPort;$DbUser;$DbPassword;$LogsDatabaseName`""

    # 5. Read content, execute every replacement sequentially, and stream back to file
    (Get-Content $MangosdConf) `
        -replace $OldUpdatePath, $NewUpdatePath `
        -replace $OldDataDirPattern, $NewDataDirSetting `
        -replace $OldLogsDirPattern, $NewLogsDirSetting `
        -replace $OldHonorDirPattern, $NewHonorDirSetting `
        -replace $OldPDumpDirPattern, $NewPDumpDirSetting `
		-replace $OldLuaDirPattern, $NewLuaDirSetting `
        -replace $OldLoginInfoPattern, $NewLoginInfoSetting `
        -replace $OldWorldInfoPattern, $NewWorldInfoSetting `
        -replace $OldCharacterInfoPattern, $NewCharacterInfoSetting `
        -replace $OldLogsInfoPattern, $NewLogsInfoSetting `
        | Set-Content $MangosdConf

    Write-Host "[OK] mangosd.conf updated: directories, and all four database connections." -ForegroundColor Green
} else {
    Stop-Pipeline -Message "Configuration injection failed: Could not locate mangosd.conf inside $MangosEtcDir"
}

# realmd reads only the login database, and spells the key without the dot.
$RealmdConf = Join-Path $EtcDir "realmd.conf"

if (Test-Path $RealmdConf) {
    $ConfHost = if ([string]::IsNullOrWhiteSpace($DbHost)) { "127.0.0.1" } else { $DbHost }
    $ConfPort = if ($DbPort -gt 0) { $DbPort } else { 3306 }

    (Get-Content $RealmdConf) `
        -replace '^LoginDatabaseInfo\s*=\s*".*"', "LoginDatabaseInfo = `"$ConfHost;$ConfPort;$DbUser;$DbPassword;$LoginDatabaseName`"" `
        | Set-Content $RealmdConf

    Write-Host "[OK] realmd.conf updated with the login database connection." -ForegroundColor Green
} else {
    Stop-Pipeline -Message "Configuration injection failed: Could not locate realmd.conf inside $MangosEtcDir"
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
        Invoke-MySqlFile -Path $_.FullName -Database $WorldDatabaseName -FailureMessage "Importing a PlayerBot world SQL file failed"
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
        Invoke-MySqlFile -Path $_.FullName -Database $CharacterDatabaseName -FailureMessage "Importing a PlayerBot characters SQL file failed"
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
                  -Database $LoginDatabaseName `
                  -DefaultsFile $script:DbDefaultsFile `
                  -FailureMessage "Could not register the local realm in $LoginDatabaseName.realmlist"

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
@echo off
:: mysqld resolves its datadir RELATIVE to the working directory, so it has to start from
:: its own bin folder - launched from anywhere else it dies with
:: "Can't change dir to ...\..\data\ (Errcode: 2)".
::
:: "cd /d %~dp0..." rather than a bare "cd <folder>": %~dp0 is this file's own directory,
:: so the launcher works whatever directory it is started from - a bare relative cd only
:: worked when the shell happened to sit in the server folder - and /d also crosses drives.
cd /d "%~dp0$MariaDbFolderName\bin"
if errorlevel 1 (
    echo Could not enter "%~dp0$MariaDbFolderName\bin".
    echo Is the portable MariaDB in place, and is its folder still named "${MariaDbFolderName}"?
    pause
    exit /b 1
)

:: A second instance cannot bind the port, and its console window closes immediately -
:: which looks exactly like "the database will not start" when it is in fact already up.
netstat -ano | findstr /r /c:"LISTENING" | findstr ":3306 " >nul
if not errorlevel 1 (
    echo MariaDB is already running on port 3306 - nothing to do.
    pause
    exit /b 0
)

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

# Release the run lock and singleton, drop the temporary credential files and close the
# transcript on the success path too.
Remove-PipelineLock
Remove-PipelineSingleton
Remove-PipelineCredentialFiles
Write-Host "[OK] Pipeline execution fully completed! Server environment is ready." -ForegroundColor Green
try { Stop-Transcript | Out-Null } catch { }
