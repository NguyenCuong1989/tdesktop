# APΩ Telegram Desktop Development Workflow

This fork treats the modified Telegram Desktop source itself as the agent product. The repository must remain a normal, persistent Git repository with a clean upstream relationship; no temporary copy is canonical.

## Canonical repository topology

- Local source: `C:\Users\pc\Documents\GitHub\tdesktop`
- `origin`: `https://github.com/NguyenCuong1989/tdesktop.git` — writable product fork
- `upstream`: `https://github.com/telegramdesktop/tdesktop.git` — official Telegram Desktop, fetch-only locally
- Product branch: `apo/master`
- Upstream integration branch: `dev` tracking `upstream/dev`

## Upstream update sequence

```powershell
git status --short --branch
git fetch upstream dev --prune
git switch apo/master
git rebase upstream/dev
git submodule sync --recursive
git submodule update --init --recursive
.\Telegram\build\apo_verify.ps1 -RunReferenceTests
git push --force-with-lease origin apo/master
```

Never use `git reset --hard` or delete local work as an update shortcut. If rebase conflicts, resolve and verify before continuing.

## Verification gates

A change is not considered native-verified merely because the Python reference tests pass. The gates are:

1. Git topology and clean working tree.
2. APΩ branch based on current `upstream/dev`.
3. All submodules at superproject-recorded commits.
4. `git diff --check` passes.
5. Reference APΩ tests pass when available.
6. Official Windows build toolchain is present.
7. Configure and compile Telegram Desktop successfully.
8. Launch the produced binary and verify native runtime behavior.
9. Verify raw-behavior traces for lifecycle, inbound update, outbound request, UI event, decision, APO verification, execution, observed result, and state update.

## Windows native build baseline

Follow `docs/building-win.md` from the same upstream revision. At the current upstream revision this requires Visual Studio 2026, Windows SDK `10.0.26100.0`, and the documented `-vcvars_ver=14.44` toolset initialization. Do not replace this with an unrelated local compiler and call the product verified.

The official flow is:

```text
prepare dependencies
→ configure x64 with product api_id/api_hash
→ generate Telegram.slnx
→ build Telegram
→ launch generated Telegram.exe
→ runtime acceptance tests
```

## Claims discipline

- `pytest` passing in `telegram-unified-ecosystem` proves the reference/spec tests only.
- Source hooks existing proves wiring at source level only.
- Only a successful native build proves compile/link compatibility.
- Only an executed binary with observed traces proves runtime integration.
- Only end-to-end Telegram actions with observed server results prove execution integration.

This distinction must be preserved in reports and release notes.
