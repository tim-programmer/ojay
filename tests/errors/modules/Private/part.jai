#scope_file
helper :: () -> int { return 20; }        // the same name as module.jai's: each file has its own
counter := 40;

#scope_export
part_api :: () -> int { counter += 1; return helper() + shared() + counter; }
