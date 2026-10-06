# CLAUDE.md — VGA32 Spectrum Analyzer

Operational rules for working in this repository. Project facts live in `@docs/ARCHITECTURE.md`,
`@docs/HARDWARE.md`, `@docs/spike-phase0.md` and the spec/plan under `docs/superpowers/`.

## Toolchain — always

- Always build with ESP-IDF at `~/esp/esp-idf-6.2`, commit `25fe69f946311abdaf9ad56591f25fedbc20ac98`.
  Activate with `. ~/esp/esp-idf-6.2/export.sh` in the same shell command as `idf.py`.
- Never switch IDF version without re-running the phase 0 spike (`tools/spike_capture.py`):
  `components/espsdr_rx` and `third_party/esp-sdr/main/common/rx_recalibration.c` call private
  libphy symbols whose ABI is only validated for that commit.
- Always pass an explicit build dir and sdkconfig: `idf.py -B build/<name> -D SDKCONFIG=build/<name>/sdkconfig ...`.
  `build/app` is the firmware, `build/host` the host tests, `build/rx_smoke`, `build/fabgl_smoke`, `build/spike-*` the probes.
  Never use the default `build/` root as an IDF build dir.
- With `idf.py -C <project>` (smoke tests, esp-sdr stock), `-B` and `-D SDKCONFIG=` must be ABSOLUTE
  (`$PWD/build/...`): relative paths resolve against the project dir and can write into a submodule.
- After changing `sdkconfig.defaults`, delete `build/<name>/sdkconfig` and reconfigure: defaults never
  override an existing sdkconfig.
- Never use `sudo`. If a step seems to need it, stop and ask.

## Third-party code — never edit in place

- `third_party/FabGL` (shaeberling fork, pinned) and `third_party/esp-sdr` (pinned) are git submodules.
- Never commit changes inside a submodule. FabGL fixes go to `patches/fabgl/NNNN-<topic>.patch`:
  1. edit in `third_party/FabGL`, 2. `git -C third_party/FabGL diff > patches/fabgl/NNNN-topic.patch`,
  3. `git -C third_party/FabGL checkout -- .`, 4. `tools/apply_patches.sh`, 5. rebuild.
- Run `tools/apply_patches.sh` after every `git submodule update`.
- esp-sdr is a reference source only: reuse its headers/`rx_recalibration.c` through
  `components/espsdr_rx/CMakeLists.txt`; copy-adapt anything else into `components/espsdr_rx/` keeping
  the GPL attribution header.
- Build esp-sdr stock only out-of-tree (`idf.py -C third_party/esp-sdr -B build/spike-...`) and verify
  `git -C third_party/esp-sdr status --porcelain` is empty afterwards.

## Memory and timing invariants

- Keep the init order in `app_main`: radio (`espsdr_rx_init`) → PS/2 → VGA. Wi-Fi must allocate first,
  the VGA framebuffer takes what is left.
- Never place `.data`/`.bss` above `0x3ffe8000`: the linker guard `sram_guard.ld` enforces it.
- Heap headroom after the VGA framebuffer is ~5 KB (`docs/ARCHITECTURE.md`). Every new static buffer or
  heap allocation costs screen lines: measure heap at each init stage on the board before and after,
  and update the budget table. Never remove the `getViewPortHeight() == UI_HEIGHT` guard in `app_main`.
- Create long-lived tasks with static stacks (`xTaskCreateStaticPinnedToCore`); install drivers before
  `setResolution()` so FabGL gets the remaining heap.
- Never allocate in the capture/DSP/render loops: all buffers are static or allocated at init.
- `espsdr_rx_*` is single-caller: only the SDR task calls it after init.
- `words` returned by `espsdr_rx_capture` is overwritten by the next capture: consume it immediately.
- Core 1 belongs to the FabGL video ISR and the UI task. Never put DSP or capture work on core 1.
- Always call `canvas.waitCompletion()` before writing raw pixels with `getScanline()`, and re-read
  `getScanline(y)` every frame (scrolling swaps row pointers).
- VGA16 pixel packing: high nibble = even pixel.
- Any loop that can run longer than a few ms on core 0 must yield (`vTaskDelay(1)`) for the task watchdog.

## Code conventions

- C11 for components, C++ only where FabGL is used (`main/*.cpp`).
- Code comments in French, technical terms in English. Comment only the non-obvious *why*.
- Pure logic (DSP, controls) lives in components with no ESP-IDF include so it builds on the host.
- No dead code, no unused variables, no new warnings (host tests build with `-Wall -Wextra -Werror`).
- Logging: `ESP_LOGI/E` at init and on errors only; never log per frame or per capture.

## Quality gates — before claiming anything works

1. Host tests: `cmake -S tests/host -B build/host && cmake --build build/host && ctest --test-dir build/host --output-on-failure`.
2. Firmware build: `idf.py -B build/app -D SDKCONFIG=build/app/sdkconfig build` with zero warnings in `main/` and `components/`.
3. If FabGL or `espsdr_rx` changed: rebuild the matching smoke project under `tests/target/`.
4. Hardware claims require a flash + observation by the user. Never state on-device behaviour
   (capture OK, picture stable, keys work) without that evidence; say "built, not yet validated on hardware".
5. New pure logic gets a failing host test first (TDD), then the implementation.

## Hardware loop

- The reference board enumerates as `/dev/ttyACM0` (CH9102). Confirm the port before flashing; only one
  board exists, so never flash from parallel agents.
- Open the serial port with `dtr=False, rts=False` set BEFORE `open()` to avoid resetting the board, and
  always give pyserial a read timeout.
- Reference signal for validation: phone hotspot forced on Wi-Fi channel 6 → hump of about 20 MHz
  centred on 2437 MHz.

## Delegation

- Independent work across disjoint directories (e.g. `components/X` vs `tests/target/Y`) may be
  dispatched to parallel agents. Agents never commit and never flash; the coordinator reviews each
  diff, reruns the quality gates and commits.
- Read-only research across third-party sources goes to an Explore agent.

## Documentation — same session

- After any significant change, update `docs/ARCHITECTURE.md`, `docs/HARDWARE.md`, `README.md` and
  this file in the same session.
- Only `README.md` and `CLAUDE.md` live at the repo root; every other `.md` goes under `docs/`.
- Record hardware measurements (heap after each init stage, capture µs, error counts) in `docs/`.
