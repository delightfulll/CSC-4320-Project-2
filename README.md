# CSC 4320 Project 2 - Process Execution with Threads

This project simulates real-time process execution using threads in C and
implements the **Producer-Consumer** synchronization problem using a bounded
buffer, a mutex, and semaphores.

## What It Does

- Reads process info from `processes.txt`.
- Creates one thread per process.
- Each thread waits for its arrival time, simulates a CPU burst with `sleep()`,
  and then acts as a **producer** by adding itself to a bounded buffer.
- A **consumer** thread removes finished processes from the buffer.
- Uses a mutex to protect the shared buffer and semaphores to track empty and
  full slots.
- Prints each thread's activity (waiting for lock, acquiring, releasing, etc).

## Files

| File                | Description                         |
| ------------------- | ----------------------------------- |
| `process_threads.c` | Main C source code                  |
| `processes.txt`     | Input file with process data        |
| `.gitignore`        | Files to ignore in git              |

## Input Format

`processes.txt` has one process per line after the header:

```text
PID Arrival_Time Burst_Time Priority Memory_Size
1 0 6 3 200
2 1 4 3 350
3 2 2 1 120
```

## How to Build and Run

With `gcc`:

```bash
gcc -Wall -Wextra -pthread process_threads.c -o process_threads
./process_threads
```

## Notes

- Built and tested on macOS. Uses **named semaphores** (`sem_open`) because
  `sem_init` is deprecated on macOS. The code will also run on Linux.
- Requires a C compiler with pthread support (`-pthread` flag).

## Example Output

```text
Process 3 arrived (burst=2).
Process 3 started.
Process 3 finished.
[Producer 3] Waiting for empty slot...
[Producer 3] Waiting for buffer lock...
[Producer 3] Acquired lock, adding to buffer.
[Producer 3] Released lock and signaled full slot.
[Consumer] Waiting for buffer lock...
[Consumer] Acquired lock.
[Consumer] Removed process 3 from buffer.
...
All processes completed.
```
