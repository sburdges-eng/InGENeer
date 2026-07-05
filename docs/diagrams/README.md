# CAD platform diagrams

Interactive hierarchy of the **CAD platform** (`~/Dev/CAD`): InGENeer, auracad, TOTaLi, Liberals4Liberty, and related repos. `~/Dev` is the wider dev workspace.

## Interactive explorer (browser)

```bash
open docs/diagrams/cad-platform-explorer.html
```

**Important:** open from `docs/diagrams/` so `cad-platform-nodes.js` loads (same folder as the HTML file).

**Controls**

- **+ / −** on a tree row — expand or collapse that branch
- **Click a row** — select node; right panel shows focused Mermaid subgraph + detail
- **Expand all / Collapse all / Reset to root** — toolbar buttons
- **Breadcrumb** — jump to an ancestor

Requires network on first load (Mermaid 10 UMD from jsDelivr CDN).

### Troubleshooting

| Symptom | Fix |
|---------|-----|
| `Cannot read properties of undefined (reading 'run')` | Fixed in current build — uses `mermaid.render()` after UMD script loads. Hard-refresh (Cmd+Shift+R). |
| Blank diagram / “Loading Mermaid…” | Check network; ensure `mermaid.min.js` loaded in DevTools Network tab. |
| Empty tree | Ensure `cad-platform-nodes.js` is beside the HTML file (not moved alone). |

## Cursor Canvas (in IDE)

Open beside chat: [cad-platform-hierarchy.canvas.tsx](/Users/seanburdges/.cursor/projects/Users-seanburdges-Dev-InGENeer-orchestrator/canvases/cad-platform-hierarchy.canvas.tsx)

- **CollapsibleSection** tree on the left
- **Focus** button selects a node; DAG subgraph on the right
- **Open HTML explorer** launches the browser version from the repo

## Static Mermaid source

`cad-platform-hierarchy.mmd` — full platform graph for CLI export:

```bash
# if mermaid-cli is installed: npm i -g @mermaid-js/mermaid-cli
mmdc -i docs/diagrams/cad-platform-hierarchy.mmd -o docs/diagrams/cad-platform-hierarchy.svg
```

## Shared data

`cad-platform-nodes.js` — node tree consumed by the HTML explorer (do not import from canvas; canvas embeds its own copy per Cursor rules).
