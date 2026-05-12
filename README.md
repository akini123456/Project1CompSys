# ECE 434 Projects 1 and 2

Multiprocess array analysis using `fork`, `pipe`, `poll`, and `waitpid`.

## Files
- `project1.c` — main source file
- `problem1_signals.c` — Project 2 Problem 1 signal version of Project 1
- `problem2_signals.c` — Project 2 Problem 2 signal and masking experiments
- `Makefile` — compile instructions
- `input.txt` — generated input array
- `output.txt` — process log and results
- `trace.csv` — per-process timing data
- `REPORT.md` — Project 2 design, experiments, observations, and written answers
- `outputs/` — generated Project 2 test outputs

## Compile
```
make
```

## Run
```
./project1 <L> <H> <PN> [branch]
```

Project 2:
```
make run-problem1
make run-problem1-exp1
make run-problem1-exp2
make run-problem2-part1
make run-problem2-q2
make run-problem2-q3
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
