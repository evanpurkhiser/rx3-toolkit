# Integration branches

Integration branches are disposable test branches assembled from the fork's
core and module branches. The configuration in this directory records the
merge order and the historical base used to isolate each module's commits.

The `Assemble integration branch` workflow rebuilds
`integration/full-t3u-fader` after CI succeeds on any configured input branch.
It can also be started manually from the Actions page.

To preview the same assembly locally without publishing it:

```sh
scripts/assemble-integration.sh --remote fork
```

Pass `--validate` to run the source checks. Pass `--push` only when the remote
integration branch should be replaced; publication uses an exact force lease.

When a module branch is rebased onto a different core stack, update its `pick`
base in `full-t3u-fader.conf`. Any merge or source conflict stops the assembly.
