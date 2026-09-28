# Integration branches

Integration branches are disposable test branches assembled from the fork's
core and module branches. The configuration in this directory records the
merge order, the historical base used to isolate each module's commits, and
the combined CI workflow used only on the assembled branch.

The `Assemble integration branch` workflow rebuilds
`evanpurkhiser/integration` when it is started from the Actions page. Run it
after the configured source branches have passed their own CI.

To preview the same assembly locally without publishing it:

```sh
scripts/assemble-integration.sh --remote fork
```

Pass `--validate` to run the source checks. Pass `--push` only when the remote
integration branch should be replaced; publication uses an exact force lease.

When a module branch is rebased onto a different core stack, update its `pick`
base in `integration.conf`. Any merge or source conflict stops the assembly.
