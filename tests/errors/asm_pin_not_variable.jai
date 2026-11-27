// ERROR: is not a variable or a register declared in the block, so it cannot be pinned
#import "Basic";
main :: () {
    #asm { nothing === r0; nop }
}
