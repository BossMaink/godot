# ValleyIdler engine integration

This fork retains the project's transparent desktop presentation fixes while following upstream Godot. Project renderer selection is separate from this engine release.

## Maintained changes

- Preserve RD screen-copy alpha for transparent targets; opaque targets still force alpha to one.
- Tonemap transparent Forward+ and Mobile buffers with premultiplied-alpha semantics. Give transparent Mobile buffers sufficient alpha precision.
- Preserve the existing Compatibility foliage blend and GodotSharp shutdown fixes in this fork's history.
- Reserve a separate combined texture unit for GLES global parameters. Upstream's stage-local slot overlapped a 21-texture material and corrupted subsequent tree draws; the isolated 80-instance regression now passes.
- Match the GLES particle material UBO binding to the upstream C++ binding enum after removal of the global UBO.
- Use upstream translation generation and compilation unchanged; the local per-language compilation split has been removed.
- Provide an opt-in Windows Vulkan-to-D3D11 shared GPU texture presentation path using DirectComposition. This path imports same-adapter D3D11 textures into Vulkan, synchronizes with keyed mutexes and copies on the GPU. It does not read pixels back to the CPU.
- Reuse D3D11 backbuffer references and Vulkan submission arrays, and omit redundant per-frame Flush before Present.

## Explicit switches

- `GODOT_VULKAN_D3D11_PRESENT=1`: enable the Vulkan GPU presentation bridge. Requires matching adapter LUID, external-memory sharing and keyed-mutex support in addition to Vulkan support. No universal device-support claim or automatic renderer fallback is implied.
- `GODOT_VULKAN_D3D11_TRACE=<output-prefix>`: collect bridge-stage diagnostics, written when the bridge is destroyed. Leave unset in normal operation.
- `GODOT_VULKAN_D3D11_REFERENCE=1`: restore per-frame GetBuffer/Flush for local comparison.
- `GODOT_CAPTURE_LAYERED_WINDOWS=1`: include layered windows in native diagnostic screenshots.

The discarded CPU-readback presentation prototype and its activation switch have been removed. Historical measurements remain in the project evidence only.

D3D12 uses its existing native Windows presentation implementation. Non-Windows platforms keep their existing rendering paths. Device loss, older Vulkan implementations and cross-vendor bridge coverage remain to be validated.

## Verification evidence

The ValleyIdler workspace retains the raw performance runs, native dark/light desktop captures, alpha regressions and assembly/binary identities under:

- `output/render-pipelines-alpha-20261013/`
- `output/vulkan-d3d11-optimization-20261013/`
- `output/engine-release-20261013/` (upstream update, merge decisions, rebuild and release regressions)

The project test entrypoints are `AgentTest/ForwardPlusTransparency/Run.ps1` and `AgentTest/RenderPipelines/Run.ps1`. Performance collection uses the existing shared performance tools; profiled runs are not normal baseline timings. The previous performance comparison is not certification of a new upstream build.
