// ERROR: circular dependency
A :: B + 1;
B :: A + 1;
main :: () {}
