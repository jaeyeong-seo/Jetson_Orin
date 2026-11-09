# Jetson Orin experiment workspace

This directory organizes experiments on the `jetson_orin` branch. Creating it
does not change Vortex's default parameters, FP32 units, controllers, RTL or
SimX model.

| Location | Purpose |
| --- | --- |
| `configs/` | Versioned parameter profiles and experiment descriptions |
| `results/` | Local logs, measurements and waveforms; ignored by Git |
| `<repository>/build_jetson_orin/` | Separate generated build tree; ignored by Git |

## Changing the model

Parameter defaults live in `VX_config.toml`; supported experiment-specific
overrides are passed through `CONFIGS` using the existing Vortex build flow.
Keep each experiment's settings and relevant source commit in `configs/`.
No parameter profile is selected automatically.

Implement architecture changes in the existing source tree on this branch,
rather than copying Vortex into this directory:

- FP32/FPU RTL: `hw/rtl/fpu/`; corresponding model: `sim/simx/fpu_unit.*`.
- Core integration and controllers: `hw/rtl/core/`; corresponding model:
  `sim/simx/core.*` and the other affected simulation components.
- Configuration generation and layer boundaries: see `AGENTS.md`.

Keep RTL and the SimX timing model consistent when behavior or timing changes.
Run `configure` again from the dedicated build directory after source or
configuration updates. Use identical settings for the kernel, runtime and
backend builds that participate in an experiment.

## Editing and execution

Edit and push the `jetson_orin` branch from the cloud checkout. Fetch that branch
on the simulation server using the user's PowerShell SSH session. Build and run
there only when requested; this workspace does not start anything automatically.
Do not merge into `main` unless explicitly requested.

While simulation remains unauthorized, use `[skip ci]` in setup-only commit
messages so pushing this branch does not launch the inherited simulation CI.
No builds or simulations have been validated for this workspace yet.
