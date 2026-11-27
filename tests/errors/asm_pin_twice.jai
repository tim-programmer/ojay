// ERROR: two operands are pinned to register
#import "Basic";
main :: () {
    x := 1;
    y := 2;
    #if CPU == .X64        #asm { x === rax; y === eax; add x, y }
    else #if CPU == .ARM32 #asm { x === r0; y === r0; add x, x, y }
    else                   #asm { x === a0; y === a0; add x, x, y }
}
