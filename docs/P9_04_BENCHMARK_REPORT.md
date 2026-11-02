# P9-04 — Trade-History Growth Optimization

## Environment
- Build: CMake Release
- Benchmark: matching_allocation_profile
- Workload: 10,000 MARKET orders producing 10,000 trades
- Repetitions: 7 per strategy
- Execution order: Alternated between runs

## Results

| Strategy | Median elapsed (ms) | Range (ms) | Growth events |
|---|---:|---:|---:|
| Exact-capacity baseline | 259.361 | 87.044–277.833 | 10,000 |
| Geometric growth | 10.631 | 10.262–12.142 | 15 |
| Preallocated | 10.929 | 9.494–11.395 | 0 |

## Findings

- Geometric growth achieved a 24.4x median elapsed-time speedup
  over exact-capacity reservation for this workload.
- Median elapsed time decreased by approximately 95.9%.
- Capacity growth events decreased from 10,000 to 15.
- Geometric growth ended with capacity 16,384 for 10,000 trades.
- All six Debug and six Release test suites passed.

## Limitations

- Results are specific to this benchmark and test environment.
- Exact-capacity baseline measurements showed substantial variation.
- Results do not establish per-order tail latency or performance
  under concurrent production workloads.
- Geometric growth retains additional unused vector capacity.
