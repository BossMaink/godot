Ambient Lighting — native Godot C++ module

AmbientCubeField stores 48 nonnegative LINEAR RGB coefficients: eight corners,
corner index x + 2*y + 4*z, six directions +X,-X,+Y,-Y,+Z,-Z. sample() accepts
normalized room position and a room-local normal. This is authored irradiance,
not an automatic GI bake, six lights, or a cubemap image.

AmbientCubeVolume3D publishes into its existing World3D/scenario. bounds selects
the influence volume; sampling_bounds selects the probe grid (zero size uses
bounds). Position clamps to the grid within the selected influence volume.
Rotation, translation and positive uniform scale are supported. receiver_mask
uses existing mesh instance rendering layers. Overlaps select descending priority,
then runtime owner order; edge_blend fades to the existing ambient, not another
room. Adjacent physical rooms need explicit disjoint zones/masks; there is no
automatic portal topology or cross-wall interpolation. Limit: 32 active volumes
per world; excess is an explicit error and fallback, never silent truncation.

The scene thread submits immutable snapshots through the existing render command
queue, ordered with Environment and light updates. Only the render thread applies
snapshots and creates, updates or releases GPU buffers. A moved mesh samples its new position without
material replacement or a probe rebuild. Moving geometry does not automatically
change authored bounce coefficients. field/blend_field/blend provide day/night
inputs from the existing clock; the module does not own time or save games.

Mobile evaluates Cube before material IRRADIANCE. Lightmaps and existing reflection
probe behavior retain their precedence. Unshaded and ambient-light-disabled
materials retain their semantics. Normal-mapped materials use final shading
normals; other materials preserve full-precision fragment normals.

Mobile SSAO uses the actual selected Environment, including camera overrides.
Opt in with rendering/renderer/mobile_ssao=true and Environment.ssao_enabled=true.
Radius, intensity, power, detail, horizon, sharpness and direct-light influence
are supported by this module's algorithm. AO channel affect is not supported.
Material AO and screen AO combine by minimum for indirect light; direct-light
influences are separate. Emission and final color/alpha are not globally darkened.

Passes: single-sample full-size depth, reconstructed geometric normals, half-size
eight-direction/four-step AO, depth-aware spatial filter and full-size output,
then normal Mobile forward shading. Main-view MSAA remains unchanged. Depth
coverage differs from MSAA at thin edges. Screen/transparent/unshaded display
surfaces are excluded as occluders. Each viewport owns its buffers. No temporal
history, off-screen occlusion, deep-cabinet solution, transparent occlusion, or
multiview SSAO is claimed. Forward+ and Compatibility algorithms are unchanged.

This module requires the accompanying small core patch. Disable compilation with
module_ambient_lighting_enabled=no (keep patched source package present). Build
matching native binaries, GodotSharp bindings, and export templates together.

Project tools: Tools/EnginePatches/BuildAmbientLighting.ps1 and
BuildAmbientLightingBindings.ps1; tests: AgentTest/AmbientLighting/Run.ps1.
Production defaults remain inactive pending complete visual/performance acceptance.
