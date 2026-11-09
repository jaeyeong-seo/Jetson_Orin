# Jetson Orin experiments on Vortex

Status: branch scaffold; parameter values and additional hierarchy are not yet specified.

## Goal

Develop a configurable Vortex model on the `jetson_orin` branch of
`jaeyeong-seo/Jetson_Orin`, starting from `main` at
`7fe19369587c087f902c3e2bc2add612aa910a95`. Keep the experiments on this
branch; merging into `main` or submitting changes to upstream Vortex is a
separate decision.

## Configuration and architecture

Use Vortex's existing TOML configuration and supported per-build overrides.
Keep named experiment settings reproducible, and use a separate ignored build
directory such as `build_jetson_orin` to avoid mixing build outputs.

Before implementing the model, specify the desired core/cluster shape,
warp/thread counts, cache and memory configuration, and what additional
hardware or configuration hierarchy is required. The branch name alone does
not make Vortex an accurate model of NVIDIA Jetson Orin.

For architecture changes, keep the SimX timing model and RTL consistent.
Follow the repository's configuration-generation and layer-boundary rules.

## Execution workflow

Edit and review code in the cloud checkout, then publish changes to this branch.
The user will fetch the branch and run builds and simulations through their
PowerShell SSH session on `orcas1.cs.ucla.edu`. Re-run `configure` from the
dedicated build directory after updating sources or configuration.

No build or simulation has been run for this scaffold. Until simulation is
explicitly requested, do not launch local or remote simulations, dispatch CI,
or push commits that trigger simulation workflows. Use GitHub's `[skip ci]`
commit marker for setup-only pushes under that restriction.

Server checkout paths, installed toolchains and the model's functional
validation remain to be established. Do not store SSH passwords in the repository.
