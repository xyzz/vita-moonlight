# Contributing

Moonlight Vita targets PS Vita and PSTV. Keep changes focused on the Vita client and
follow the existing code style and project structure.

## Development

Clone the primary repository with its submodules:

```bash
git clone --recurse-submodules https://github.com/xyzz/vita-moonlight.git
cd vita-moonlight
git submodule update --init --recursive
```

See [README.md](README.md) for the supported Linux, WSL and Windows/MSYS2 setup.
Do not commit build directories, local logs, device addresses or other
machine-specific files.

## Commits

Use Conventional Commits for new work:

```text
type(scope): description
```

The scope is optional. The project uses `feat`, `fix`, `perf`, `refactor`,
`build`, `ci`, `docs`, `test`, `chore` and `revert`. Common scopes
include `video`, `audio`, `input`, `network`, `session`, `hosts`, `ui`,
`config`, `vita`, `release`, `deps` and `moonmic`.

Keep one coherent technical change per commit. Tests and documentation that are
required to complete the same change should stay with it. Use `!` or a
`BREAKING CHANGE:` trailer only for an actual incompatible change.

The release helper uses commit subjects to select SemVer bumps: breaking changes
produce a major bump, `feat` produces a minor bump, and other changes produce a
patch bump. Do not change `CHANGELOG.md` or `resources/changeinfo.xml` for
normal development; release preparation keeps them synchronized.

Pull request titles must follow the same Conventional Commit format. This keeps
the release history valid when GitHub uses the pull request title as the squash
commit subject.

## Validation

Run the checks that apply to the change. A normal release build is:

```bash
./makepsv --gxm --release --no-deploy
```

Changes to release automation should also pass:

```bash
python3 -m unittest discover -s tests
```

When hardware behavior changes, test on the relevant PS Vita or PSTV model and
include the host software and stream/decoder path used.

## Pull requests

Explain what changed and why, keep unrelated changes out of the same pull
request, and list the commands and hardware used for validation. Avoid
whitespace-only edits, generated build output and unrelated submodule updates.
