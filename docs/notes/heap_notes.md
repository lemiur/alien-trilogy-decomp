The candidate boundary intervals are established from the return/delay-slot structure in the original executable:

- `Heap_Alloc`: 0x80014dfc..0x80014e7b, 128 bytes. Exact with readable C, GCC C257 `-O2 -G8`, and both ASPSX A221 and A234. The allocator uses `g_heapNext` as a pointer and `g_heapUsed` / `g_heapFree` as numeric byte counts.
- `Heap_Mark`: 0x80014ecc..0x80014f87, 188 bytes.
- `Heap_Release`: 0x80014f88..0x80015047, 192 bytes.

The actual snapshot table begins at 0x8009b30c; fields occur in address order `used`, `free`, `next` at offsets 0, 4, and 8, respectively. The candidate C is preserved in `heap_drafts.c`; its compiled extent is currently 200 bytes for `Heap_Mark` and 220 bytes for `Heap_Release`, so neither is certified. The extra code comes from the compiler's array addressing/control-flow choices. No synthetic base anchor is used.
