// Heap data-structure exercise: a bump allocator over the brk syscall backs
// a binary search tree (32 keys inserted in a scrambling order; the in-order
// traversal must yield the sorted sequence) and a singly linked list (built,
// reversed, summed). Pointer chasing stresses the memory hierarchy, and the
// recursive walks stress call/return prediction. Returns a checksum of the
// two sums as the exit code.

#include "risc-e.h"

static char* heap_end;

static int heap_init(void) {
    heap_end = (char*)risc_e_brk(0);
    char* end = (char*)risc_e_brk((void*)(heap_end + 4096));
    return end >= heap_end + 4096 ? 0 : 1;
}

static void* heap_alloc(unsigned long size) {
    size = (size + 7u) & ~7u;  // keep 8-byte alignment
    char* p = heap_end;
    heap_end += size;
    risc_e_brk(heap_end);
    return p;
}

typedef struct Node {
    int key;
    struct Node* left;
    struct Node* right;
} Node;

static Node* bst_insert(Node* root, int key) {
    if (root == 0) {
        Node* n = (Node*)heap_alloc(sizeof(Node));
        n->key = key;
        n->left = 0;
        n->right = 0;
        return n;
    }
    if (key < root->key) {
        root->left = bst_insert(root->left, key);
    } else {
        root->right = bst_insert(root->right, key);
    }
    return root;
}

static void bst_walk(Node* root, int* out, int* count) {
    if (root == 0) return;
    bst_walk(root->left, out, count);
    out[(*count)++] = root->key;
    bst_walk(root->right, out, count);
}

typedef struct ListNode {
    int value;
    struct ListNode* next;
} ListNode;

static ListNode* list_build(int n) {
    ListNode* head = 0;
    for (int v = 1; v <= n; ++v) {
        ListNode* node = (ListNode*)heap_alloc(sizeof(ListNode));
        node->value = v;
        node->next = head;
        head = node;
    }
    return head;
}

static ListNode* list_reverse(ListNode* head) {
    ListNode* prev = 0;
    while (head != 0) {
        ListNode* next = head->next;
        head->next = prev;
        prev = head;
        head = next;
    }
    return prev;
}

static unsigned long list_sum(ListNode* head) {
    unsigned long sum = 0;
    while (head != 0) {
        sum += (unsigned long)head->value;
        head = head->next;
    }
    return sum;
}

int main(void) {
    if (heap_init() != 0) return 1;

    // Insert 0..31 in the scrambling order (i*7) % 32 so the tree is not
    // accidentally degenerate; the in-order walk must still yield 0..31.
    Node* root = 0;
    for (int i = 0; i < 32; ++i) root = bst_insert(root, (i * 7) % 32);

    int sorted[32];
    int count = 0;
    bst_walk(root, sorted, &count);

    int bst_ok = count == 32;
    unsigned long bst_sum = 0;
    for (int i = 0; i < count; ++i) {
        bst_sum += (unsigned long)sorted[i];
        if (i > 0 && sorted[i - 1] >= sorted[i]) bst_ok = 0;
    }
    if (!bst_ok) return 2;

    ListNode* list = list_reverse(list_build(16));
    const unsigned long list_total = list_sum(list);

    return (int)((bst_sum + list_total) % 256);
}
