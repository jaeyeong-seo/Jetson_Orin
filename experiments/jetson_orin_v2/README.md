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
        ├── L1 D-Cache 0: 150 KB → Core 0 전용
        ├── L1 D-Cache 1: 150 KB → Core 1 전용
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
구체적인 SFU lane 수는 새로 지정하지 않는다.

## 3. Core별 LMEM

| 항목 | 목표값 |
| --- | --- |
| 배치 | Core당 1개 |
| Core당 용량 | 32 KB |
| 전체 용량 | 64 KB |
| Core당 Bank 수 | 32 banks |

현재 기준 소스에는 `VX_CFG_LMEM_NUM_BANKS` 설정 항목이 있으므로
32 bank를 목표값으로 기록한다. 설정 적용 및 조합 검증은 구현 단계에서 진행한다.

## 4. Socket의 L1 Cache

캐시는 Socket에 두되, 각 인스턴스를 Core 하나에 전용으로 매핑한다.

| 항목 | L1 D-Cache | L1 I-Cache |
| --- | --- | --- |
| Socket당 개수 | 2개 | 2개 |
| Core 매핑 | Core당 1개 전용 | Core당 1개 전용 |
| 인스턴스당 용량 | 150 KB | 128 KB |
| 전체 용량 | 300 KB | 256 KB |
| Cache line | 128 B | 미지정 |
| Cache sector | 32 B | 미지정 |

L1 D-Cache는 line당 4개 sector를 갖는다.
150 KB는 요청한 정확한 목표 용량으로 유지하며, 다른 용량으로 반올림하지 않는다.

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

## 7. 활성화할 확장 기능

- Tensor Core
- DXA
- TEX
- RTU

SFU도 유지한다. “등”에 포함될 수 있는 그 밖의 확장은 별도로 지정되기 전까지
새 활성화 요구사항으로 추가하지 않는다.

## 8. 아직 확정하지 않은 세부 사항

- RF access 폭 및 1R1W 포트의 bank별/전체 적용 단위.
- Warp 수, warp당 thread 수, XLEN 및 동작 클럭.
- Tensor Core의 연산 형식, tile 크기 및 처리량.
- L1 I-Cache의 line/sector 크기.
- 별도 지시가 없는 cache associativity 및 메모리 타이밍.

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
