# STATUS

## Snapshot
- Date: 2026-02-25
- Branch: `feat/psn-internet-remoteplay-mvp`
- Latest commit: `b414194`

## Completed In This Branch
- Switched Vita PSN internet login to browser auth-code exchange flow.
- Added built-in OAuth defaults so users do not need to fill endpoint values in TOML.
- Added QR-assisted login path in Profile screen.
- Moved login instructions out of the Connection Status card into a dedicated lower panel.
- Updated active-login controls:
  - `Start`: show/hide QR
  - `Select`: browser fallback
  - `X`: paste redirect URL or code
  - `Square`: cancel login
- Fixed auth URL truncation risk by expanding URL buffer and adding overflow error handling.

## Key Commits
- `b414194` chore: bump version to 0.1.552 after auth QR updates
- `498cd2b` feat(profile): move PSN hints to lower panel and add QR-assisted login controls
- `acdf2d8` feat(ui): add embedded QR encoder and vita2d QR rendering
- `d47151e` fix(psn-auth): prevent authorization URL truncation
- `4da203c` Default to built-in PSN auth settings for user-facing flow
- `4f9c229` Switch Vita PSN login flow to browser auth-code exchange

## Validation Run
- `./tools/build.sh debug` -> success
- `./tools/build.sh --env testing` -> success
- `./tools/build.sh test` -> success (test binary built)

## Runtime Findings From User Testing
- Vita browser can open some sites (e.g., Google) but Sony auth pages may render blank on Vita.
- Phone flow reaches Sony domains (`account.sony.com` / `my.account.sony.com`).
- User reported being stuck in sign-in/passkey flow on phone in some attempts.
- Important behavior reminder: post-login redirect may target custom scheme
  `com.scee.psxandroid.scecompcall://redirect...` which browsers cannot open directly; copy URL/code and paste via Vita IME (`X`).

## Current Config Check
- Verified user TOML (`/Volumes/Untitled/data/vita-chiaki/chiaki.toml`) has no `psn_oauth_*` endpoint overrides.
- App therefore uses built-in auth endpoints and parameters from `vita/src/psn_auth.c`.

## Open Follow-Ups
- Clarify in-app instruction text further for non-technical users (shorter, explicit “copy code” wording).
- Add optional on-screen “copy only `code=` value” example.
- If sign-in stalls persist, test desktop-browser-first flow and capture exact final URL/error text to confirm whether issue is browser/session-specific vs. auth policy change.
