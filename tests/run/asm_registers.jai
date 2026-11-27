// #asm register declarations (`name: gpr;`, `name: vec;`), pinning (`=== reg`)
// and registers named in the block (clobbered: the compiler keeps nothing there).
#import "Basic";

#if CPU == .X64  Word :: s64;
else             Word :: s32;

// A temporary register: x * 3.
triple :: (x: Word) -> Word {
    y := x;
    #if CPU == .X64 {
        #asm {
            t: gpr;
            mov t, y
            add y, t
            add y, t
        }
    } else #if CPU == .ARM32 {
        #asm {
            t: gpr;
            mov t, y
            add y, y, t
            add y, y, t
        }
    } else {
        #asm {
            t: gpr;
            mv t, y
            add y, y, t
            add y, y, t
        }
    }
    return y;
}

// Pinned operands: quotient and remainder (x86 div uses rdx:rax; the others
// use fixed registers named in the instructions).
divide :: (a: Word, b: Word) -> Word, Word {
    q := a;
    r: Word = 0;
    d := b;
    #if CPU == .X64 {
        #asm {
            q === a;
            r === d;
            xor r, r
            div d
        }
    } else #if CPU == .ARM32 {
        #asm {
            q === r0;
            r === r1;
            d === r2;
            udiv r3, r0, r2      // (r3 is named: clobbered)
            mls r1, r3, r2, r0   // r = a - (a / b) * b
            mov r0, r3
        }
    } else {
        #asm {
            q === a0;
            d === a1;
            r === a2;
            rem a2, a0, a1
            div a0, a0, a1
        }
    }
    return q, r;
}

// A register named in the block (not an operand) is clobbered: the values
// around the block that the compiler keeps in registers survive it.
around :: (x: Word) -> Word {
    a := x + 1;
    b := x + 2;
    c := x + 3;
    #if CPU == .X64        #asm { mov rax, -1; mov rcx, -1; mov rdx, -1; mov rsi, -1; mov rdi, -1; mov r8, -1 }
    else #if CPU == .ARM32 #asm { mov r0, #0; mov r1, #0; mov r2, #0; mov r3, #0; mov r12, #0 }
    else                   #asm { li a0, -1; li a1, -1; li a2, -1; li t0, -1; li t1, -1 }
    return a * 100 + b * 10 + c;
}

// A declared vec register (no float registers on the ESP32-C6).
#if CPU != .RISCV32 {
    twice :: (f: float32) -> float32 {
        x := f;
        #if CPU == .X64 {
            #asm {
                v: vec;
                movss v, x
                addss x, v
            }
        } else {
            #asm {
                v: vec;
                vmov.f32 v, x
                vadd.f32 x, x, v
            }
        }
        return x;
    }
} else {
    twice :: (f: float32) -> float32 { return f * 2; }
}

main :: () {
    print("% %\n", triple(14), triple(-5));
    q, r := divide(100, 7);
    print("% %\n", q, r);
    print("%\n", around(1));
    print("%\n", twice(1.25));
}
