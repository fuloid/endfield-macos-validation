# D3DMetal and Metal HUD verification

D3DMetal mode uses only Apple's D3D11 and DXGI PE modules, one shared Unix
library and the framework/resources from GPTK 4.0b3. It passes `-force-d3d11`
for direct launch. No D3D12, NVAPI or MetalFX modules are installed. DXMT
remains the default.

The macOS app build resolves every D3DMetal framework import with `dlopen`
and runs a harmless D3D11 device/swapchain fixture under DXMT and D3DMetal.
The fixture presents 240 real frames; it contains no game or account code.
A clean device refusal on Apple's paravirtual CI GPU is reported as a
limitation. Crashes, missing imports and presentation errors fail the build.
This is not Endfield gameplay or performance validation.

HUD and log preferences are saved separately. Apple's HUD must be enabled
for its logging, so choosing a log mode also enables the HUD for that launch.
Off disables recording. Every frame preserves all actual HUD log records;
it never synthesizes a 60 Hz stream. Every second retains the last received
record per process and source timestamp second, including the last partial
second at shutdown. No records means unavailable statistics, not zero FPS.

The native collector owns `log stream --style ndjson --level debug` and
terminates/reaps it on exit. It accepts only this user's processes running
this engine's Wine loader or mapping its ntdll (device/inode identity), and
checks an exact WINEPREFIX when Wine has not erased it during process naming.
Like CPU capture, attribution relies on the one-prefix-per-engine design
when Wine erases the environment. Process start times prevent PID reuse from
inheriting an earlier attribution. Logs share the configured Wine log size
cap and are trimmed by the launch supervisor every five seconds.

CI reports whether real attributed HUD records were emitted. macOS version,
driver, log permissions and virtual GPU restrictions can prevent emission;
the release notes must retain that limitation when no records are available.
