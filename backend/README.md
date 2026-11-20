# Smart Public Transport Fleet & Route Optimization System (Bangalore BMTC)
## Part 1: Core C Data Structure Library (`libtransport_core`)

### 1. Project Overview
The Smart Public Transport Fleet & Route Optimization System is an intelligent route planning, fleet scheduling, and live simulation platform designed for Bangalore's bus transit network (BMTC). This repository contains **Part 1** of the system: a high-performance, strictly verified, memory-safe C11 core data structure library. Engineered with zero external dependencies, comprehensive pointer validation, opaque container types, and mathematically verified algorithmic bounds, this foundational library directly powers subsequent phases including graph search (A* and Dijkstra routing), real-time passenger flow simulations, REST microservices, and React operational dashboards.

---

### 2. Folder Structure
```text
project/
  backend/
    include/
      common.h         # Status codes, status_to_string, shared callback function pointer types
      types.h          # Domain entity models (Stop, Route, Bus, Trip, Edge, Passenger, etc.)
      linkedlist.h     # Doubly linked list with head, tail, and size tracking
      stack.h          # Dynamic array LIFO stack with 0-indexed top access
      queue.h          # Circular-buffer FIFO queue with wrap-around resizing
      tree.h           # General tree (first-child / next-sibling representation)
      heap.h           # Binary heap with dynamic increase/decrease-key indexing
      avl.h            # Self-balancing AVL tree with LL/RR/LR/RL rotations
      hashtable.h      # Separate chaining hash table (int64_t and string keys)
    src/
      common.c         # Implementation of status code to string mappings
      types.c          # Constructors, destructors, and entity comparator helpers
      linkedlist.c     # Doubly linked list implementation
      stack.c          # Stack implementation
      queue.c          # Queue implementation
      tree.c           # General tree implementation
      heap.c           # Binary heap implementation
      avl.c            # AVL tree implementation
      hashtable.c      # Separate chaining hash table implementation
    tests/
      test_util.h      # Minimalist assertion and test-runner macro framework
      test_linkedlist.c# Linked list test suite
      test_stack.c     # Stack test suite (1000-element stress test)
      test_queue.c     # Queue test suite (wrap-around growth test)
      test_tree.c      # Tree test suite (3-level hierarchy and subtree deletion)
      test_heap.c      # Heap test suite (min-heap, max-heap, decrease-key)
      test_avl.c       # AVL test suite (1000-element balance, 500 deletions, rotations)
      test_hashtable.c # Hash table test suite (10000-element resizing, collisions)
      test_types.c     # Entity constructors, destructors, and priority heap tests
    Makefile           # Strict build system (all, test, asan, valgrind, clean)
    README.md          # Comprehensive library documentation
  data/
    .gitkeep           # Placeholder for schedule and geospatial datasets
  docs/
    .gitkeep           # Placeholder for architecture and API documentation
  .gitignore           # Ignores build artifacts (*.o, *.a, *.exe)
```

---

### 3. Build and Test Commands
The build system uses GNU Make with strict compiler flags (`-std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror -g -Iinclude`).

```bash
# Navigate to backend directory
cd project/backend

# Build the static library (build/libtransport_core.a)
make all

# Build and execute all 8 test suites
make test

# Build and run all test suites with AddressSanitizer and UndefinedBehaviorSanitizer
make asan

# Run memory leak and access checks under Valgrind (on supported Linux/Unix platforms)
make valgrind

# Clean all generated build artifacts and test binaries
make clean
```

---

### 4. Memory Ownership Rules
All containers in this library store generic `void*` items and strictly enforce uniform ownership semantics:
1. **Creation with Optional Destructor (`FreeFn`):**
   - Each container is initialized with an optional `FreeFn free_fn`.
   - **Owned Mode (`free_fn != NULL`):** The container owns its elements. When an element is removed-and-discarded (e.g. `ll_remove_at`, `ll_remove_if`, `avl_delete`, `ht_remove_int`, `ht_remove_str`, `tree_remove_subtree`), cleared (`*_clear`), or destroyed (`*_destroy`), `free_fn` is automatically invoked on each contained item.
   - **Unowned Mode (`free_fn == NULL`):** The container acts as a non-owning view/reference collection. It never frees any contained items.
2. **Transfer of Ownership on Extraction:**
   - Functions that explicitly remove and **return** an item to the caller (`stack_pop`, `queue_dequeue`, `heap_pop`, `heap_remove_at`) **transfer ownership** to the caller. `free_fn` is **never** invoked on returned items, regardless of whether `free_fn` was configured. The caller is responsible for the item's lifecycle.
3. **Set Semantics in Hash Table:**
   - When using the Hash Table as a set (e.g. A* closed set), `NULL` values are permitted. Calling `ht_get_*` returning `NULL` is ambiguous; callers must use `ht_contains_*` to check presence.

---

### 5. Module Operations and Time Complexities

