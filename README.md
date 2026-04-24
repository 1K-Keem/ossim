# Simple Operating System (CO2018 Assignment)

## Overview
This repository contains our group's implementation for the Simple Operating System assignment for the Operating Systems (CO2018) course at HCMUT. The goal is to simulate major OS components, including a Multi-Level Queue (MLQ) scheduler, a 64-bit multi-level paging memory manager, synchronization mechanisms, and custom system calls.

## Team Members
| Name | Student ID | GitHub Username | Assigned Tasks |
| :--- | :--- | :--- | :--- |
| Lê Đức Nguyên Khoa | 2411603 | @username | *...* |
| Trần Văn Thiên Kim | 2411816 | @username | *...* |
| Trần Tấn Phát | 2412610 | @username | *...* |
| Lê Bảo Tấn Phong | 2412635 | @username | *...* |
| Phan Phước Thiện Quang | 2412843 | @username | *...* |
| Lương Hoàng Vĩnh Tiến | 2413477 | @username | *...* |

---

## Task Breakdown & Assignment

### 1. CPU Scheduler (MLQ Policy)
**Assignee:** [Name]
**Files:** `queue.c`, `sched.c`
- [ ] Implement `enqueue()` in `queue.c` to push PCBs into the correct priority queue.
- [ ] Implement `dequeue()` in `queue.c` to retrieve the next 'in turn' PCB.
- [ ] Implement `get_proc()` in `sched.c` to fetch waiting processes according to the MLQ slot rules.

### 2. Memory Management (Core Paging & Swapping)
**Assignee:** [Name]
**Files:** `mm.c`, `mm-vm.c`, `mm-memphy.c`
- [ ] Implement fundamental memory operations (`ALLOC`, `FREE`, `READ`, `WRITE`).
- [ ] Manage memory regions (`vm_rg_struct`) and track available space via the free list (`vm_freerg_list`).
- [ ] Implement the page swapping mechanism to move physical frames between the simulated RAM (`MEMRAM`) and SWAP (`MEMSWP`) devices.

### 3. Memory Management (64-bit Multi-level Architecture)
**Assignee:** [Name]
**Files:** `mm64.c`, architecture headers
- [ ] Implement the 64-bit 5-level address translation scheme (PGD, P4D, PUD, PMD, PT).
- [ ] Enforce canonical addressing rules (bits 63-57 set to `0` for user space and `1` for kernel space).
- [ ] Enforce strict separation between User Space and Kernel Space.
- [ ] Implement the `vmap_pgd_memset` system call handler to simulate dummy allocations in the large 64-bit address space.

### 4. Synchronization & System Calls
**Assignee:** [Name]
**Files:** `syscall.c`, `src/sys_xxxhandler.c`, `src/syscall.tbl`
- [ ] Identify shared resources accessed by multiple virtual processors.
- [ ] Implement locking mechanisms (e.g., mutexes, spinlocks) to protect shared data structures (queues, memory lists) from race conditions.
- [ ] Create and register new system call handlers in the kernel interface.
- [ ] Write user-space test programs (e.g., `sc`) to invoke and verify custom system calls.

### 5. Integration, Build & Logging Pipeline
**Assignee:** [Name]
**Files:** `Makefile`, shell scripts, `input/` configs
- [ ] Maintain the `Makefile` and build instructions for all environments.
- [ ] Build a structured logging pipeline to capture OS events (context switches, page faults, memory allocations) to make debugging easier for the team.
- [ ] Write automation scripts to run the compiled code against all test cases in the `input/` directory to ensure system stability.

### 6. Technical Writing & Project Management
**Assignee:** [Name]
**Files:** Final Report (PDF)
- [ ] Draw the Gantt diagram describing CPU process execution scheduling based on team outputs.
- [ ] Map and document the status of memory allocation in data segments.
- [ ] Visualize and explain the multi-level paging address translation scheme.
- [ ] Extract and compile statistics on memory accesses and multilevel paging storage size from the simulation.
- [ ] Draft detailed answers for all theoretical questions embedded in the assignment specification.
- [ ] Verify the codebase against GNU C coding standards and package the final `assignment_[STUDENTID].zip` file.

---

## Project Structure
* `include/` - Header files (`common.h`, `os-cfg.h`, `os-mm.h`, etc.)
* `src/` - Source code for OS modules (`queue.c`, `sched.c`, `mm.c`, `syscall.c`, etc.)
* `input/` - Configuration and process description files for testing.
* `output/` - Sample outputs to verify our implementation.
* `Makefile` - Build instructions.

---

## How to Build and Run

### Prerequisites

- GNU Make
- GCC
- POSIX shell tools (`chmod`, `mkdir`, `rm`)
- `pthread` support (already used by the `Makefile` via `-lpthread`)

> Note for Windows users: run this project in **WSL**, **Git Bash (MSYS2)**, or another Unix-like environment. Running `make` directly in PowerShell/CMD can fail because the `Makefile` uses POSIX commands.

### 1. Build

```bash
make all
```

This produces the executable:

- `./os`

### 2. Run One Test Case

The program expects a config name from `input/` (do not include `input/` in the argument).

```bash
./os os_1_mlq_paging
```

To save output:

```bash
./os os_1_mlq_paging > output/os_1_mlq_paging.output
```

### 3. Run All Default Test Cases

```bash
for tc in \
	os_0_mlq_paging \
	os_1_mlq_paging \
	os_1_mlq_paging_small_1K \
	os_1_mlq_paging_small_4K \
	os_1_singleCPU_mlq \
	os_1_singleCPU_mlq_paging \
	os_2_mlq_paging \
	os_2_singleCPU_mlq_paging \
	sched \
	sched_0 \
	sched_1 \
	os_sc \
	os_syscall \
	os_syscall_list
do
	./os "$tc" > "output/${tc}.output"
done
```

### 4. Compare with Expected Outputs

Expected output samples are also stored in `output/`. You can compare generated outputs with those files to validate behavior.

### 5. Clean Build Artifacts

```bash
make clean
```