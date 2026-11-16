# Jetson Orin v2 — SimX experiment

Status: C++/SimX implementation; RTL changes are deferred until the model and performance have been reviewed.
Base Vortex source: `7f90ab6e99fde3e01580da5202b8ccdf202f25a6`.
Profile: `profiles/jetson_orin_v2.json`. On-chip capacities use KiB/MiB.

## Machine

| Item | Configuration |
| --- | --- |
| XLEN / FLEN | 32 / 32 |
| Cluster / Socket / Core | 1 / 1 / 2 |
| Compute slices | 4 per core, 8 total |
| Warp size | 32 threads |
| Resident warps | 8 per slice, 32 per core, 64 total |
| Resident threads | Up to 1,024 per core, 2,048 total |
| CTA slots | Existing Vortex rule: up to 32 per core, further limited by warp and shared-memory usage |
| Clock | Vortex default, 400 MHz |

## Compute slice

| Resource | Per slice | Whole device |
| --- | --- | --- |
| FP32 lanes | 32 | 256 |
| INT32 ALU lanes | 16 | 128 |
| LSU lanes | 8 | 64 |
| Tensor core blocks | 1 | 8 |
| SFU | Existing functions; one block per slice | TFU disabled |
| Logical RF capacity | 64 KiB | 512 KiB |

Each slice has one operand collector and two RF banks. Each bank has 32 KiB,
a 1R1W port configuration and a 1024-bit access width. The existing operand
conflict and commit models are retained.

With 32 integer and 32 FP32 architectural registers per thread:
`8 warps × 32 threads × 64 registers × 4 bytes = 64 KiB` per slice.
The C++ FP storage representation and simulator metadata are not physical RF capacity.

## Shared memory and caches

| Resource | Capacity / placement | Geometry |
| --- | --- | --- |
| LMEM / shared memory | 32 KiB per core | 32 banks |
| L1 D-cache | 160 KiB per core; two instances in the socket | 5 ways, 2 banks, 128 B line, 32 B sector, FIFO |
| L1 I-cache | 128 KiB per core; two instances in the socket | Default 4 ways and derived banks, 64 B line = 64 B sector |
| L2 | Disabled / bypass | No cache allocation |
| L3 | Shared 4 MiB | 128 B line, 32 B sector |

Per core: D-cache 160 KiB + shared memory 32 KiB = 192 KiB of data storage.
The two remain separate stores. Tags and control metadata are additional.
The two cores total 384 KiB of D-cache plus shared-memory data.

A D-cache has 128 sets per bank, 1,280 lines and 5,120 sectors.
An I-cache has 2,048 lines. Its bank count follows the existing fetch-width
rule (four banks at fetch width four).

Data-side memory transactions are 32 B. Instruction fills remain 64 B:
a bounded read adapter splits each fill into two 32 B lower requests and
reassembles responses, including out-of-order completion and backpressure.
Payload storage covers the larger instruction fill; data-side byte offsets
and byte enables still use the 32 B transaction granule.

The common `L1_LINE_SIZE` remains the lower-link granule (32 B for this
profile). Actual I-cache and D-cache line/sector sizes are configured
independently. This keeps the L2 bypass and L3 input word at 32 B.

LMEM is software-managed shared memory, not tensor-only memory. With WGMMA
disabled, WMMA operands come from registers; LSU/matrix loads can stage
shared-memory data into those registers, and DXA can fill shared memory.

## ISA and accelerators

| Enabled | Disabled |
| --- | --- |
| FP32/F, Zfhmin, VM (Sv32) | FP64/D, Atomic/A, Zacas |
| DXA, TEX, RTU, RASTER, OM/ROP | TFU |
| Wide and Matrix load/store | WGMMA |
| Tensor 2:4 sparsity and DSM | |

All supported tensor number-format paths are enabled: FP32, TF32, FP16/BF16,
FP8/BF8, INT32, INT8/UINT8, INT4/UINT4, MXFP8/MXBF8, MXFP4 and NVFP4.
Floating/MX results accumulate to FP32, integer results to INT32.
The initial backend is TFR with `TCU_PE_RATIO=1`; this is a Vortex model
choice, not a calibration to a measured Orin tensor throughput.
DSM enables masking; it is not assigned an artificial cycle speedup.

## DRAM

| Item | Configuration |
| --- | --- |
| Requested physical module | LPDDR, 64 GB |
| Timing backend | Native Ramulator LPDDR5_6400 preset |
| Channels / total interface | 16 × 16 bits = 256 bits |
| Data rate | 6,400 MT/s per pin |
| Theoretical peak | 204.8 GB/s, or 512 B/core clock at 400 MHz |
| Device organization | 16 Gb x16, two ranks per channel |

Ramulator transfers 16 B per native LPDDR5 transaction; a 32 B request
therefore contains two native transactions. The profile uses the existing
preset timings, without measured Orin PHY/controller/NoC latency calibration.
The extension in `third_party/ramulator_ext/lpddr5.cpp` corrects the pinned
device's read/write prerequisite checks, which otherwise test row zero and
never complete accesses to nonzero rows. Follow-up: remove this extension
when the Ramulator pin includes the corrected requested-row checks.
LPDDR5 is supported by the Ramulator backend; `VORTEX_DRAM_MODEL=bank` is
explicitly unsupported for this standard.

The existing RV32 address-space contract is retained. The requested module
capacity does not imply that all 64 GB are addressable by this build; extended
physical addressing remains deferred as instructed.

## Build and validation

From a dedicated build directory, configure with
`../configure --xlen=32 --profile=jetson_orin_v2 --tooldir=<installed-tools>`.
Run configure again after source/configuration updates. Use this same profile
for the kernel, runtime and SimX builds.

Focused checks include:

- `make -C tests/unittest/icache_fill run`: fill addresses, delayed and out-of-order
  responses, original tags, fences and slot reuse.
- `make -C tests/unittest/dram_sim run`: existing DRAM regressions plus LPDDR5
  channel scaling and bandwidth against its physical peak.
- SimX `vecadd` and `sgemm_tcu` with this profile; the CI catalog includes
  `ci/testcases/jetson_orin_v2.yaml` for these cases.

Validated with GCC 14 and the pinned RV32 toolchain:

- Native SimX and runtime builds passed with the v2 profile and all enabled extensions.
- Instruction-fill unit tests passed with default geometry (one transfer) and
  v2 geometry (two transfers), including delayed/reordered responses and fences.
- DRAM regressions passed for HBM2, HBM3, GDDR6 and LPDDR5. The nonzero-row
  regression fails with the original LPDDR5 device and passes with the extension.
- LPDDR5 streaming measured 388.8 B/core cycle (155.52 GB/s), with 16.0×
  channel scaling. This synthetic result is not an application benchmark.
- `vecadd -n128` passed: 876 instructions / 1,035 cycles.
- FP16→FP32 `sgemm_tcu -n16` passed: 752 instructions / 2,420 cycles.
- Software/simulator and SimX multithreading boundary checks passed.

Long performance sweeps and RTL timing parity are separate subsequent stages.

## Workspace

- `configs/`: experiment configuration notes; the executable profile lives in `profiles/`.
- `results/`: ignored local logs and measurements.
- `build_jetson_orin_v2/`: ignored generated build tree.

This profile is an experimental SimX machine. The RTL does not yet implement
its independent instruction-fill geometry or read adapter; RTL integration
and xrt/model-parity validation are required before using it as an RTL configuration.
