// ERROR: is not a register that can be pinned
#import "Basic";
main :: () {
    x := 1;
    #if CPU == .X64        #asm { x === rsp; add x, 1 }
    else #if CPU == .ARM32 #asm { x === sp; add x, x, #1 }
    else                   #asm { x === sp; addi x, x, 1 }
}
