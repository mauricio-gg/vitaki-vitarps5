# PSN OAuth Client Credentials

## What these values are

The `VITARPS5_PSN_OAUTH_CLIENT_ID` and `VITARPS5_PSN_OAUTH_CLIENT_SECRET` values
committed in `.env.prod` and `.env.testing` are the well-known, publicly
reverse-engineered OAuth client credentials for the Sony PS App (PlayStation
mobile application). They were originally identified and published by
[grill2010](https://github.com/grill2010) and are widely used across the
open-source Remote Play ecosystem (Chiaki, Chiaki4deck, and derivatives).

## Why they are intentionally committed

These credentials identify the **client application** to Sony's OAuth endpoint —
they do not authenticate a user and do not grant access to any user's account or
data. The actual user authentication happens via Sony's OAuth device-code flow,
which runs at runtime and is never stored in this repository.

Committing them in both env files ensures that builds succeed out of the box
without requiring contributors to locate or supply values that are freely
available in any Chiaki-derived project. Removing or rotating them locally will
not improve security; it will only break the build.

## What to do if Sony rotates the upstream values

If Sony issues new PS-App client credentials and the old ones stop working,
update `VITARPS5_PSN_OAUTH_CLIENT_ID` and `VITARPS5_PSN_OAUTH_CLIENT_SECRET` in
both `.env.prod` and `.env.testing`. The canonical upstream source is the
[chiaki-ng](https://github.com/streetpea/chiaki-ng) project, which tracks these
values as part of its PSN authentication support.

## Do NOT treat these as secrets

Do not rotate, redact, or scrub these values on the assumption that they are
accidental credential leaks. They are public, widely known values documented
here deliberately.
