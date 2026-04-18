$ts = Get-Date -Format "yyyyMMdd-HHmmss"
$branch = "backup/main-$ts"

git checkout main
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

git branch $branch main
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

git push -u origin $branch
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Created and pushed $branch"
exit 0
