# ECE 434 Project 1

Multiprocess array analysis using `fork`, `pipe`, `poll`, and `waitpid`.

## Files
- `project1.c` — main source file
- `Makefile` — compile instructions
- `input.txt` — generated input array
- `output.txt` — process log and results
- `trace.csv` — per-process timing data
- `report.pdf` — design, experiments, observations

## Compile
```
make
```

## Run
```
./project1 <L> <H> <PN> [branch]
```

### Arguments
| Arg | Description |
|-----|-------------|
| `L` | Total number of integers (≥ 12000) |
| `H` | Number of hidden key locations to print |
| `PN` | Maximum process count (1–50) |
| `branch` | Branching factor: 2, 3, or 5 (default 2) |

### Example
```
./project1 12000 20 7 2
```

## Clean
```
make clean
```
