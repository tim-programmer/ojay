// ERROR: exceeded the call depth limit
// ERROR: more calls)
down :: (n: int) -> int { return down(n - 1) + 1; }
X :: #run down(10);
