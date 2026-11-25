#import "Basic";

// A small inventory system: dynamic arrays of structs, sorting by key,
// removal, and pointers into the array.

Item :: struct {
    name: string;
    quantity: int;
    price_cents: int;
    restock := false;
}

add_item :: (items: *[..]Item, name: string, qty: int, price: int) -> *Item {
    it := array_add(items);   // returns a pointer to the new, default-initialized slot
    it.name = name;
    it.quantity = qty;
    it.price_cents = price;
    return it;
}

total_value :: (items: []Item) -> int {
    total := 0;
    for items total += it.quantity * it.price_cents;
    return total;
}

sort_by_value :: (items: []Item) {
    // Simple selection sort, descending by stock value.
    for i: 0..items.count-2 {
        best := i;
        for j: i+1..items.count-1 {
            if items[j].quantity * items[j].price_cents > items[best].quantity * items[best].price_cents
                best = j;
        }
        if best != i {
            tmp := items[i];
            items[i] = items[best];
            items[best] = tmp;
        }
    }
}

remove_unordered :: (items: *[..]Item, index: int) {
    items.*[index] = items.*[items.count - 1];
    items.count -= 1;
}

format_cents :: (c: int) -> string {
    // Note: "%%" is an escaped percent sign, so the two digits are built separately.
    cents := c % 100;
    digits := ifx cents < 10 then sprint("0%", cents) else sprint("%", cents);
    return sprint("%.%", c / 100, digits);
}

main :: () {
    items: [..]Item;
    add_item(*items, "bolts", 500, 5);
    add_item(*items, "nuts", 800, 3);
    hammer := add_item(*items, "hammer", 4, 1299);
    add_item(*items, "saw", 2, 2450);
    add_item(*items, "glue", 0, 450);

    hammer.restock = true;  // pointer into the array (no growth since)
    print("% items, total value %\n", items.count, format_cents(total_value(items)));

    for * items {
        if it.quantity < 3 it.restock = true;
    }
    for items if it.restock print("restock: %\n", it.name);

    sort_by_value(items);
    for items print("%: % x % = %\n", it_index, it.name, format_cents(it.price_cents),
                    format_cents(it.quantity * it.price_cents));

    remove_unordered(*items, 0);
    print("after removing the first: % items:", items.count);
    for items print(" %", it.name);
    print("\n");

    // Grow far past the initial capacity.
    many: [..]int;
    for 1..1000 array_add(*many, it * it);
    print("many: count=% last=% allocated>=count: %\n", many.count, many[many.count - 1],
          many.allocated >= many.count);
    array_free(many);
}
