#import "Basic";

twice :: (c: Code) #expand {
    #insert c;
    #insert c;
}

INCREMENT :: #code counter += 1;
DECLARE :: #code doubled := counter * 2;
REPORT :: #code if counter > 2 print("big %\n", counter); else print("small %\n", counter);
SWAP :: #code a, b = b, a;

main :: () {
    counter := 0;
    twice(#code counter += 5);
    print("%\n", counter);
    #insert INCREMENT;
    #insert DECLARE;
    print("% %\n", counter, doubled);
    #insert REPORT;
    counter = 1;
    #insert REPORT;
    a, b := 1, 2;
    #insert SWAP;
    print("% %\n", a, b);
    for 1..3 {
        #insert #code if it == 2 continue;
        print("it %\n", it);
    }
    x := #insert #code counter + 41;
    print("%\n", x);
}
