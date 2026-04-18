git fetch --all --prune
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$latest = git for-each-ref --format="%(refname:short)" --sort=-committerdate refs/remotes/origin/backup/main-* | Select-Object -First 1
if (-not $latest) {
    $latest = git for-each-ref --format="%(refname:short)" --sort=-committerdate refs/heads/backup/main-* | Select-Object -First 1
}

if (-not $latest) {
    Write-Error "No backup/main-* branch found on origin or local."
    exit 1
}

git checkout main
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

git reset --hard $latest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

git push --force-with-lease origin main
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Restored main from $latest and pushed to origin/main"
exit 0
