# Contributing

Moonlight Vita runs on PS Vita and PSTV. Keep changes focused and follow the
existing style.

## Development

Follow the [build instructions](README.md#building) for Linux, WSL or
Windows/MSYS2. Update submodules whenever their revisions change.

Keep build directories, logs, host addresses and other local files out of commits.

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

Run the tests that cover your changes. To build a release VPK:

```bash
./makepsv --gxm --release --no-deploy
```

To run the automated tests, including the release checks:

```bash
python3 -m unittest discover -s tests
```

For streaming or input changes, test on a PS Vita or PSTV. Record the host
software and decoder used.

## Pull requests

Explain what changed and why, keep unrelated changes out of the same pull
request, and list the commands and hardware used for validation. Avoid
whitespace-only edits, generated build output and unrelated submodule updates.
