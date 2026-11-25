// The heap reclaims and merges freed memory: this allocates, checks and frees
// far more than the heap holds (about 110 KB on the MCX N947 RAM image).
#import "Basic";

seed: u32 = 12345;
random :: (n: int) -> int {
    seed = seed * 1664525 + 1013904223;
    return cast(int) (seed >> 8) % n;
}

Block :: struct {
    p: *u8;
    size: int;
    tag: u8;
}

fill :: (b: *Block) {
    for 0..b.size - 1  b.p[it] = cast,trunc(u8) (b.tag + it);
}

intact :: (b: *Block, count: int) -> bool {
    for 0..count - 1  if b.p[it] != cast,trunc(u8) (b.tag + it)  return false;
    return true;
}

SLOTS :: 48;
MAX_SIZE :: 1200;

main :: () {
    // Random allocations, reallocations and frees, with their contents checked.
    blocks: [SLOTS] Block;
    allocated, allocations, reallocations, frees := 0, 0, 0, 0;
    for round: 1..6000 {
        b := *blocks[random(SLOTS)];
        if !b.p {
            b.size = 1 + random(MAX_SIZE);
            b.tag = cast,trunc(u8) random(256);
            b.p = alloc(b.size);
            assert((cast(int) b.p) % 8 == 0, "alloc is 8-byte aligned");
            fill(b);
            allocated += b.size;
            allocations += 1;
            continue;
        }
        if !intact(b, b.size) { print("block corrupted in round %\n", round); exit(1); }
        if random(3) == 0 {
            new_size := 1 + random(MAX_SIZE);
            b.p = realloc(b.p, new_size, b.size);
            if !intact(b, min(b.size, new_size)) { print("realloc lost data in round %\n", round); exit(1); }
            if new_size > b.size  allocated += new_size - b.size;
            b.size = new_size;
            fill(b);
            reallocations += 1;
        } else {
            free(b.p);
            b.p = null;
            frees += 1;
        }
    }
    for * blocks  if it.p { free(it.p); frees += 1; }
    print("random: % allocations, % reallocations, % frees, % bytes, more than 1 MB: %\n",
          allocations, reallocations, frees, allocated, allocated > 1_000_000);

    // Merging: 72 KB of small blocks, freed in an interleaved order, then a
    // 56 KB block. Without merging the small free blocks the heap could not
    // hold both (on the MCX N947). Repeated, with different small sizes.
    ptrs: [640] *u8;
    for cycle: 1..3 {
        size := 72 + cycle * 8;
        for * ptrs { it.* = alloc(size); memset(it.*, cast,trunc(u8) it_index, size); }
        for i: 0..ptrs.count - 1  if i % 2 == 1  free(ptrs[i]);
        for i: 0..ptrs.count - 1  if i % 2 == 0 {
            assert(ptrs[i][size - 1] == cast,trunc(u8) i);
            free(ptrs[i]);
        }
        big: Block;
        big.size = 56_000;
        big.tag = cast,trunc(u8) cycle;
        big.p = alloc(big.size);
        fill(*big);
        if !intact(*big, big.size) { print("big block corrupted\n"); exit(1); }
        free(big.p);
        print("cycle %: % blocks of % bytes, then one of % bytes\n", cycle, ptrs.count, size, big.size);
    }

    // Growing one block with realloc while others come and go.
    grow: Block;
    grow.tag = 9;
    for 1..50 {
        new_size := it * 1000;
        grow.p = realloc(grow.p, new_size, grow.size);
        if grow.size && !intact(*grow, grow.size) { print("growing block lost data\n"); exit(1); }
        grow.size = new_size;
        fill(*grow);
        if grow.size >= 40_000 {  // keep the heap from filling up
            free(grow.p);
            grow.p = null;
            grow.size = 0;
        }
        tmp := alloc(300);
        free(tmp);
    }
    free(grow.p);
    print("done\n");
}
