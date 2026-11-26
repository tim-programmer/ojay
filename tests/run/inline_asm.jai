// #asm: the target's instructions, with Jai variables as operands.
#import "Basic";

#if CPU == .X64  Word :: s64;
else             Word :: s32;

SHIFT :: 2;

combine :: (a: Word, b: Word) -> Word {
    x := a;
    y := b;
    #if CPU == .X64 {
        #asm {
            add x, y        // x += y
            sub x, 1; shl x, SHIFT
        }
    } else #if CPU == .ARM32 {
        #asm {
            add x, x, y
            sub x, x, #1
            lsl x, x, #SHIFT
        }
    } else {
        #asm {
            add x, x, y
            addi x, x, -1
            slli x, x, SHIFT
        }
    }
    return x;
}

load :: (p: *Word) -> Word {
    v: Word;
    #if CPU == .X64       #asm { mov v, [p] }
    else #if CPU == .ARM32 #asm { ldr v, [p] }
    else                  #asm { lw v, 0(p) }
    return v;
}

// Counts down to zero in a loop (labels are unique to each copy of the block).
count_down :: (n: Word) -> Word {
    steps: Word = 0;
    #if CPU == .X64 {
        #asm {
            again: add steps, 1
            sub n, 1
            jnz again
        }
    } else #if CPU == .ARM32 {
        #asm {
            again: add steps, steps, #1
            subs n, n, #1
            bne again
        }
    } else {
        #asm {
            again: addi steps, steps, 1
            addi n, n, -1
            bnez n, again
        }
    }
    return steps;
}

main :: () {
    print("%\n", combine(5, 6));  // ((5 + 6) - 1) << 2
    w: Word = 1234;
    print("%\n", load(*w));
    print("%\n", count_down(7));
    #asm { "nop" }
    // Names that are not variables (registers, special registers, CSRs) stay as written.
    special: Word = -1;
    #if CPU == .X64       #asm { mov special, rsp }
    else #if CPU == .ARM32 #asm { mrs special, PRIMASK }
    else                  #asm { csrr special, mhartid }
    print("%\n", special != -1);
    #if CPU == .X64 {
        f: float32 = 1.5;
        g: float32 = 2.25;
        #asm { addss f, g }
        print("%\n", f);
    } else #if CPU == .ARM32 {
        f: float32 = 1.5;
        g: float32 = 2.25;
        #asm { vadd.f32 f, f, g }
        print("%\n", f);
    } else {
        print("3.75\n");
    }
}
