# git-change-management

Use this skill when the task involves repository state, staging, committing, pushing, branch/worktree decisions, or final handoff from a Git-backed harness.

## Inputs

- Git repository containing the harness.
- `12-git-change-management/README.md`
- `.gitignore`
- Changed files and validation evidence.
- `.agent/skills/git-change-management/scripts/git_preflight.sh`

## Contract

- Git inspection is always allowed; Git mutation requires explicit user request in the current turn.
- Before staging, commit, or push, show `git status --short` plus a concise diff summary.
- Stage explicit file paths only.
- Never stage deletions without explicit confirmation.
- Never use destructive cleanup, reset, path checkout, history rewrite, or force-push commands without explicit user request in the current turn.
- Generated build artifacts must stay untracked and ignored.
- Handoff must include repo root, branch, diff summary, risk tier, validation evidence, and rollback note.

## Steps

1. Run `git status --short --branch`.
2. Run `.agent/skills/git-change-management/scripts/git_preflight.sh`.
3. Classify changes by risk: low, medium, high.
4. If the user asks to stage/commit/push, present status and diff summary first, then stage only named paths after confirmation.
5. Report whether the worktree contains unrelated or unknown changes.
6. Include Git state in the final handoff.

## Evidence

Report:
- Repo root.
- Branch and upstream if available.
- Worktree status.
- Diff stat.
- Untracked files.
- Staged paths.
- Tracked generated artifacts verdict.
- Validation commands and exit statuses.
