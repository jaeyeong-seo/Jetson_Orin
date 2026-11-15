# Jetson Orin v2 — Vortex 목표 사양

상태: 요구사항 정리. 하드웨어/시뮬레이터 구현 및 실행 설정 적용 전.

최신 기준 Vortex 소스를 바탕으로 아래 구성을 갖는 모델을 만든다.
버전명은 **v2**, 작업 브랜치는 `jetson_orin_v2`이다.
기준 소스는 `jaeyeong-seo/Jetson_Orin`의 `main`,
commit `7f90ab6e99fde3e01580da5202b8ccdf202f25a6`이다.

## 1. 전체 계층

| 계층 | 구성 |
| --- | --- |
| Cluster | 전체 1개 |
| Socket | Cluster 안에 1개, 전체 1개 |
| Core | Socket 안에 2개, 전체 2개 |
| Compute Slice | Core당 4개, 전체 8개 |

```text
GPU
└── Cluster 0
    └── Socket 0
        ├── Core 0
        │   ├── Compute Slice 0–3
        │   └── LMEM 32 KB / 32 banks
        ├── Core 1
        │   ├── Compute Slice 0–3
        │   └── LMEM 32 KB / 32 banks
        ├── L1 D-Cache 0: 160 KiB 권장안 → Core 0 전용
        ├── L1 D-Cache 1: 160 KiB 권장안 → Core 1 전용
        ├── L1 I-Cache 0: 128 KB → Core 0 전용
        └── L1 I-Cache 1: 128 KB → Core 1 전용

L2: 비활성화
L3: 전체 공유 4 MB
외부 메모리: LPDDR DRAM 64 GB
```

## 2. Compute Slice 내부 구성

| 구성 요소 | Slice당 | Core당 합계 | 전체 합계 |
| --- | --- | --- | --- |
| FP32 CUDA lanes에 대응하는 FP32 연산 lanes | 32 lanes | 128 lanes | 256 lanes |
| INT32 ALU | 16 lanes | 64 lanes | 128 lanes |
| Tensor Core | 1개 | 4개 | 8개 |
| SFU | 기존 Vortex 구성 유지 | 각 Slice에 포함 | 각 Slice에 포함 |
| LSU | 8 lanes | 32 lanes | 64 lanes |
| Register File 용량 | 64 KB | 256 KB | 512 KB |

FP32 항목은 요청한 연산 자원의 명칭이며, CUDA 소프트웨어 호환성에 대한
별도 요구사항을 추가하지 않는다.

각 Compute Slice의 Register File 목표:

| 항목 | 목표값 |
| --- | --- |
| 용량 | 64 KB |
| Bank 수 | 2 banks |
| 포트 구성 | 1R1W |
| Access 폭 | 1024 bits = 128 B |

2 bank가 용량을 균등 분할하면 bank당 32 KB이다.
1024-bit access 폭이 bank당 폭인지 RF 전체 폭인지, 1R1W의 적용 단위는
구현 전에 확정한다. 현재 문서는 요청값을 그대로 기록한다.

SFU는 각 Compute Slice에 포함하고 기존 기능과 세부 구성을 유지한다.
구체적인 SFU lane 수는 새로 지정하지 않는다. TFU는 비활성화한다.

## 3. Core별 LMEM

| 항목 | 목표값 |
| --- | --- |
| 배치 | Core당 1개 |
| Core당 용량 | 32 KB |
| 전체 용량 | 64 KB |
| Core당 Bank 수 | 32 banks |

현재 기준 소스에는 `VX_CFG_LMEM_NUM_BANKS` 설정 항목이 있으므로
32 bank를 목표값으로 기록한다. 설정 적용 및 조합 검증은 구현 단계에서 진행한다.

LMEM은 Core 내부의 software-managed shared-memory scratchpad로 취급한다.
같은 Core의 Compute Slice들이 접근하며, 소프트웨어가 CTA별 영역과 동기화를
관리한다. OpenCL의 local memory / CUDA의 shared memory에 대응하는 용도이고,
Tensor Core 전용 메모리는 아니다.

WGMMA를 비활성화하므로 일반 WMMA의 A/B 피연산자는 레지스터에서 읽는다.
LMEM에 둔 타일은 LSU를 통한 load, 필요하면 Matrix Load를 거쳐 레지스터에
가져올 수 있다. DXA는 global memory에서 LMEM으로 데이터를 복사하는 데 쓸 수 있다.

