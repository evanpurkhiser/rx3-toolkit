# Branch assembly

The fork keeps three independent core PR branches and module-only histories.
The graph in `assembly` produces three layers:

1. `evanpurkhiser/core` merges the three core PR branches onto upstream main.
2. Each public feature branch is staged from that shared core and merges one
   module source history.
3. `evanpurkhiser/integration` merges all public feature branches.

Each public feature tip is a merge whose first parent is the assembled core and
whose second parent is its module-only history. The assembler recovers that
second parent before recreating the feature branch, so the source histories do
not need their own published branches or redundant CI runs.

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
