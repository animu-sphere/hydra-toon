## Summary

Describe the change and why it is needed. Link related issues.

## Verification

List the commands run and their results (for example, `cmake --build`, `ctest`,
or `ost test` and `ost validate`). If a check was not run, explain why.

## Checklist

- [ ] Tests cover changed behavior, or I have explained why they do not.
- [ ] Affected documentation and capability claims are updated, or not applicable.
- [ ] Completed work is removed from the roadmap; live status stays in the capability matrix.
- [ ] Reference comparisons cover a visible defect, semantic change or recurrence under the [stop rule](../docs/design/MATERIAL_POLICY.md#121-reference-comparison-and-stop-rule), or are not applicable.
- [ ] `python scripts/check_docs.py` passes; docs-only changes do not need GPU/full CTest runs.
- [ ] No private data or non-redistributable assets are included.
