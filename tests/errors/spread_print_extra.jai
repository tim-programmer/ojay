// ERROR: 'print' takes '..' only right after the format string, as its only argument
#import "Basic";
main :: () { args: []Any; print("% %", 1, ..args); }
