## Benchmarks

The included results were automatically generated using the `run_bench.sh` script, all benchmarks can be found in `bench/`.

All benchmarks were performed on the following system:

- **Timestamp:** `Sat May 30 01:19:49 AM CEST 2026`
- **CPU:** `AMD Ryzen 5 3600X 6-Core Processor`
- **OS:** `Fedora Linux 43 (KDE Plasma Desktop Edition)`
- **Kernel:** `7.0.10-100.fc43.x86_64`
- **Reduct:** `Reduct 4.0.0+88b7fc8`
- **Hyperfine:** `hyperfine 1.20.0`
- **Heaptrack:** `heaptrack 1.5.0`
- **Lua:** `Lua 5.4.8  Copyright (C) 1994-2025 Lua.org, PUC-Rio`
- **LuaJIT:** `LuaJIT 2.1.1767980792 -- Copyright (C) 2005-2026 Mike Pall. https://luajit.org/`
- **Python:** `Python 3.14.4`
- **Janet:** `Janet 1.35.2-meson`

### brainfuck

| Command | Mean [µs] | Min [µs] | Max [µs] | Relative |
|:---|---:|---:|---:|---:|
| `reduct` | 958.5 ± 121.7 | 861.7 | 2342.6 | 1.07 ± 0.25 |
| `lua` | 1111.9 ± 169.0 | 1002.8 | 2176.1 | 1.24 ± 0.30 |
| `luajit (jit)` | 1011.2 ± 174.9 | 892.5 | 2150.2 | 1.13 ± 0.29 |
| `luajit (int)` | 896.2 ± 172.9 | 783.7 | 2282.1 | 1.00 |

##### Memory Usage

| Command | Peak Memory |
|:---|---:|
| `reduct` | 176.53K |
| `lua` | 103.77K |
| `luajit (jit)` | 78.38K |
| `luajit (int)` | 78.30K |

---

### fib35

| Command | Mean [ms] | Min [ms] | Max [ms] | Relative |
|:---|---:|---:|---:|---:|
| `reduct` | 314.7 ± 0.9 | 313.6 | 316.7 | 2.69 ± 0.02 |
| `lua` | 751.9 ± 26.9 | 718.9 | 783.8 | 6.43 ± 0.23 |
| `luajit (jit)` | 116.9 ± 0.9 | 116.0 | 119.1 | 1.00 |
| `luajit (int)` | 469.0 ± 28.8 | 438.5 | 500.4 | 4.01 ± 0.25 |
| `python3` | 1069.7 ± 46.9 | 1039.2 | 1193.0 | 9.15 ± 0.41 |
| `janet` | 1502.2 ± 39.4 | 1444.8 | 1559.0 | 12.85 ± 0.35 |

##### Memory Usage

| Command | Peak Memory |
|:---|---:|
| `reduct` | 109.41K |
| `lua` | 102.45K |
| `luajit (jit)` | 78.38K |
| `luajit (int)` | 78.30K |
| `python3` | 1.82M |
| `janet` | 1.07M |

---

### fib65

| Command | Mean [µs] | Min [µs] | Max [µs] | Relative |
|:---|---:|---:|---:|---:|
| `reduct` | 752.6 ± 140.8 | 639.1 | 1627.7 | 1.00 |
| `lua` | 989.6 ± 174.2 | 873.4 | 2489.6 | 1.31 ± 0.34 |
| `luajit (jit)` | 845.3 ± 169.9 | 718.8 | 1924.7 | 1.12 ± 0.31 |
| `luajit (int)` | 842.2 ± 170.1 | 720.7 | 1934.6 | 1.12 ± 0.31 |
| `python3` | 12150.2 ± 1515.0 | 11277.5 | 22402.0 | 16.14 ± 3.63 |
| `janet` | 3465.3 ± 576.6 | 3127.0 | 6745.0 | 4.60 ± 1.15 |

##### Memory Usage

| Command | Peak Memory |
|:---|---:|
| `reduct` | 108.70K |
| `lua` | 99.38K |
| `luajit (jit)` | 78.30K |
| `luajit (int)` | 78.30K |
| `python3` | 1.82M |
| `janet` | 1.07M |

---

### fizzbuzz

| Command | Mean [µs] | Min [µs] | Max [µs] | Relative |
|:---|---:|---:|---:|---:|
| `reduct` | 857.5 ± 179.7 | 710.1 | 2147.5 | 1.00 |
| `lua` | 6185.8 ± 808.8 | 5792.5 | 11861.4 | 7.21 ± 1.78 |
| `luajit (jit)` | 1584.9 ± 306.7 | 1396.5 | 3241.1 | 1.85 ± 0.53 |
| `luajit (int)` | 1450.9 ± 263.9 | 1282.7 | 3025.6 | 1.69 ± 0.47 |
| `janet` | 9788.2 ± 1019.4 | 9259.7 | 18354.1 | 11.42 ± 2.67 |

##### Memory Usage

| Command | Peak Memory |
|:---|---:|
| `reduct` | 116.50K |
| `lua` | 105.45K |
| `luajit (jit)` | 78.38K |
| `luajit (int)` | 78.30K |
| `janet` | 1.22M |

---

### mandelbrot

| Command | Mean [ms] | Min [ms] | Max [ms] | Relative |
|:---|---:|---:|---:|---:|
| `reduct` | 266.0 ± 2.0 | 263.7 | 270.3 | 14.20 ± 1.82 |
| `lua` | 295.2 ± 9.6 | 284.4 | 307.4 | 15.76 ± 2.08 |
| `luajit (jit)` | 18.7 ± 2.4 | 17.6 | 31.5 | 1.00 |
| `luajit (int)` | 126.8 ± 3.1 | 125.1 | 141.0 | 6.77 ± 0.88 |

##### Memory Usage

| Command | Peak Memory |
|:---|---:|
| `reduct` | 173.59K |
| `lua` | 112.02K |
| `luajit (jit)` | 78.38K |
| `luajit (int)` | 78.30K |

---