## 4. Socket의 L1 Cache

캐시는 Socket에 두되, 각 인스턴스를 Core 하나에 전용으로 매핑한다.

| 항목 | L1 D-Cache | L1 I-Cache |
| --- | --- | --- |
| Socket당 개수 | 2개 | 2개 |
| Core 매핑 | Core당 1개 전용 | Core당 1개 전용 |
| 인스턴스당 용량 | 160 KiB 권장안 | 128 KB |
| 전체 용량 | 320 KiB 권장안 | 256 KB |
| Cache line | 128 B | 미지정 |
| Cache sector | 32 B | 미지정 |
| Bank 수 | 2 banks | 미지정 |
| Way 수 | 5-way 권장안 | 미지정 |
| 교체 정책 | 기존 기본값 FIFO 유지 권장 | 기존 기본값 유지 |

L1 D-Cache는 line당 4개 sector를 갖는다.

### 4.1 D-Cache + LMEM 합계 192 KiB

192 KB 조건은 기존 Core별 배치에 맞춰 **Core당 데이터 용량 합계 192 KiB**로
해석한다. 이 계산에서 KB는 1024 B 기준이다. LMEM 32 KiB는 유지한다.
따라서 D-Cache는 기존 150 KiB 목표에서 160 KiB로 늘리는 안을 권장한다.

| 항목 | Core당 권장안 |
| --- | --- |
| L1 D-Cache 데이터 용량 | 160 KiB = 163,840 B |
| LMEM 데이터 용량 | 32 KiB = 32,768 B |
| 합계 | 192 KiB = 196,608 B |
| D-Cache bank 수 | 2 banks |
| D-Cache bank당 용량 | 80 KiB |
| D-Cache associativity | 5-way |
| D-Cache bank당 set 수 | 128 sets |
| D-Cache 전체 line 수 | 1,280 lines |
| D-Cache 전체 sector 수 | 5,120 sectors |

`2 banks × 128 sets/bank × 5 ways × 128 B = 160 KiB`이다.
Vortex는 비2거듭제곱 way 수를 지원하며, bank 수와 bank당 set 수는
2의 거듭제곱이어야 한다. 이 안은 기존 캐시 구조의 용량 제약을 만족한다.
RTL 구조 변경 없이 관련 설정값으로 표현할 수 있는 안이며, 빌드/타이밍/성능
검증은 수행하지 않았다. D-Cache 용량은 기존 목표보다 10 KiB 늘어난다.

두 Core 전체로는 D-Cache 320 KiB + LMEM 64 KiB = 384 KiB이다.
Tag, valid bit 등의 관리 정보는 위 데이터 용량 합계에 포함하지 않는다.

현재 Vortex에서 D-Cache와 LMEM은 별도 저장소이다. 이 권장안은 두 데이터 용량의
합계를 맞추며, 하나의 물리 SRAM을 동적으로 나누는 unified L1/shared-memory
구조를 새로 구현하는 요구사항은 포함하지 않는다.

## 5. L2 / L3 Cache

| 항목 | L2 Cache | L3 Cache |
| --- | --- | --- |
| 사용 여부 | 비활성화, bypass | 활성화 |
| 용량 | 해당 없음 | 전체 공유 4 MB |
| Cache line | 해당 없음 | 128 B |
| Cache sector | 해당 없음 | 32 B |

L3는 line당 4개 sector를 갖는다.
일반 데이터 경로의 목표 계층은 L1 D-Cache → L2 bypass → L3 → LPDDR DRAM이다.

## 6. 외부 메모리

| 항목 | 목표값 |
| --- | --- |
| 종류 | LPDDR DRAM |
| 모듈 용량 | 64 GB |
| 전체 메모리 인터페이스 폭 | 256 bits |
| 전체 Peak bandwidth | 204.8 GB/s |

대역폭은 전체 메모리 인터페이스의 이론 최대값이며 실측 지속 대역폭이 아니다.
LPDDR 세대, 채널 분할 및 메모리 타이밍은 아직 지정하지 않는다.

## 7. ISA 및 확장 기능

