# Simple Operating System (CO2018 Assignment)

## Overview
This repository contains our group's implementation for the Simple Operating System assignment for the Operating Systems (CO2018) course at HCMUT. The goal is to simulate major OS components, including a Multi-Level Queue (MLQ) scheduler, a 64-bit multi-level paging memory manager, synchronization mechanisms, and custom system calls.

## Team Members
| Name | Student ID | GitHub Username | Assigned Tasks |
| :--- | :--- | :--- | :--- |
| Lê Đức Nguyên Khoa | 2411603 | @monoz2509k | Technical_Documentation & Data_Synthesis |
| Trần Văn Thiên Kim | 2411816 | @1K-Keem | Advanced_Memory_Mapping |
| Trần Tấn Phát | 2412610 | @tranphat246 | User_Space_Memory |
| Lê Bảo Tấn Phong | 2412635 | @tanphong-sudo | System_Calls, Synchronization & Centralized_Logging |
| Phan Phước Thiện Quang | 2412843 | @ducklemon596 | Kernel_Space_Memory |
| Lương Hoàng Vĩnh Tiến | 2413477 | @Vinh-Tien-hcmut | CPU_Scheduler |

---
## Task Breakdown & Assignment

### 1. CPU Scheduler (MLQ Policy)
**Assignee:** Lương Hoàng Vĩnh Tiến
**Files:** `queue.c`, `sched.c`
- [ ] Implement `enqueue()` to push PCBs into the correct priority queue.
- [ ] Implement `dequeue()` to retrieve the next 'in turn' PCB.
- [ ] Implement `get_proc()` to fetch waiting processes according to the Multi-Level Queue slot rules and dual-priority mechanisms.

### 2. User Space Memory (Core Paging & Swapping)
**Assignee:** Trần Tấn Phát
**Files:** `mm.c`, `mm-vm.c`, `mm-memphy.c`
- [ ] Implement fundamental user-space operations (`ALLOC`, `FREE`, `READ`, `WRITE`).
- [ ] Manage virtual memory regions (`vm_rg_struct`) and track available space via the free list (`vm_freerg_list`).
- [ ] Implement the page swapping mechanism to move physical frames between the simulated RAM (`MEMRAM`) and SWAP (`MEMSWP`) devices.

### 3. Kernel Space Memory (Slab Allocator)
**Assignee:** Phan Phước Thiện Quang
**Files:** `mm.c` (Kernel operations section)
- [ ] Implement `kmalloc` to allocate physically contiguous memory regions in kernel space.
- [ ] Implement `kmem_cache_create` to initialize slab cache pools for frequently requested structures.
- [ ] Implement `kmem_cache_alloc` to allocate objects from the predefined slab cache pools to reduce memory fragmentation.

### 4. Advanced Memory Mapping (64-bit Multi-level Architecture)
**Assignee:** Trần Văn Thiên Kim
**Files:** `mm64.c`, architecture headers
- [ ] Implement the 64-bit 5-level address translation scheme (PGD, P4D, PUD, PMD, PT).
- [ ] Enforce canonical addressing rules (bits 63-57 set to `0` for user space and `1` for kernel space).
- [ ] Implement the `vmap_pgd_memset` system call handler to simulate dummy allocations in the large 64-bit address space.

### 5. System Calls, Synchronization & Centralized Logging
**Assignee:** Lê Bảo Tấn Phong
**Files:** `syscall.c`, `sys_xxxhandler.c`, `syscall.tbl`, Core Data Structures
- [ ] Implement locking mechanisms (mutexes/spinlocks) to protect shared data structures (queues, memory lists) from race conditions in the multi-processor simulation.
- [ ] Create and register new system call handlers in the kernel interface, ensuring user-space test programs (`sc`) work correctly.
- [ ] Implement a centralized logging pipeline within the OS to trace segmentation faults, memory leaks, and context switches across CPUs, making debugging easier for the whole team.

### 6. Technical Documentation & Data Synthesis
**Assignee:** Lê Đức Nguyên Khoa
**Files:** Final Report (PDF)
- [ ] Synthesize simulation outputs to draw the Gantt diagram describing CPU process execution scheduling.
- [ ] Map and document the status of memory allocation in data segments.
- [ ] Visualize and explicitly explain the multi-level paging address translation scheme.
- [ ] Extract and compile statistics on memory accesses and multilevel paging storage size.
- [ ] Draft detailed, logical answers for all theoretical questions embedded in the assignment specification.
- [ ] Verify the codebase against GNU C coding standards and typeset the final academic report (e.g., using LaTeX).

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