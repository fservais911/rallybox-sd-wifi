git checkout main
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

git add -A
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$ts = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
git commit -m "snapshot: $ts"
if ($LASTEXITCODE -eq 0) {
    git push origin main
    exit $LASTEXITCODE
}

Write-Host "No changes to commit (or commit failed)."
exit 0