| 항목 | 목표 |
| --- | --- |
| XLEN | 32 |
| FP32 / F 확장 | 활성화 |
| FP64 / D 확장 | 비활성화 |
| Tensor Core | 활성화 |
| DXA / TEX / RTU | 활성화 |
| RASTER / OM (ROP) | 활성화 |
| Zfhmin | 활성화. FP16 load/store와 형식 변환 |
| VM | 활성화. XLEN=32의 기본 페이지 테이블 형식은 Sv32 |
| Atomic / A 확장 | 비활성화 |
| Zacas | 비활성화 |
| TFU | 비활성화 |
| Tensor 2:4 sparsity | 활성화 |
| Tensor DSM | 활성화 |
| WGMMA | 비활성화 |
| Wide Load/Store | 활성화 권장안. 아직 사용자 선택 미확정 |
| Matrix Load/Store | 활성화 권장안. 아직 사용자 선택 미확정 |

SFU는 유지한다. 별도 지시가 없는 기능은 기준 Vortex의 기본값을 따른다.

### 7.1 Tensor 연산 형식

현재 Vortex가 지원하는 Tensor 연산 형식을 모두 활성화하는 것을 목표로 한다.

- Float: FP32, TF32, FP16/BF16, FP8 E4M3 / BF8 E5M2.
- Integer: I32, I8/U8, I4/U4.
- Block-scaled: MXFP8/MXBF8, MXFP4, NVFP4.
- 형식별 설정: `TCU_TF32`, `TCU_FP16`, `TCU_FP8`, `TCU_FP4`, `TCU_INT8`,
  `TCU_INT4`, `TCU_MX`, `TCU_MXFP4`, `TCU_NVFP4`의 `ENABLE`을 모두 활성화 대상에 포함한다.

FP4 datapath는 MXFP4/NVFP4 지원에 포함된다. Float/MX 계열은 FP32,
integer 계열은 INT32로 누산/출력한다. 모든 형식 활성화가 임의의 입력/출력
형식 조합을 허용한다는 뜻은 아니다.

2:4 sparsity와 MX 형식을 위해 Tensor metadata 경로가 필요하지만,
그 경로가 WGMMA 활성화를 요구하지는 않는다.

### 7.2 Wide / Matrix Load·Store 권장 이유

일반 GPU에서 넓은 메모리 접근과 행렬 fragment 전송은 유용하다.
현재 Vortex의 기본값은 두 확장 모두 비활성화이며, v2에서는 둘 다 활성화를 권장한다.

- Wide: 연속한 FP32 word 2개/4개를 묶어 64/128-bit 접근을 표현한다.
- Matrix: WMMA가 읽는 레지스터 배치로 행렬 fragment를 load/store한다.
  WGMMA를 끄더라도 사용할 수 있다.

Vortex에서는 두 명령 계열을 여러 scalar load/store micro-op으로 확장한다.
따라서 켜는 것만으로 LSU의 물리 접근 폭이나 대역폭이 64/128-bit로 증가하지 않는다.
컴파일러/커널이 해당 명령을 사용해야 효과가 있다. Wide는 F 확장과 FLEN=32가
필요하며, 현재의 XLEN=32, FP32 목표와 부합한다.

## 8. 아직 확정하지 않은 세부 사항

- RF access 폭 및 1R1W 포트의 bank별/전체 적용 단위.
- Warp 수, warp당 thread 수 및 동작 클럭.
- Tensor Core의 tile 크기 및 형식별 처리량.
- D-Cache 160 KiB / 5-way 권장안과 Wide/Matrix Load·Store 권장안의 채택 여부.
- L1 I-Cache의 line/sector 크기.
- 별도 지시가 없는 cache associativity 및 메모리 타이밍.
- XLEN=32에서 외부 DRAM 64 GB 전체를 사용하는 물리 주소/VM 지원 방식.
  기준 설정의 메모리 주소 폭은 32 bits이며, VM 활성화만으로 64 GB 전체 주소
  지원이 생기지는 않는다.

위 항목의 값을 임의로 정하지 않는다.
KB/MB/GB는 요구사항의 표기를 유지하며, 실제 byte 단위의 환산 기준은
설정을 적용할 때 명시한다.

## 9. 작업 공간

| 위치 | 용도 |
| --- | --- |
| `README.md` | v2 목표 사양 |
| `configs/` | 추후 작성할 설정 프로필과 실행 조건 |
| `results/` | 로컬 로그 및 결과. Git에서 제외 |

이 문서는 목표 사양이며 실행 가능한 설정 프로필이 아니다.
현재 단계에서는 소스, `VX_config.toml`, RTL, SimX 및 기존 프로필을 변경하지 않는다.
빌드와 시뮬레이션은 수행하지 않는다.
