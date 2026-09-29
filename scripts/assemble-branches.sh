#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0

set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/assemble-branches.sh [--push] [--validate] [--remote NAME]
                                    [--upstream NAME]

Rebuild the shared core, each feature branch, and the integration branch from
.github/integration/assembly. Source and core branches remain unchanged.

  --push      replace the assembled branches using exact force leases
  --validate  run source checks on the assembled integration branch
  --remote    fork remote containing the core and module source branches
  --upstream  remote containing the upstream main branch
EOF
}

push=false
validate=false
remote=
upstream=

while (($#)); do
    case "$1" in
        --push) push=true ;;
        --validate) validate=true ;;
        --remote)
            [[ $# -ge 2 ]] || { echo "--remote requires a name" >&2; exit 2; }
            remote=$2
            shift
            ;;
        --upstream)
            [[ $# -ge 2 ]] || { echo "--upstream requires a name" >&2; exit 2; }
            upstream=$2
            shift
            ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

repo=$(git rev-parse --show-toplevel)
assembler=$repo/.github/integration/vendor/git-assembler/git-assembler
assembly=$repo/.github/integration/assembly
integration=evanpurkhiser/integration

readonly branches=(
    evanpurkhiser/core
    feature/usb-wifi
    feature/rx3-ssh
    feature/pcm-tcp-stream
    feature/framebuffer-video-stream
    feature/remote-control
    ref/link-export-activate
    feature/crossfader-curve
    "$integration"
)

if [[ -z "$remote" ]]; then
    if git -C "$repo" remote get-url fork >/dev/null 2>&1; then
        remote=fork
    else
        remote=origin
    fi
fi

git -C "$repo" remote get-url "$remote" >/dev/null

if [[ -z "$upstream" ]]; then
    if git -C "$repo" remote get-url origin 2>/dev/null | grep -q 'Tratosca/rx3-toolkit'; then
        upstream=origin
    elif git -C "$repo" remote get-url upstream >/dev/null 2>&1; then
        upstream=upstream
    else
        git -C "$repo" remote add upstream https://github.com/Tratosca/rx3-toolkit.git
        upstream=upstream
    fi
fi

git -C "$repo" remote get-url "$upstream" >/dev/null

git -C "$repo" fetch --no-tags "$upstream" \
    +refs/heads/main:refs/remotes/assembly-upstream/main
git -C "$repo" fetch --no-tags "$remote" \
    '+refs/heads/core/*:refs/remotes/assembly-fork/core/*' \
    '+refs/heads/_assembly/source/*:refs/remotes/assembly-fork/_assembly/source/*'

mkdir -p "$repo/.git/info"
cat > "$repo/.git/info/attributes" <<'EOF'
.github/workflows/ci.yml merge=union
docs/README.md merge=union
EOF

git -C "$repo" config commit.gpgSign false
"$assembler" --config "$assembly" --assemble --create --recreate --all

git -C "$repo" switch "$integration"
cp "$repo/.github/integration/ci.yml" "$repo/.github/workflows/ci.yml"

if ! git -C "$repo" diff --quiet -- .github/workflows/ci.yml; then
    git -C "$repo" add .github/workflows/ci.yml
    git -C "$repo" -c commit.gpgSign=false commit -m "Configure integration CI"
fi

if "$validate"; then
    make -C "$repo" hook test preflight
fi

for branch in "${branches[@]:1}"; do
    git -C "$repo" merge-base --is-ancestor evanpurkhiser/core "$branch"
done

for branch in "${branches[@]:1:${#branches[@]}-2}"; do
    git -C "$repo" merge-base --is-ancestor "$branch" "$integration"
done

if "$push"; then
    declare -a leases=()
    declare -a refspecs=()

    for branch in "${branches[@]}"; do
        old=$(git -C "$repo" ls-remote --heads "$remote" "refs/heads/$branch" |
            awk '{ print $1 }')
        leases+=("--force-with-lease=refs/heads/$branch:$old")
        refspecs+=("refs/heads/$branch:refs/heads/$branch")
    done

    git -C "$repo" push "$remote" "${leases[@]}" "${refspecs[@]}"
fi
