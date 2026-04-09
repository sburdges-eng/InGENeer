# Repository setup (maintainers)

Checklist after creating the GitHub (or GitLab) remote—do once per hosting provider.

## Remote and backups

1. Add remote: `git remote add origin <url>` then `git push -u origin main`.
2. Confirm **default branch** is `main` (or align CI branch filters in `.github/workflows/ci.yml`).
3. Enable **branch protection** on the default branch:
   - Require pull request before merging
   - Require status checks: **CI** jobs (`python`, `dotnet`, `gitleaks` as applicable)
   - Optional: require linear history, disallow force-push

## Security features (GitHub)

1. **Dependabot alerts** — enable for the repo (security tab).
2. **Secret scanning** — enable if available on your plan (complements CI **gitleaks**).
3. **Private vulnerability reporting** — enable (see [SECURITY.md](../SECURITY.md)).

## Dependabot

[`.github/dependabot.yml`](../.github/dependabot.yml) schedules weekly updates for **pip** (`orchestrator/`), **NuGet** (`icad-addin/`), and **GitHub Actions**. Adjust cadence or ignore rules as needed.

## License

[LICENSE](../LICENSE) is **MIT**. If the product is proprietary, replace with your legal team’s text and update [CONTRIBUTING.md](../CONTRIBUTING.md) accordingly.

## Optional

- Add repository **topics** (e.g. `cad`, `orchestrator`, `python`, `dotnet`).
- Require **CODEOWNERS** for `schemas/` and `AutonomAtIon/` when multiple teams touch contracts.
