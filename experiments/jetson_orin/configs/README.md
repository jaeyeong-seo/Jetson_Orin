# Parameter profiles

No Jetson Orin parameter values have been chosen yet. Vortex's existing
defaults remain unchanged.

For each future profile, record:

- The source commit and the architectural change being evaluated.
- XLEN and the exact supported `CONFIGS` overrides.
- Core, warp/thread, FPU, cache and memory settings relevant to the experiment.
- The backend, workload and arguments used for validation.
- Build commands and the matching result directory.

Use the repository's existing configuration mechanism. Files placed here are
experiment records until they are explicitly connected to a build command;
Vortex does not automatically load this directory.
