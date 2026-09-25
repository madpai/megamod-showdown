---
name: merge-branch
description: Bring another session's branch (a cloud session's claude/..., or a feature branch) into halo-sandbox, prove it really landed, verify and push. Use whenever asked to merge, pull in or integrate a branch.
---

# Merge a branch -- and prove it landed

A past merge reported success while the branch never reached main. So the
last step here is not optional.

1. Save your own work first: `git status --short` must be clean; commit
   (WIP is fine) or stash.
2. Fetch and merge:
   ```sh
   git fetch megamod <branch>
   git merge --no-ff FETCH_HEAD          # or --ff-only when main has not moved
   ```
3. Conflicts:
   - **`hta_android` / `src/app/session.h`** (the session struct): take your
     side's struct, then `python3 scripts/codemod/session_stage1.py`
     regenerates both files with every field from both sides.
   - **`docs/HANDOFF.md` CURRENT TESTING OBJECTIVE**: keep both sides' items.
   - Code in the same function on both sides: keep both behaviours; if they
     truly exclude each other, stop and ask the owner.
4. **Prove it landed**, before anything else:
   ```sh
   git merge-base --is-ancestor FETCH_HEAD HEAD && echo "branch is in HEAD"
   ```
   Also grep for one symbol the branch added. If either check fails, the
   merge did not happen, whatever git said.
5. Run the `verify` skill (with the Trial map), then push
   `git push megamod halo-sandbox:main`, and report the pushed hash.
