# SORTING AND SEARCHING ALGORITHMS SPECIFICATION

## 1. Overview
SettleSmart 2.0 incorporates foundational sorting and searching algorithms in pure C to power high-speed in-memory queries, deterministic presentation of transaction histories, and O(log n) random-access lookups.

## 2. Bubble Sort (`src/algorithms/bubble_sort.c`, `bubble_sort.h`)
Bubble Sort provides a simple, stable, in-place sorting mechanism for bounded in-memory arrays (such as recent transactions).

### Capabilities
- **Sorting Keys**:
  - `amount`: Transaction financial value in paise (`amount_paise`).
  - `created_at` / `timestamp`: Transaction creation timestamp string.
  - `id`: Unique database transaction ID.
- **Direction**: Ascending (`SORT_ORDER_ASC`) or Descending (`SORT_ORDER_DESC`).
- **Deterministic Tie-Breaking**: When two records possess identical primary keys, they are stably broken by `id` ascending.
- **Early Exit Optimization**: Employs a swapped flag; if a pass yields zero swaps, the array is already sorted and execution halts immediately.

### API Integration
- `GET /api/transactions/recent/sorted?sort_by=amount&order=desc`: Returns recent transactions sorted in memory.
- Default: `sort_by=created_at&order=desc`.

## 3. Binary Search (`src/algorithms/binary_search.c`, `binary_search.h`)
Binary Search enables $O(\log n)$ random-access lookups on sorted arrays of transactions and entities.

### Implementations
1. **Transaction Search by ID**:
   - `binary_search_transaction_by_id(const Transaction *arr, size_t count, long long target_id, size_t *out_index)`
   - Requires the array to be sorted by `id` ascending.
   - Divides search interval in half on each step:
     - If `arr[mid].id == target_id`, returns 0 with `*out_index = mid`.
     - If `arr[mid].id < target_id`, search upper half `low = mid + 1`.
     - Else search lower half `high = mid - 1`.
2. **Entity Search by ID**:
   - `binary_search_entity_by_id(const Entity *arr, size_t count, long long target_id, size_t *out_index)`
   - Operates identically over sorted entity arrays.

### API Integration
- `GET /api/transactions/recent/search?id=:id`:
  - Fetches recent transactions for the organization.
  - Sorts them by ID using `bubble_sort_transactions(arr, count, SORT_BY_ID, SORT_ORDER_ASC)`.
  - Executes `binary_search_transaction_by_id` in $O(\log n)$ time.
  - Returns `200 OK` with transaction payload if found, or `404 Not Found` if absent.
