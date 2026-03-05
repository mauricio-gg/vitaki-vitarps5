# Building from Source

VitaRPS5 uses a Docker-based build system for consistent, reproducible builds.

## Prerequisites

- Docker installed and running
- Git

## Build Commands

```bash
# Clone the repository
git clone https://github.com/mauricio-gg/vitaki-vitarps5.git
cd vitaki-vitarps5

# Release build (recommended for production)
./tools/build.sh

# Testing build (enables logging output for debugging)
./tools/build.sh --env testing

# Debug build (symbols + verbose logging)
./tools/build.sh debug

# Development shell (for experimentation)
./tools/build.sh shell

# Run test suite
./tools/build.sh test

# Deploy to Vita via FTP
./tools/build.sh deploy <vita_ip>
```

## Build Output

The VPK file will be created in:
- `./build/vitaki-fork.vpk` (main build output)
- `./VitakiForkv0.1.XXX.vpk` (versioned copy in project root)

## Environment Profiles

The build script auto-loads `.env.prod` (default) or any profile you pass via `--env`:
- `./tools/build.sh --env testing` — verbose developer builds with logging enabled
- `./tools/build.sh --env prod` — production-ready builds (logging disabled)

See `docs/ai/LOGGING.md` for the variables each profile controls.

## Testing Notes

- `./tools/build.sh test` only compiles the test executable and does **not** build a `.vpk`
- Use `./tools/build.sh` or `./tools/build.sh debug` when validating streaming behavior on hardware
- Always use `./tools/build.sh --env testing` for debugging — production builds have logging disabled

**Important:** Always use `./tools/build.sh` — never call Docker manually. The script ensures the correct environment and handles versioning automatically.

## Runtime Logs

Logs are written to `ux0:data/vita-chiaki/vitarps5.log` on the Vita. Pull this file to share debug output.

When comparing builds, check these log markers:
- `Bitrate policy: preset_default (...)`
- `Recovery profile: stable_default`
- `Video gap profile: stable_default (hold_ms=24 force_span=12)`

See `docs/ai/STABILITY_AB_TESTING_GUIDE.md` for a full A/B testing procedure.

## Legacy Build System

For compatibility with the original Chiaki build system, you can build using the root CMakeLists.txt:

**Prerequisites:**
- VitaSDK installed and configured
- All Chiaki dependencies installed

```bash
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake \
         -DCHIAKI_ENABLE_VITA=ON \
         -DCHIAKI_ENABLE_GUI=OFF \
         -DCHIAKI_ENABLE_CLI=OFF
make
```

**Note:** The Docker build system is recommended as it handles all dependencies automatically.

## Creating Releases

Releases use a GitHub Actions workflow and can only be created from the `main` branch.

1. Go to **Actions** > **Create Release**
2. Click **Run workflow**
3. Choose version bump type:
   - **patch** (0.0.X) — Bug fixes, small changes
   - **minor** (0.X.0) — New features, non-breaking changes
   - **major** (X.0.0) — Breaking changes, major updates
4. Click **Run workflow**

The workflow automatically calculates the version, builds the VPK, generates a changelog, creates a git tag, and publishes a GitHub release with the VPK attached.
