# Design docs

These are the living design docs. Each one describes what the code on `main` does today, and cites the files it is describing. When a PR changes behaviour that a doc describes, that PR updates the doc.

| File | What it covers |
|---|---|
| [architecture.md](architecture.md) | Repo layout, the modules of the Vita client and what each owns, every thread that exists during a stream (with priorities), and the session flow from launch to teardown. |
| [streaming.md](streaming.md) | The streaming pipeline in detail: video and audio delivery, decode, loss handling, and the recovery and reconnect paths. |
| [psn.md](psn.md) | PSN internet play: sign-in and tokens, session setup and hole punching, and how it differs from a LAN connect. |
| `ui-mocks/` | HTML mocks for the XMB redesign (issue #271). Added by PR #273. |

Where other things live:

- How-to guides (building, installing, using the app) are on the GitHub wiki: https://github.com/mauricio-gg/vitaki-vitarps5/wiki
- Idea and investigation write-ups go in GitHub Discussions.
- Work is tracked only in GitHub issues. There are no TODO or progress files in the repo.
