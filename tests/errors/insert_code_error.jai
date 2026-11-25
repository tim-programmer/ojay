// ERROR: insert_code_error.jai:8:37: error: undeclared identifier 'undefined_thing'
// ERROR: insert_code_error.jai:5:5
// ERROR: insert_code_error.jai:8:14
run_twice :: (body: Code) #expand {
    #insert body;
    #insert body;
}
main :: () { run_twice(#code { q := undefined_thing; }); }
