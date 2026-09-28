#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0

set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/assemble-integration.sh [options] [CONFIG]

Recreate an integration branch in a temporary worktree from a declarative
configuration. The source repository remains untouched.

Options:
  --remote NAME  Read source branches from NAME (default: origin)
  --push         Replace the configured remote integration branch
  --validate     Run the repository source checks before publishing
  -h, --help     Show this help

Configuration commands:
  target BRANCH
  base BRANCH
  merge BRANCH
  pick BRANCH BASE_COMMIT
EOF
}

remote=origin
push=false
validate=false
config=.github/integration/full-t3u-fader.conf

while (($#)); do
    case "$1" in
        --remote)
            [[ $# -ge 2 ]] || { echo "--remote requires a name" >&2; exit 2; }
            remote=$2
            shift 2
            ;;
        --push)
            push=true
            shift
            ;;
        --validate)
            validate=true
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        -* )
            echo "unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
        *)
            config=$1
            shift
            ;;
    esac
done

repo=$(git rev-parse --show-toplevel)
config=$(realpath "$config")

[[ -f "$config" ]] || { echo "configuration not found: $config" >&2; exit 2; }
git -C "$repo" remote get-url "$remote" >/dev/null

target=
base=
declare -a merge_branches=()
declare -a pick_branches=()
declare -a pick_bases=()

while read -r command first second extra; do
    [[ -n "${command:-}" && "$command" != \#* ]] || continue
    [[ -z "${extra:-}" ]] || { echo "invalid configuration line: $command $first $second $extra" >&2; exit 2; }

    case "$command" in
        target)
            [[ -n "${first:-}" && -z "${second:-}" && -z "$target" ]] || { echo "invalid target command" >&2; exit 2; }
            target=$first
            ;;
        base)
            [[ -n "${first:-}" && -z "${second:-}" && -z "$base" ]] || { echo "invalid base command" >&2; exit 2; }
            base=$first
            ;;
        merge)
            [[ -n "${first:-}" && -z "${second:-}" ]] || { echo "invalid merge command" >&2; exit 2; }
            merge_branches+=("$first")
            ;;
        pick)
            [[ -n "${first:-}" && -n "${second:-}" ]] || { echo "invalid pick command" >&2; exit 2; }
            pick_branches+=("$first")
            pick_bases+=("$second")
            ;;
        *)
            echo "unknown configuration command: $command" >&2
            exit 2
            ;;
    esac
done < "$config"

[[ -n "$target" ]] || { echo "configuration has no target" >&2; exit 2; }
[[ -n "$base" ]] || { echo "configuration has no base" >&2; exit 2; }

declare -a fetch_specs=("+refs/heads/$base:refs/remotes/$remote/$base")

for branch in "${merge_branches[@]}" "${pick_branches[@]}"; do
    fetch_specs+=("+refs/heads/$branch:refs/remotes/$remote/$branch")
done

git -C "$repo" fetch --no-tags "$remote" "${fetch_specs[@]}"

old_target=$(git -C "$repo" ls-remote --heads "$remote" "refs/heads/$target" | awk '{ print $1 }')
worktree=$(mktemp -d "${TMPDIR:-/tmp}/rx3-integration.XXXXXX")

cleanup() {
    git -C "$repo" worktree remove --force "$worktree" >/dev/null 2>&1 || true
    rm -rf "$worktree"
}
trap cleanup EXIT

git -C "$repo" worktree add --detach "$worktree" "$remote/$base"
git -C "$worktree" config user.name "RX3 integration builder"
git -C "$worktree" config user.email "integration-builder@users.noreply.github.com"

for branch in "${merge_branches[@]}"; do
    echo "Merging $remote/$branch"
    git -C "$worktree" -c commit.gpgSign=false merge \
        --no-ff --no-edit "$remote/$branch"
done

integration_core=$(git -C "$worktree" rev-parse HEAD)

for index in "${!pick_branches[@]}"; do
    branch=${pick_branches[$index]}
    first_parent=${pick_bases[$index]}
    source_ref=$remote/$branch

    git -C "$worktree" merge-base --is-ancestor "$first_parent" "$source_ref" || {
        echo "$first_parent is not an ancestor of $source_ref" >&2
        exit 1
    }

    mapfile -t commits < <(
        git -C "$worktree" rev-list --reverse --first-parent \
            "$first_parent..$source_ref"
    )

    ((${#commits[@]})) || {
        echo "$source_ref has no commits after $first_parent" >&2
        exit 1
    }

    echo "Replaying ${#commits[@]} commit(s) from $source_ref"

    for commit in "${commits[@]}"; do
        if ! git -C "$worktree" -c commit.gpgSign=false cherry-pick "$commit"; then
            mapfile -t conflicts < <(
                git -C "$worktree" diff --name-only --diff-filter=U
            )

            if [[ ${#conflicts[@]} -ne 1 || ${conflicts[0]} != .github/workflows/ci.yml ]]; then
                echo "unexpected conflict while replaying $commit" >&2
                exit 1
            fi

            git -C "$worktree" checkout --ours .github/workflows/ci.yml
            git -C "$worktree" add .github/workflows/ci.yml
            GIT_EDITOR=true git -C "$worktree" -c commit.gpgSign=false \
                cherry-pick --continue
        fi

        # Each source branch has already passed its own specialized CI. Keep
        # the integration branch's workflow stable rather than trying to merge
        # independent workflow edits from every module PR.
        git -C "$worktree" restore --source="$integration_core" \
            --staged --worktree .github/workflows/ci.yml

        if ! git -C "$worktree" diff --quiet HEAD -- .github/workflows/ci.yml; then
            git -C "$worktree" -c commit.gpgSign=false commit --amend --no-edit
        fi
    done
done

if "$validate"; then
    make -C "$worktree" hook test preflight
fi

assembled=$(git -C "$worktree" rev-parse HEAD)
echo "Assembled $target at $assembled"

if "$push"; then
    git -C "$worktree" push "$remote" \
        "--force-with-lease=refs/heads/$target:$old_target" \
        "HEAD:refs/heads/$target"
fi
