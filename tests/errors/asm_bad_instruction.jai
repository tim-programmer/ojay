// ERROR: error: in #asm
// ERROR: invalid instruction mnemonic 'frobnicate'
main :: () { x := 1; #asm { frobnicate x } }
