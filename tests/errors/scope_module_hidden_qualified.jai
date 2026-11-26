// ERROR: module 'P' has no member 'shared' that it exports
P :: #import "Private";
main :: () { x := P.shared(); }
