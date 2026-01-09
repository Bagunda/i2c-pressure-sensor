# Create GitHub repository and push code
# Requires Personal Access Token with repo permissions

param(
    [string]$RepoName = "i2c-pressure-sensor",
    [string]$GitHubUser = "shururik",
    [string]$Token = ""
)

$ErrorActionPreference = "Stop"

Write-Host "Creating GitHub repository..." -ForegroundColor Cyan

# If token not specified, try to get from environment variables
if ([string]::IsNullOrEmpty($Token)) {
    # First from current session
    $Token = $env:GITHUB_TOKEN
    
    # If not in current session, read from system environment variables
    if ([string]::IsNullOrEmpty($Token)) {
        $Token = [System.Environment]::GetEnvironmentVariable("GITHUB_TOKEN", "User")
    }
    
    # If still not found, try Machine scope
    if ([string]::IsNullOrEmpty($Token)) {
        $Token = [System.Environment]::GetEnvironmentVariable("GITHUB_TOKEN", "Machine")
    }
}

if ([string]::IsNullOrEmpty($Token)) {
    Write-Host "ERROR: GitHub Personal Access Token not found" -ForegroundColor Red
    Write-Host ""
    Write-Host "Create token at https://github.com/settings/tokens" -ForegroundColor Yellow
    Write-Host "Required permissions: repo" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Then run:" -ForegroundColor Yellow
    Write-Host "  .\create_github_repo.ps1 -Token YOUR_TOKEN" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Or set environment variable:" -ForegroundColor Yellow
    Write-Host '  [System.Environment]::SetEnvironmentVariable("GITHUB_TOKEN", "your_token", "User")' -ForegroundColor Yellow
    exit 1
}

# Create repository via GitHub API
$headers = @{
    "Authorization" = "token $Token"
    "Accept" = "application/vnd.github.v3+json"
}

$body = @{
    name = $RepoName
    description = "I2C Pressure Sensor 0-10 bar examples and practical notes for ESP32"
    private = $false
} | ConvertTo-Json

try {
    $response = Invoke-RestMethod -Uri "https://api.github.com/user/repos" `
        -Method Post `
        -Headers $headers `
        -Body $body `
        -ContentType "application/json"
    
    Write-Host "[OK] Repository created: $($response.html_url)" -ForegroundColor Green
    
    # Extract username from response
    $actualUser = $response.owner.login
    if ($actualUser -ne $GitHubUser) {
        Write-Host "Note: Repository created under user: $actualUser" -ForegroundColor Yellow
    }
    
    # Add remote and push
    Write-Host ""
    Write-Host "Adding remote and pushing code..." -ForegroundColor Cyan
    
    $repoUrl = "https://$Token@github.com/$actualUser/$RepoName.git"
    
    git remote remove origin 2>$null
    git remote add origin $repoUrl
    git branch -M main
    git push -u origin main
    
    Write-Host ""
    Write-Host "[OK] Done! Repository available at:" -ForegroundColor Green
    Write-Host "  $($response.html_url)" -ForegroundColor Cyan
    
} catch {
    Write-Host "ERROR creating repository:" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    
    if ($_.Exception.Response.StatusCode -eq 401) {
        Write-Host ""
        Write-Host "Invalid token. Check token at https://github.com/settings/tokens" -ForegroundColor Yellow
    } elseif ($_.Exception.Response.StatusCode -eq 422) {
        Write-Host ""
        Write-Host "Repository with this name already exists." -ForegroundColor Yellow
        Write-Host "Use different name or delete existing repository." -ForegroundColor Yellow
    }
    
    exit 1
}
