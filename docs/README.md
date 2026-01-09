# Engineering Memory Library

Purpose: capture reusable context so anyone can answer "symptom, location, fix, risk"
fast and consistently.

## Structure
- worklog/YYYY-MM-DD.md
- incidents/INC-YYYYMMDD-xx.md
- adr/ADR-xxxx.md
- runbook/ (optional)

## Recording Checklist
1. Clear location: module, function, file path, config name.
2. Time semantics: exchange timestamp vs local timestamp.
3. Trigger conditions: market data, order state, risk rules.
4. Minimal repro: shortest steps and key inputs.
5. Evidence: logs or snapshots (redacted).
6. Separate facts and hypotheses.
7. Fix boundary and regression hooks.

## Templates

### Worklog (YYYY-MM-DD.md)
```md
# YYYY-MM-DD Worklog - Agentd Debug

## Goals
- [ ] ...

## Change Summary
- Files:
- Interfaces/fields:

## Key Assumptions
- Assumption A: ...

## Blockers
- [ ] ...

## Next
- [ ] ...
```

### Incident (INC-YYYYMMDD-xx.md)
```md
# INC-YYYYMMDD-xx Short Title

## Metadata
- Environment/version:
- Impact:
- Related code:

## Symptom
- Description:
- Evidence:

## Minimal Repro
1)
2)

## Root Cause
- Facts:
- Hypothesis:

## Fix
- Changes:
- Risks/boundaries:

## Verification
- Tests/replay/monitoring:
```

### ADR (ADR-xxxx.md)
```md
# ADR-xxxx Title

## Context

## Decision

## Alternatives

## Consequences and Risks
```

## 10-Minute Loop
1. Start a Worklog and write goals.
2. When a problem appears, create an Incident with symptom, evidence, repro.
3. After the fix, fill root cause, fix, verification, regression hooks.
4. Weekly: summarize repeated issues into the runbook.

## Notes
- Redact account, symbol, money, and gateway details.
- Repro first, then conclusions.
- No regression hook means repeat failures.
