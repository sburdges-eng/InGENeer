# Developer setup (InGENeer)

## Prerequisites

- **Python** 3.11+ (3.11 matches CI; 3.12+ usually fine).
- **.NET SDK** 10.x (for `icad-addin/`).
- **Git**

## Python orchestrator

```bash
cd orchestrator
python3 -m venv .venv
source .venv/bin/activate   # Windows: .venv\Scripts\activate
pip install -e ".[dev]"
python -m pytest -q
ingenieer-run --help
```

### Optional environment variables

Documented in [`.env.example`](../.env.example). The common override is **`INGENEER_SCHEMA_DIR`** when running tests or CLI from a non-standard layout.

## C# bridge / loopback host

From the **repository root**:

```bash
dotnet restore icad-addin/InGENeer.IcadAddin.slnx
dotnet build icad-addin/InGENeer.IcadAddin.slnx -c Release
```

No IntelliCAD/Carlson host is required for this build; projects are reference libraries and a console loopback host.

## Schema handoff (orchestrator ↔ CAD workspace)

```bash
./scripts/copy_schema_handoff.sh
```

Use the output bundle when working in a **separate** Cursor window or repo for C# host code (see architecture SOP 2).

## CI parity

GitHub Actions runs **gitleaks**, **pytest** (Python 3.11), and **dotnet build** on push/PR to `main`. See [`.github/workflows/ci.yml`](../.github/workflows/ci.yml).
