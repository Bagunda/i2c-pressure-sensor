# Setup GitHub access for Cursor IDE

Write-Host "=== GitHub Access Setup ===" -ForegroundColor Cyan
Write-Host ""

# Check GitHub CLI
$ghInstalled = $false
$ghCheck = Get-Command gh -ErrorAction SilentlyContinue
if ($ghCheck) {
    $ghInstalled = $true
    Write-Host "[OK] GitHub CLI installed" -ForegroundColor Green
} else {
    Write-Host "[X] GitHub CLI not installed" -ForegroundColor Yellow
}

# Check token
$hasToken = $false
if ($env:GITHUB_TOKEN) {
    $hasToken = $true
    Write-Host "[OK] GITHUB_TOKEN is set" -ForegroundColor Green
} else {
    Write-Host "[X] GITHUB_TOKEN not set" -ForegroundColor Yellow
}

# Check credential helper
$credHelper = git config --global credential.helper 2>$null
if ($credHelper) {
    Write-Host "[OK] Git credential helper: $credHelper" -ForegroundColor Green
} else {
    Write-Host "[X] Git credential helper not configured" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "=== Recommended Actions ===" -ForegroundColor Cyan
Write-Host ""

if (-not $ghInstalled) {
    Write-Host "1. Install GitHub CLI:" -ForegroundColor Yellow
    Write-Host "   winget install --id GitHub.cli" -ForegroundColor White
    Write-Host "   Or download from https://cli.github.com/" -ForegroundColor White
    Write-Host ""
    Write-Host "   Then authorize:" -ForegroundColor Yellow
    Write-Host "   gh auth login" -ForegroundColor White
    Write-Host ""
}

if (-not $hasToken) {
    Write-Host "2. Or set GITHUB_TOKEN:" -ForegroundColor Yellow
    Write-Host "   Create token at https://github.com/settings/tokens" -ForegroundColor White
    Write-Host "   Permissions: repo" -ForegroundColor White
    Write-Host ""
    Write-Host "   Then run:" -ForegroundColor Yellow
    Write-Host '   [System.Environment]::SetEnvironmentVariable("GITHUB_TOKEN", "your_token_here", "User")' -ForegroundColor White
    Write-Host "   Restart Cursor IDE" -ForegroundColor White
    Write-Host ""
}

if (-not $credHelper) {
    Write-Host "3. Configure Git credential helper:" -ForegroundColor Yellow
    Write-Host "   git config --global credential.helper manager-core" -ForegroundColor White
    Write-Host ""
}

if ($ghInstalled -or $hasToken) {
    Write-Host "[OK] GitHub access configured!" -ForegroundColor Green
} else {
    Write-Host "[!] Follow steps above to configure." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "After setup I will be able to:" -ForegroundColor Cyan
Write-Host "  - Create repositories automatically" -ForegroundColor White
Write-Host "  - Push/pull without prompts" -ForegroundColor White
Write-Host "  - Manage repository settings" -ForegroundColor White
