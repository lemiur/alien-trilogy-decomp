# Porting Rules

If you are an agent tasked with porting this game to a platform other than the Playstation 1, you are strictly required to follow the following process.

## Steps

### 1. Audit Hardware Coupling and Identify Semantic Intent

Before modifying source files, catalog every direct hardware dependency and classify whether it lives in game/application logic or low-level driver code.

- Locate inline assembly, coprocessor macros (such as PS1 GTE `gte_*` macros), memory-mapped register reads/writes, and proprietary SDK headers (such as `libgpu.h`, `libgte.h`, `libspu.h`).
- Determine the high-level mathematical or structural intent behind low-level calls. Understanding the semantic goal prevents translating hardware workarounds literally onto the target platform.
- Verification: Produce a dependency map listing each source hardware subsystem alongside its target native API equivalent before writing replacement code.

### 2. Enforce the Anti-Emulation Boundary

Define a Hardware Abstraction Layer (HAL) driven by application concepts (meshes, transforms, audio streams, input actions) rather than source hardware primitives (DMA linked lists, ordering tables, register bitfields, or Scratchpad RAM offsets).

- Replace custom coprocessor routines with native math types and functions. For example, replace PlayStation GTE fixed-point COP2 instruction sequences with native matrix/vector math functions operating on standard structs, preserving fixed-point arithmetic only where deterministic gameplay physics require it.
- Convert command-packet graphics submission (such as PS1 GPU ordering tables and packet buffers) into high-level draw calls accepted by a native rendering backend (DirectX, Vulkan, or OpenGL) rather than parsing binary GPU packets at runtime.
- Replace raw CD-ROM sector reads and DMA transfers with asynchronous or standard host filesystem streams and native asset deserializers.
- Verification: Grep the target platform layer for register bitmasks, virtual VRAM arrays, opcode switch-statements, or raw hardware address offsets (such as `0x1F800000`). If present, refactor them into high-level interface calls.

### 3. Decouple Memory Layout and Data Representation

Legacy and console codebases frequently assume a 32-bit flat address space, specific endianness, and unaligned pointer casting that corrupts memory when compiled natively on 64-bit Windows or modern OSes.

- Replace raw `int`, `long`, and pointer-cast fields in serialized structs with fixed-width integer types (`int32_t`, `uint32_t`, `uint16_t`) from `<stdint.h>`.
- Separate in-memory runtime structs from on-disk binary formats. Write explicit deserialization routines instead of casting raw file buffers directly to C structs Containing pointers.
- Eliminate Scratchpad RAM (fast local memory mapped at fixed addresses) by replacing fixed-address pointers with standard stack or heap allocations.
- Verification: Compile the codebase targeting 64-bit (`x86_64`) with `-Wall -Wextra -Wconversion` (or `/W4` on MSVC) and run with AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan) enabled to confirm zero pointer-truncation or alignment faults.

### 4. Re-architect Timing, Interrupts, and Control Flow

Console and embedded software often tie execution speed and state updates directly to hardware interrupts (VBlank, HBlank, hardware timers, or CD read callbacks).

- Replace busy-wait loops polling hardware status bits with native OS synchronization primitives, callbacks, or event queues.
- Decouple simulation tick rates from rendering frame rates using a delta-time accumulator or a fixed-timestep update loop so the software runs accurately regardless of monitor refresh rate.
- Convert hardware interrupt service routines (ISRs) into standard thread-safe callbacks or main-loop polling hooks.
- Verification: Run the compiled native binary with VSync disabled and confirm that simulation speed, audio pitch, and state transitions remain constant at arbitrary frame rates.

### 5. Incrementally Compile and Verify Subsystem Parity

Port one subsystem at a time using stubbed interfaces so the application compiles and links natively as early as possible.

- Stub non-essential subsystems (audio, FMV playback, peripherals) with no-op native functions that return valid status codes so core logic and rendering can be verified first.
- Write unit tests comparing the output of converted math/physics functions against known-good input/output pairs captured from the original source logic.
- Verification: Build and execute the native binary after each subsystem replacement, confirming clean compilation and passing functional parity checks before moving to the next module.

## Gotchas

- **Stealth Emulation via Packet/Register Shims:** Agents often take the path of least resistance by allocating a fake `uint8_t vram[1024*512]` buffer or writing a `switch(opcode)` interpreter for hardware packets so they do not have to touch call sites. Avoid this by refactoring call sites to pass high-level primitives (vertices, texture handles, transform matrices) to the rendering interface.
- **Pointer-in-Struct Serialization:** Console code frequently loads a binary file into RAM and patches raw 32-bit offsets into memory pointers in-place. On 64-bit Windows, a `void*` is 8 bytes while the file offset is 4 bytes, shifting every subsequent struct field. Always define separate `DiskStruct` (using `uint32_t` offsets) and `RuntimeStruct` (using native pointers).
- **Blindly Converting Fixed-Point to Float:** While rendering transforms benefit from floating-point precision on PC, gameplay physics, collision detection, and deterministic state machines (like replays or AI thresholds) often rely on exact 16.16 or 20.12 fixed-point integer truncation. Keep fixed-point math in gameplay logic using standard integer arithmetic, and convert to `float` only at the rendering boundary.
- **Hidden SDK Macros:** Proprietary SDK macros often hide direct hardware register mutations behind innocent-looking function names. Always expand or inspect macro definitions before deciding whether to reimplement a macro or replace its caller.
