// ERROR: #asm: register 't' is declared twice
#import "Basic";
main :: () {
    #asm { t: gpr; t: gpr; nop }
}
