# Design docs

These are the living design docs. Each one describes what the code on `main` does today, and cites the files it is describing. When a PR changes behaviour that a doc describes, that PR updates the doc.

| File | What it covers |
|---|---|
| [architecture.md](architecture.md) | Repo layout, the modules of the Vita client and what each owns, every thread that exists during a stream (with priorities), and the session flow from launch to teardown. |
| [streaming.md](streaming.md) | The streaming pipeline in detail: video and audio delivery, decode, loss handling, and the recovery and reconnect paths. |
| [psn.md](psn.md) | PSN internet play: sign-in and tokens, session setup and hole punching, and how it differs from a LAN connect. |
| `ui-mocks/` | HTML mocks for the XMB redesign (issue #271), including the Appearance settings group with Background, Theme (Ocean default, Ember, Orchid, Moss, Graphite) and Background Blur (#375, #348), and the splash (`splash.html`, #366 and #398). Added by PR #273. Also `input-test.html` and `screens/input-test-*.png`: the debug Input test panel (#458; its combo is L + R + Triangle since #474). `screens/mic-2f-*` to `mic-2p-*`: the microphone UI (#474), approved by the CEO on 2026-10-30: the Controls rows (2f, 2g: Microphone and Display mic button on-screen, the latter dimmed while the mic is off), the stacked stream-start hints (2h, 2i: with and without the stats panel), the bottom-centre button at rest and after a tap (2j to 2m) and the top-left corner icon used when the button is off (2n to 2p). |

Where other things live:

- How-to guides (building, installing, using the app) are on the GitHub wiki: https://github.com/mauricio-gg/vitaki-vitarps5/wiki
- Idea and investigation write-ups go in GitHub Discussions.
- Work is tracked only in GitHub issues. There are no TODO or progress files in the repo.
