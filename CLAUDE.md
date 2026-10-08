# CLAUDE.md

VitaRPS5: a PS Vita Remote Play client for PS5/PS4, a fork of Vitaki (which is built on Chiaki). Hardware limits that matter: 960x544 screen, tight RAM and CPU, rendering through vita2d.

## Read first

Read `docs/design/README.md` before changing anything. It is the register of the living design docs (architecture, streaming, PSN, UI mocks) and says how the app works.

## Hard rule: design docs

Any change to behaviour, structure or a decision covered by a file in `docs/design/` updates that file in the same PR. The Team Lead's review checks this and does not approve a PR that leaves a design doc out of date. Every PR states it on the "Design docs:" line of the PR template.

## Build (Docker only)

Never call VitaSDK by hand. All commands run from the repo root.

| Command | What it does |
|---|---|
| `./tools/build.sh` | Release build. Bumps the version, writes `build/vitaki-fork.vpk` and a versioned `.vpk` copy in the repo root. |
| `./tools/build.sh --env testing` | Loads `.env.testing`. Required for any on-device streaming validation, because it enables runtime logs. |
| `./tools/build.sh debug` | Debug build with symbols. Also bumps the version. |
| `./tools/build.sh test` | Cross-compiles the test suite in `build-test/`. See pitfalls: it does not run anything. |
| `./tools/build.sh format` | Runs clang-format over the sources. |
| `./tools/build.sh shell` | Interactive shell inside the build container. |
| `./tools/build.sh deploy <vita_ip>` | Uploads `build/vitaki-fork.vpk` to the Vita over FTP (port 1337). Build first. |

## Repo map

- `vita/src/`, `vita/include/`: the Vita app (entry point, UI, audio, video, input, host/session logic).
- `lib/`: Chiaki core, treated as upstream. Change it only when necessary and say why in the PR.
- `third-party/`: vendored dependencies.
- `assets/`, `vita/assets/`, `vita/res/`: images, certificates and Vita bundle resources.
- `tools/`: `build.sh` and analysis helpers.
- `docs/design/`: the living design docs.

## Where things live

- How-tos: the GitHub wiki, https://github.com/mauricio-gg/vitaki-vitarps5/wiki
- Ideas and investigation write-ups: GitHub Discussions.
- Work is tracked only in GitHub issues. No TODO files, no progress notes in the repo.
- PRs are merged by the maintainer with admin override, because agents post as the repo owner and a non-author approval is impossible.

## Pitfalls

- `./tools/build.sh test` only cross-compiles for the Vita (ARM). It never runs the tests (#222). Re-run pure-helper assertions natively with `cc` if you need real coverage.
- Testing logs (`*_vitarps5-testing.log`) are seen as binary by BSD grep. Always use `grep -a`, or a missing match proves nothing.
- Vita thread priority: a lower number is a higher priority. Threads in `lib/` default to about 160 and must be promoted explicitly.
- Latency-sensitive code is `vita/src/host.c`, `vita/src/video.c` and `vita/src/audio.c`. Read `docs/design/streaming.md` before changing polling loops, priorities or buffer sizes.
- Run `./tools/build.sh format` before pushing any C change.
- VitaSDK headers exist only inside Docker, so clangd errors about them are false positives.
- The build container runs as the host user (`-u $(id -u):$(id -g)`, already set in `tools/build.sh`). Running it as another uid makes CMake fail with a misleading "pkgRedirects" error that is really a permission denial.
- Every interaction must work by touch and by controller.
- Use the project logging, not raw `printf`.
- No per-frame allocations; reuse textures.
- The project is AGPLv3. Do not add closed-source dependencies.

## graphify

This project has a knowledge graph at graphify-out/ with god nodes, community structure, and cross-file relationships.

Rules:
- For codebase questions, first run `graphify query "<question>"` when graphify-out/graph.json exists. Use `graphify path "<A>" "<B>"` for relationships and `graphify explain "<concept>"` for focused concepts. These return a scoped subgraph, usually much smaller than GRAPH_REPORT.md or raw grep output.
- If graphify-out/wiki/index.md exists, use it for broad navigation instead of raw source browsing.
- Read graphify-out/GRAPH_REPORT.md only for broad architecture review or when query/path/explain do not surface enough context.
- After modifying code, run `graphify update .` to keep the graph current (AST-only, no API cost).
