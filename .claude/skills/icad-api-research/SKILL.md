---
name: icad-api-research
description: "Research the correct Carlson / IntelliCAD (iCAD) API for a command before writing C#, to satisfy Rule 4 (no API hallucinations). Produces a cited // TODO block, never invented method calls. Usage: /icad-api-research <CommandName or capability>"
disable-model-invocation: true
args: capability
---

# iCAD / Carlson API Research

Rule 4 of the AutonomAtIon architecture: **never guess proprietary CAD API methods** (Autodesk,
Bentley, Siemens, Carlson, ITC/IntelliCAD). Uncertain APIs must use `// TODO` with a doc citation
request. This skill runs the research loop that produces that citation.

## Steps

### 1. Search the repo's own research first

```bash
cd /Users/seanburdges/Dev/InGENeer
grep -ni "<capability>" docs/CARLSON_API_RESEARCH.md docs/INTENT_COMMAND_API_REFERENCE.md
```

If a documented API + citation already exists, use it directly and skip to step 4.

### 2. Search external documentation

If not found in-repo, search the official sources (Carlson SDK docs, IntelliCAD/ITC ARX/.NET API
references, ODA docs). Use WebSearch/WebFetch. Capture: exact namespace, class, method signature,
and the source URL + access date.

### 3. Record the finding

Append a row to `docs/CARLSON_API_RESEARCH.md` under the relevant section:

| Capability | API (namespace.Class.Method) | Source (URL, accessed YYYY-MM-DD) | Confidence |

If you could NOT find an authoritative source, say so explicitly — do not fabricate a signature.

### 4. Emit the C# stub block

Produce the dispatcher block for the user to paste (do NOT invent calls if unconfirmed):

```csharp
case "<CommandName>":
    // TODO(Rule 4): Implement <CommandName> via <confirmed API or "API UNCONFIRMED">.
    // Source: <URL accessed YYYY-MM-DD | "needs doc citation">
    // Parameters: <parameters>
    return BridgeExecutionResult.Stub("<CommandName>", intent);
```

### 5. Report

Tell the user: what was found (with citation), whether confidence is high/low, and whether the
stub is ready to implement or still blocked on an authoritative source. Never present an unconfirmed
API as confirmed.