| Module | Core Data Structure | Key Operations | Time Complexity | Space Complexity |
| :--- | :--- | :--- | :--- | :--- |
| **`LinkedList`** | Doubly Linked List with Head & Tail | `ll_append`, `ll_prepend`<br>`ll_insert_at`, `ll_remove_at`<br>`ll_get`, `ll_find`<br>`ll_foreach`, `ll_foreach_reverse` | $O(1)$<br>$O(n)$<br>$O(n)$<br>$O(n)$ | $O(n)$ |
| **`Stack`** | Dynamic Array (initial cap 8, factor 2) | `stack_push`<br>`stack_pop`, `stack_peek`<br>`stack_get` (offset from top)<br>`stack_size`, `stack_is_empty` | $O(1)$ amortized<br>$O(1)$<br>$O(1)$<br>$O(1)$ | $O(n)$ |
| **`Queue`** | Circular Buffer (initial cap 8, factor 2) | `queue_enqueue`<br>`queue_dequeue`, `queue_peek`<br>`queue_foreach` (front to back)<br>`queue_size`, `queue_is_empty` | $O(1)$ amortized<br>$O(1)$<br>$O(n)$<br>$O(1)$ | $O(n)$ |
| **`Tree`** | First-Child / Next-Sibling Tree | `tree_add_child`<br>`tree_remove_subtree`<br>`tree_height`<br>`tree_preorder`, `tree_levelorder`<br>`tree_find` | $O(k)$ ($k$ children)<br>$O(s)$ ($s$ subtree nodes)<br>$O(n)$<br>$O(n)$<br>$O(n)$ | $O(n)$ |
| **`Heap`** | Dynamic Array Binary Min/Max Heap | `heap_push`<br>`heap_pop`, `heap_remove_at`<br>`heap_peek`<br>`heap_update` (decrease/increase key)<br>`heap_is_valid` | $O(\log n)$<br>$O(\log n)$<br>$O(1)$<br>$O(\log n)$<br>$O(n)$ | $O(n)$ |
| **`AVL Tree`** | Height-Balanced BST (LL, RR, LR, RL) | `avl_insert`<br>`avl_delete`<br>`avl_search`<br>`avl_lower_bound`<br>`avl_min`, `avl_max`<br>`avl_inorder`, `avl_is_balanced` | $O(\log n)$<br>$O(\log n)$<br>$O(\log n)$<br>$O(\log n)$<br>$O(\log n)$<br>$O(n)$ | $O(n)$ |
| **`HashTable`** | Separate Chaining (SplitMix64 / FNV-1a) | `ht_put_int`, `ht_put_str`<br>`ht_get_int`, `ht_get_str`<br>`ht_contains_int`, `ht_contains_str`<br>`ht_remove_int`, `ht_remove_str`<br>`ht_foreach` | $O(1)$ average, $O(n)$ worst<br>$O(1)$ average, $O(n)$ worst<br>$O(1)$ average, $O(n)$ worst<br>$O(1)$ average, $O(n)$ worst<br>$O(b + n)$ | $O(b + n)$ |

---

### 6. Heap Comparator Convention
The binary heap implementation (`heap.h`/`heap.c`) uses a unified priority comparison rule:
$$\text{cmp}(a, b) < 0 \iff a \text{ has HIGHER priority than } b \text{ (popped first)}$$
- **Min-Heap Configuration:** Use an ascending comparator (e.g. `a < b ? -1 : (a > b ? 1 : 0)`). Lower numerical values emerge first (e.g. lowest $f(n)$ score in A* search).
- **Max-Heap Configuration:** Use a descending comparator (e.g. `a > b ? -1 : (a < b ? 1 : 0)`). Higher numerical values emerge first (e.g. highest route dispatch priority).
- **In-Place Index Tracking (`HeapIndexFn`):** An optional callback `void (*HeapIndexFn)(void *item, size_t index)` is triggered on every insertion and swap. Contained structs can store their current heap index, allowing $O(\log n)$ decrease-key/increase-key via `heap_update(h, item->heap_index)`.

---

### 7. Time and Physical Unit Conventions
All entity models and algorithms adhere to strict standardized physical units:
- **Simulation Time:** `double` representing elapsed minutes since midnight (e.g., `480.0` = 08:00 AM, `1050.5` = 05:30:30 PM).
- **Schedule Departure Time:** `int` representing minutes since midnight (`0` to `1439`).
- **Distances:** `double` representing length in **metres** ($m$).
- **Edge Travel Time (`base_time_s`):** `double` representing traversal duration in **seconds** ($s$).
- **Geographic Coordinates:** `double` latitude and longitude in decimal degrees (e.g. Bangalore center: $12.9716^\circ\text{ N}, 77.5946^\circ\text{ E}$).

---

### 8. Architectural Mapping to Future Project Parts
| Library Module | Future Project Component | Role and Responsibility |
| :--- | :--- | :--- |
| **`Heap` (Min-Heap)** | **Part 2: Routing Engine (A* Search)** | Priority queue for the A* open list, ordering candidate route nodes by $f(n) = g(n) + h(n)$. Uses `heap_update` for decrease-key operations when shorter paths are discovered. |
| **`HashTable`** | **Parts 2 & 4: Closed Set & Fast Lookups** | Set of visited stop IDs in A* search (storing `NULL` values), and $O(1)$ primary key lookup tables for routes, buses, and stops across REST API endpoints. |
| **`AVL Tree`** | **Parts 2 & 3: Timetable Engine** | Stores sorted departure schedules for all trips on each route. Fast `avl_lower_bound` enables instant queries for "next bus departure after time $T$". |
| **`Queue`** | **Part 3: Simulation Engine** | FIFO passenger queues at bus stops. Models boarding dynamics and calculates real-time passenger wait times. |
| **`Heap` (Max-Heap)** | **Part 3: Fleet Dispatch Optimizer** | Greedy allocator prioritizing bus assignments to routes experiencing high demand-to-bus ratios ($\text{priority} = \frac{\text{demand}}{\text{assigned\_buses} + 1}$). |
| **`Stack`** | **Parts 4 & 5: Administrative Undo/Redo** | LIFO history stack recording route detour modifications, road blockage events, and timetable changes to support seamless undo operations in the React UI. |
| **`LinkedList`** | **Parts 2 & 3: Stop Sequences & History** | Ordered sequence of stops defining each route, and chronologically ordered bus journey records (`JourneyRecord`). |
| **`Tree`** | **Parts 2 & 4: Organizational Hierarchy** | Hierarchical transit management modeling `City > Depots > Routes > Buses`. Supports cascading aggregations and subtree removal during depot maintenance. |

