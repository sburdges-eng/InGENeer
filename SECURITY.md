# Security policy

## Supported versions

| Line | Supported |
|------|-----------|
| `main` (0.x orchestrator + reference bridge) | Yes |

## Reporting a vulnerability

Please use **GitHub [private vulnerability reporting](https://docs.github.com/en/code-security/security-advisories/guidance-on-reporting-and-writing-information-about-vulnerabilities/privately-reporting-a-security-vulnerability)** for this repository if it is enabled (Settings → Security → Code security → Private vulnerability reporting).

If private reporting is not available, open a **draft security issue** only if your tracker supports restricting visibility; otherwise contact repository maintainers directly. Do not post exploit details in public issues.

## Scope notes

- The Python orchestrator and loopback C# host are intended for **trusted local / controlled** environments. Treat any future **network-exposed** bridge as high risk and review authentication, authorization, and input validation explicitly.
- Never commit **API keys, tokens, or host credentials**. Use environment variables or your platform’s secret store. See [`.env.example`](.env.example) (placeholders only).

## Response expectations

Maintainers will acknowledge valid reports when possible; timing depends on availability. Critical issues (remote code execution, credential leakage) take priority.
