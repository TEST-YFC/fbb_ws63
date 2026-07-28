# Target metadata

This directory is the machine-readable source of truth for SDK identity,
device architecture, build targets, capabilities, samples, known issues, and
SVD register descriptions.

`hs-fbb-cli` exposes these facts through `fbb target`. Tools and skills should
consume that interface instead of parsing build scripts or copying chip facts.

Board wiring, attached peripherals, and project acceptance criteria belong to
board packages or the developer's project workspace, not to this directory.

Run the metadata gate from the repository root:

```bash
python3 ci/test_target_metadata.py
```
