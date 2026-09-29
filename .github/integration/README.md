# Branch assembly

The fork keeps three independent core PR branches and module-only source refs.
The graph in `assembly` produces three layers:

1. `evanpurkhiser/core` merges the three core PR branches onto upstream main.
2. Each public feature branch is staged from that shared core and merges one
   `_assembly/source/*` ref.
3. `evanpurkhiser/integration` merges all public feature branches.

The source refs prevent a core rebuild from losing module history. Development
for a module belongs on its source ref; rerunning the assembler recreates its
public feature branch with the current shared core beneath it.

Git Assembler 1.5 performs the graph rebuild. The exact upstream program and
its GPLv3 license are vendored under `vendor/git-assembler`.

Run a local rebuild with:

```sh
scripts/assemble-branches.sh
```

Pass `--validate` to run source checks. `--push` replaces all assembled branch
refs with exact force leases. The GitHub workflow is manual so branch assembly
only runs when explicitly requested.

The module-specific CI edits overlap in the source branches. Assembly uses the
union merge driver to pass that mechanical overlap, then replaces the result on
the integration branch with the reviewed combined workflow in `ci.yml`.
