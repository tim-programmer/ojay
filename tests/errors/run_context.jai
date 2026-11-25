// ERROR: the context (context.allocator, context.logger, ...) is not supported yet
f :: () -> int { a := context.allocator; return 0; }
X :: #run f();
