// ERROR: #c_call/#foreign return type [4]float32 is not supported: C does not pass arrays by value
make :: () -> [4]float32 #foreign;
main :: () { m := make(); }
