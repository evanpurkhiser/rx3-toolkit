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
author_name=$(git -C "$repo" config user.name || true)
author_email=$(git -C "$repo" config user.email || true)

author_name=${author_name:-RX3 integration builder}
author_email=${author_email:-integration-builder@users.noreply.github.com}

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

readonly source_features=(
    usb-wifi:feature/usb-wifi
    rx3-ssh:feature/rx3-ssh
    pcm-tcp-stream:feature/pcm-tcp-stream
    framebuffer-video-stream:feature/framebuffer-video-stream
    remote-control:feature/remote-control
    link-export-activate:ref/link-export-activate
    crossfader-curve:feature/crossfader-curve
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
    '+refs/heads/feature/*:refs/remotes/assembly-fork/feature/*' \
    '+refs/heads/ref/link-export-activate:refs/remotes/assembly-fork/ref/link-export-activate'

for source_feature in "${source_features[@]}"; do
    source=${source_feature%%:*}
    feature=${source_feature#*:}
    feature_ref=refs/remotes/assembly-fork/$feature
    source_commit=$(git -C "$repo" rev-parse "$feature_ref^2") || {
        echo "$feature must end in an assembled two-parent merge" >&2
        exit 1
    }
    git -C "$repo" update-ref \
        "refs/remotes/assembly-source/$source" "$source_commit"
done

attributes=$(git -C "$repo" rev-parse --git-path info/attributes)
mkdir -p "$(dirname "$attributes")"
cat > "$attributes" <<'EOF'
.github/workflows/ci.yml merge=union
docs/README.md merge=union
mod/modules/core/manifest.json merge=union
mod/modules/core/runtime/rx3_composition.c merge=union
EOF

assembly_environment=(
    env
    "GIT_AUTHOR_NAME=$author_name"
    "GIT_AUTHOR_EMAIL=$author_email"
    "GIT_COMMITTER_NAME=$author_name"
    "GIT_COMMITTER_EMAIL=$author_email"
    GIT_CONFIG_COUNT=1
    GIT_CONFIG_KEY_0=commit.gpgSign
    GIT_CONFIG_VALUE_0=false
)

"${assembly_environment[@]}" "$assembler" \
    --config "$assembly" --assemble --create --recreate --all

git -C "$repo" switch "$integration"
cp "$repo/.github/integration/ci.yml" "$repo/.github/workflows/ci.yml"

if ! git -C "$repo" diff --quiet -- .github/workflows/ci.yml; then
    git -C "$repo" add .github/workflows/ci.yml
    "${assembly_environment[@]}" git -C "$repo" commit \
        -m "Configure integration CI"
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
