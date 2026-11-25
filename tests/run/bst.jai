#import "Basic";

// A binary search tree built with New, traversed recursively.

Node :: struct {
    key: int;
    count: int = 1;
    left: *Node;
    right: *Node;
}

insert :: (root: *Node, key: int) -> *Node {
    if root == null {
        n := New(Node);
        n.key = key;
        return n;
    }
    if key == root.key      root.count += 1;
    else if key < root.key  root.left = insert(root.left, key);
    else                    root.right = insert(root.right, key);
    return root;
}

walk :: (n: *Node, out: *[..]int) {
    if !n return;
    walk(n.left, out);
    for 1..n.count array_add(out, n.key);
    walk(n.right, out);
}

height :: (n: *Node) -> int {
    if !n return 0;
    return 1 + max(height(n.left), height(n.right));
}

contains :: (n: *Node, key: int) -> bool {
    while n {
        if key == n.key return true;
        n = ifx key < n.key then n.left else n.right;
    }
    return false;
}

destroy :: (n: *Node) -> int {
    if !n return 0;
    freed := destroy(n.left) + destroy(n.right) + 1;
    free(n);
    return freed;
}

main :: () {
    root: *Node;
    for int.[50, 30, 70, 20, 40, 60, 80, 30, 65, 20, 20] root = insert(root, it);

    sorted: [..]int;
    walk(root, *sorted);
    print("in order: %\n", sorted);
    print("height: %\n", height(root));
    print("contains 65: %, contains 66: %\n", contains(root, 65), contains(root, 66));
    print("root: key=% count=% left=% right=%\n", root.key, root.count, root.left.key, root.right.key);
    print("freed % nodes\n", destroy(root));
}
