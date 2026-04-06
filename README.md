# ECE 434 Spring 2026 - Project 1

Multiprocess array analysis using `fork`, `pipe`, `poll`, and `waitpid`.

## Program name
`project1.c`

## Purpose
This program implements a multi-process solution for ECE 434 Project 1. It generates an input file containing `L` integers, randomly inserts 150 hidden negative keys, loads the data into memory, partitions the array across multiple processes, and computes:
- the global maximum,
- the global average,
- up to `H` hidden key positions,
- timing and IPC trace information.

The implementation uses:
- `fork()` to create child processes,
- `pipe()` for parent/child communication,
- `poll()` so a parent can read results from whichever child finishes first,
- `waitpid()` and status macros to report child termination,
- `pstree` to display the process tree,
- `clock_gettime()` to measure runtime,
- file output for `input.txt`, `output.txt`, and `trace.csv`.

## Files produced
When the program runs, it creates these files in the current directory:

- `input.txt` — generated test input containing `L` integers
- `output.txt` — process messages, hidden key discoveries, final max/avg, and root summary
- `trace.csv` — CSV trace with per-process timing and byte-count information

## What the program does
1. Reads command-line arguments.
2. Allocates an array of size `L`.
3. Generates random positive integers.
4. Randomly inserts 150 hidden negative integers with values from `-1` to `-100`.
5. Writes the generated values to `input.txt`.
6. Starts from the root process and recursively partitions the array.
7. Each process either:
   - computes locally if the segment is small enough or the process limit is reached, or
   - forks child processes and assigns each child one partition.
8. Each child sends its result structure to the parent through a dedicated pipe.
9. The parent uses `poll()` to read whichever child pipe becomes ready first.
10. The parent merges child results and waits for all direct children with `waitpid()`.
11. The root writes final metrics and summary information to `output.txt`.
12. Each process appends one record to `trace.csv`.

## Output behavior
### `output.txt` contains
- startup configuration line,
- one message per process showing PID, return argument, and parent PID,
- messages for hidden key positions found by each process,
- child termination reports using `waitpid()` status analysis,
- final `Max` and `Avg`,
- up to `H` hidden key positions,
- root summary with total runtime, slowest process, and total IPC volume.

### `trace.csv` columns
```csv
pid,start_time,end_time,elements_processed,bytes_sent
```

## Process-tree behavior
The code supports BFS-style branching factors of `2`, `3`, or `5`, as requested. A process creates up to `branch` children and divides its current segment as evenly as possible across them.

Each child receives a unique return-code pattern derived from the parent using:
```c
child_task.ret_code = task.ret_code * 10 + (i + 1);
```
The actual process exit status is reduced to fit Linux exit-code limits:
```c
exit((child_task.ret_code % 250) + 1);
```

## Notes about this implementation
- The program generates the input file itself and also loads/uses the in-memory array, which is consistent with the project description.
- `poll()` is used to avoid fixed-order blocking while collecting child results.
- `sleep(1)` is included in child processes so the process tree remains visible for observation.
- `pstree -p` is invoked from the root to help inspect the created process tree.
- The program records timing and IPC information for every process in `trace.csv`.

## Assumptions and practical limits
- The program expects a Linux environment.
- `pstree` must be installed for the tree display command to work.
- Very large `PN` values or very large `L` values may stress system process or memory limits.
- The code stores up to `256` hidden key positions internally, which is enough for the project’s 150 hidden keys.

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
| `L` | Total number of integers to generate (≥ 12000) |
| `H` | Number of hidden key locations to print |
| `PN` | Maximum process count parameter (1–50) |
| `branch` | Optional Branching factor: 2, 3, or 5 (default 2) |

### Example
```
./project1 12000 20 7 2
./project1 100000 50 15 3
./project1 1000000 100 31 5
```

## Clean
```
make clean
```
