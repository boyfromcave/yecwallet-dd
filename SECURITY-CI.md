# Security CI

Three workflows scan this repository; none of them opens an issue, and only Semgrep's `sync` job and
Dependabot open PRs (at most four Semgrep bucket PRs, and one grouped security PR per ecosystem).
Every other result goes to the run's step summary and, as SARIF, to *Security → Code scanning*.

| Leg | Workflow / job | Runs on | Gate | Results |
|---|---|---|---|---|
| Semgrep | `semgrep.yml` | PR (diff-aware), push, nightly 02:17 UTC | PR fails on a new **critical** finding | summary, code scanning `semgrep`, bucket PRs `semgrep/<bucket>` (`contrib/semgrep/README.md`) |
| Semgrep local rules | `secops.yml` `rule-tests` | PR, push, nightly | fails when a rule misses or over-matches its test cases | job log |
| CodeQL | `codeql.yml` | PR, push to `upgrade/vault`, weekly | report only (pre-existing alerts never block a PR) | code scanning `codeql-c-cpp`, `codeql-python`, `codeql-actions` |
| Workflow security | `secops.yml` `zizmor` | PR, push, nightly 03:43 UTC | fails on any zizmor finding (default persona) | summary, code scanning `zizmor` |
| Secrets | `secops.yml` `gitleaks` | PR (the PR's commits), push (pushed commits), nightly + dispatch (full history) | fails on a leak not in `.gitleaksignore` | summary (values redacted), code scanning `gitleaks` |
| Dependency vulnerabilities | `secops.yml` `osv-scanner` | PR, push, nightly | fails on a known vulnerability not accepted in `osv-scanner.toml` | summary, code scanning `osv-scanner` |
| Dependency updates | `.github/dependabot.yml` | GitHub's schedule | security updates only, grouped per ecosystem | one PR per ecosystem |

What each covers here:

- **Semgrep**: C/C++ (semgrep-rules `c`, 0xdea `rules/c`, with the node lines' audit-lead rules sent to
  the audit report), Python, shell, Dockerfile, secrets and GitHub Actions rules at pinned commits, plus
  the wallet's own rules in `contrib/semgrep/rules/yecwallet.yaml`:
  `secret-logged` (an RPC password, passphrase, seed phrase or key reaching `Logger::write` or `qDebug`),
  `secret-in-qsettings` (secrets persisted in plain-text QSettings), `shell-or-command-string`
  (`ycashd` and helpers started through a shell or a single command string instead of program +
  argument list) and `verification-disabled` (TLS certificate checks turned off).
  Vendored code (`src/3rdparty/`, `singleapplication/`) and Qt-generated files are not scanned
  (`.semgrepignore`).
- **CodeQL** reads the C++ without building it (`build-mode: none`): the Qt build stays in
  `yecwallet-tests.yml`.
- **OSV-Scanner**: the wallet has no package-manager lockfile; it scans the tooling's hash-locked pip
  files and identifies the vendored C++ libraries under `src/3rdparty/` by content.
- **Dependabot**: GitHub Actions (pinned by SHA) and the tooling's pip files.

## Accepting a finding

Always with a reason a reviewer can check, in the same PR as the change that needs it:

- **Semgrep**: `contrib/semgrep/accepted.json` (by fingerprint, or a rule over paths), or `audit_rules` /
  `exclude_rules` in `contrib/semgrep/config.json` for a whole rule (`contrib/semgrep/README.md`).
- **gitleaks**: the finding's fingerprint (`<commit>:<file>:<rule>:<line>`) in `.gitleaksignore`, with a
  comment saying why it is not a secret. Test vectors and public constants only: a real secret is
  rotated, never ignored.
- **OSV-Scanner**: an `[[IgnoredVulns]]` entry in `osv-scanner.toml` with `reason` and an `ignoreUntil`
  at most 90 days out; otherwise bump the dependency.
- **zizmor**: fix the workflow; an inline `# zizmor: ignore[<audit>]` only with a comment saying why.
- **CodeQL**: dismiss the alert in Code scanning with a reason.

## Tool versions

All installed at exact versions: release binaries checked against a sha256 recorded in `secops.yml`
(gitleaks, osv-scanner), zizmor and semgrep from hash-locked requirements
(`.github/secops/zizmor-requirements.txt`, `contrib/semgrep/requirements.txt`), every action pinned to a
full commit SHA. The same versions are used across the Yellowback repositories.

## Repository settings the owner sets once

- *Settings → Code security*: Dependabot alerts and Dependabot security updates on (for the grouped PRs);
  code scanning needs nothing more for a public repository.
- *Settings → Actions → General*: "Allow GitHub Actions to create and approve pull requests" (Semgrep bucket
  PRs), and optionally the secret `SEMGREP_PR_TOKEN` so CI runs on those PRs (`contrib/semgrep/README.md`).
