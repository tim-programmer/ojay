// ERROR: volatile_load accesses memory-mapped hardware
#import "Hardware";
REG :: cast(*u32) 0x4009_6000;
X :: #run volatile_load(REG);
