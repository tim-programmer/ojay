// ERROR: fixed addresses such as memory-mapped registers do not exist during #run
GPIO :: struct { PDOR: u32; }
GPIO0 :: cast(*GPIO) 0x4009_6000;
read_pin :: () -> u32 { return GPIO0.PDOR; }
PIN :: #run read_pin();
