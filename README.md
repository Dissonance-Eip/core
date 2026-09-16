# Dissonance Core

Native C++ audio core that provides:
- A Node.js addon (`.node`) consumed by the Electron UI
- A CLI executable (`dissonance.core`) for local testing and F3 evidence

## Addon contract

The `.node` addon exposes three async functions:

```js
process(
  inputPath: string,
  options?: { outputPath?: string, perturbation?: number, modes?: string[] }
): Promise<{ ok: boolean, processedPath: string }>

readMetadata(filePath: string): Promise<{
  ok: boolean,
  audio: {
    audioFormat, numChannels, sampleRate, byteRate,
    blockAlign, bitsPerSample, durationSec
  },
  tags: { title, artist, comment, date, genre, software, copyright }
}>

writeTags(filePath: string, tags: {
  title?, artist?, comment?, date?, genre?, software?, copyright?
}): Promise<{ ok: boolean }>
```

### `process`

Runs the full pipeline: `GainStage → WindowedFFTStage → PerturbationStage(s) → MaskingStage`.
One `PerturbationStage` is added per requested mode (they stack in order), and the
`MaskingStage` clamps the injected noise under psychoacoustic (Bark-band) masking
thresholds so it stays below the threshold of hearing.

- `options.outputPath` — if omitted, falls back to `<inputDir>/<stem>-processed.wav`
  (used by the CLI and tests; the UI always passes a temp path).
- `options.perturbation` — `[0, 1]` per-mode noise strength, defaults to
  `ProcessingOptions::perturbation` if omitted.
- `options.modes` — perturbation strategies to stack, e.g.
  `["white_noise", "phase_distortion"]`. Valid modes: `white_noise`,
  `phase_distortion`, `spectral_gate`, `pink_noise`. When empty, falls back to a
  single `white_noise` stage if `perturbation > 0`.

### `readMetadata`

Header-only parse — no audio decode. Returns both the WAV format fields and any
LIST/INFO tags. Used by the UI to populate the editable metadata form.

### `writeTags`

Rewrites the LIST/INFO chunk in place. Other chunks (fmt, data, etc.) are
preserved. The LIST chunk is written *before* the data chunk so the parser
(which breaks after data) can read it back on the next `readMetadata` call.
Empty tag fields are omitted from the output.

## Local builds

From the `core` folder:

### Addon

```bash
npm run build               # build the .node addon (alias: build:addon)
npm run build:addon:dist    # build + rename to dist/dissonance_core-<platform>-<arch>.node
```

Output: `build/Release/dissonance_core.node`

### CLI

```bash
npm run build:cli:debug     # Debug build   → cmake-build/Debug/dissonance.core
npm run build:cli:release   # Release build → cmake-build/Release/dissonance.core
npm run build:local         # addon:dist + cli:debug in one step
```

## Run the CLI

```bash
cmake-build/Debug/dissonance.core info <file.wav>
cmake-build/Debug/dissonance.core process <file.wav> \
  --gain 0.5 \
  --perturbation 0.8 \
  --output out.wav
cmake-build/Debug/dissonance.core fft <file.wav> --full --sort
```

Flags accepted by `process`:

- `--gain <0..1>` — linear amplitude gain applied after FFT filtering
- `--perturbation <0..1>` — per-mode noise strength (default: 0.5)
- `--mode <name>` — perturbation strategy: `white_noise`, `phase_distortion`,
  `spectral_gate`, or `pink_noise`. Repeatable — modes stack in order.
- `--output <path>` — destination path; defaults to `<inputDir>/<stem>-processed.wav`

## Testing

Tests mirror `src/`: unit tests live in `tests/audio/` and `tests/utils/`,
`tests/integration/` runs the whole pipeline on real WAV files, and
`tests/bench/` holds the benchmark, which is not part of the test run. CTest
runs the tests from the repo root, so fixtures resolve against
`test_files/sound.wav`.

C++ unit tests are run via CTest (part of the `cmake-tests` CI job):

```bash
cmake -S . -B cmake-build -DBUILD_CLI=ON -DBUILD_NODE_ADDON=OFF
cmake --build cmake-build
ctest --test-dir cmake-build --output-on-failure
```

A WAV fixture for local addon smoke-testing lives at `test_files/sound.wav`.

## Code quality

```bash
npm run format:cpp          # auto-format all C++ files with clang-format
npm run format:cpp:check    # check only (run by CI)
npm run cppcheck            # run cppcheck static analysis locally
```

**Always run `npm run format:cpp` before pushing** — the CI will fail otherwise.

## Workflows

### `ci.yml` — Core Quality Checks
Runs on every push and PR:
- `cppcheck` — static analysis on `src/`
- `clang-format-check` — enforces `.clang-format` style via `npm run format:cpp:check`
- `cmake-tests` — CMake configure, build, and CTest

### `release-addon.yml` — Addon Release
Triggered on push to `dev` or a version tag:
- `addon-checks` — installs deps, builds addon, smoke-loads `.node`
- `build-addon` — matrix build for Windows / Linux / macOS arm64, produces `dist/*.node`
- `release` — publishes `.node` artifacts to GitHub Releases and dispatches UI sync

## Notes

- Addon build uses `node-gyp` (`binding.gyp`).
- CLI build uses CMake (`CMakeLists.txt`). Set `CMAKE_EXE` if `cmake` is not on PATH.
- Set `CLANG_FORMAT` if `clang-format` is not on PATH.
- Set `CMAKE_BUILD_DIR` to override the default `cmake-build/` output directory.
