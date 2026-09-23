// data_structures.h
// Hand-written DSA containers. Every structure here is used for a real
// reason in scheduler.cpp -- see the README for the "why this structure"
// justification for each one. Nothing here exists just to tick a box.
#pragma once
#include <vector>
#include <functional>
#include <stdexcept>

// ---------------------------------------------------------------------
// Singly Linked List
// Used for: the master movie roster as loaded from input. We only ever
// append (load time) and walk it front-to-back (candidate generation),
// which is exactly what a singly linked list is for.
// ---------------------------------------------------------------------
template <typename T>
class SinglyLinkedList {
    struct Node {
        T data;
        Node* next;
        Node(const T& d) : data(d), next(nullptr) {}
    };
    Node* head = nullptr;
    Node* tail = nullptr;
    size_t count = 0;

public:
    ~SinglyLinkedList() { clear(); }

    void pushBack(const T& value) {          // O(1)
        Node* node = new Node(value);
        if (!tail) head = tail = node;
        else { tail->next = node; tail = node; }
        count++;
    }

    void forEach(const std::function<void(const T&)>& fn) const { // O(n)
        for (Node* cur = head; cur; cur = cur->next) fn(cur->data);
    }

    std::vector<T> toVector() const {         // O(n)
        std::vector<T> out;
        out.reserve(count);
        for (Node* cur = head; cur; cur = cur->next) out.push_back(cur->data);
        return out;
    }

    size_t size() const { return count; }
    bool empty() const { return count == 0; }

    void clear() {
        Node* cur = head;
        while (cur) { Node* nxt = cur->next; delete cur; cur = nxt; }
        head = tail = nullptr;
        count = 0;
    }
};

// ---------------------------------------------------------------------
// Doubly Linked List
// Used for: the chronological schedule of ONE screen. Shows must be kept
// sorted by start time (forward traversal = the visual timeline the
// frontend will render), and we need to walk backwards from an insertion
// point to check the previous show's end+turnaround without re-scanning
// the whole list from the head -- exactly the doubly-linked-list use case.
// ---------------------------------------------------------------------
template <typename T>
class DoublyLinkedList {
    struct Node {
        T data;
        Node* prev;
        Node* next;
        Node(const T& d) : data(d), prev(nullptr), next(nullptr) {}
    };
    Node* head = nullptr;
    Node* tail = nullptr;
    size_t count = 0;

public:
    ~DoublyLinkedList() { clear(); }

    // Insert keeping the list sorted by a caller-supplied key (start time).
    // O(n) to find the position -- lists here are tiny (shows per screen
    // per day, typically single digits), so this is the right trade-off
    // against the bookkeeping cost of a balanced tree.
    void insertSorted(const T& value, const std::function<int(const T&)>& keyOf) {
        Node* node = new Node(value);
        int key = keyOf(value);
        if (!head || keyOf(head->data) >= key) {
            node->next = head;
            if (head) head->prev = node;
            head = node;
            if (!tail) tail = node;
            count++;
            return;
        }
        Node* cur = head;
        while (cur->next && keyOf(cur->next->data) < key) cur = cur->next;
        node->next = cur->next;
        node->prev = cur;
        if (cur->next) cur->next->prev = node;
        else tail = node;
        cur->next = node;
        count++;
    }

    // Remove the most recently inserted node matching a predicate (used by
    // undo). O(n) worst case, fine for the tiny lists involved.
    bool removeLastMatching(const std::function<bool(const T&)>& pred) {
        for (Node* cur = tail; cur; cur = cur->prev) {
            if (pred(cur->data)) {
                if (cur->prev) cur->prev->next = cur->next; else head = cur->next;
                if (cur->next) cur->next->prev = cur->prev; else tail = cur->prev;
                delete cur;
                count--;
                return true;
            }
        }
        return false;
    }

    std::vector<T> toVector() const {
        std::vector<T> out;
        out.reserve(count);
        for (Node* cur = head; cur; cur = cur->next) out.push_back(cur->data);
        return out;
    }

    const T* lastBefore(int key, const std::function<int(const T&)>& keyOf) const {
        // Returns the last node whose key is < given key (used for overlap
        // checks against the immediately preceding show).
        const T* best = nullptr;
        for (Node* cur = head; cur && keyOf(cur->data) < key; cur = cur->next) best = &cur->data;
        return best;
    }

    size_t size() const { return count; }
    bool empty() const { return count == 0; }

    void clear() {
        Node* cur = head;
        while (cur) { Node* nxt = cur->next; delete cur; cur = nxt; }
        head = tail = nullptr;
        count = 0;
    }
};

// ---------------------------------------------------------------------
// Queue (FIFO), linked-list based.
// Used for: the sorted candidate list is fed into a queue and the greedy
// scheduler dequeues one candidate at a time -- this models "process
// candidates in ranked order" as an explicit processing queue rather than
// just an array index, which is the whole point of demonstrating a queue.
// ---------------------------------------------------------------------
template <typename T>
class Queue {
    struct Node { T data; Node* next; Node(const T& d): data(d), next(nullptr) {} };
    Node* head = nullptr;
    Node* tail = nullptr;
    size_t count = 0;

public:
    ~Queue() { clear(); }
    void enqueue(const T& value) {           // O(1)
        Node* node = new Node(value);
        if (!tail) head = tail = node;
        else { tail->next = node; tail = node; }
        count++;
    }
    T dequeue() {                            // O(1)
        if (!head) throw std::runtime_error("dequeue from empty queue");
        Node* node = head;
        T value = node->data;
        head = head->next;
        if (!head) tail = nullptr;
        delete node;
        count--;
        return value;
    }
    bool empty() const { return count == 0; }
    size_t size() const { return count; }
    void clear() { while (!empty()) dequeue(); }
};

// ---------------------------------------------------------------------
// Circular Queue (fixed-capacity, array-based).
// Used for: baseline scheduling rotates through the movie list "around and
// around" per screen until the operating day is full -- a genuine
// round-robin, which is exactly what a circular queue models. (No other
// part of the engine uses round-robin rotation, so this is the only place
// a circular queue appears.)
// ---------------------------------------------------------------------
template <typename T>
class CircularQueue {
    std::vector<T> buf;
    int cap, frontIdx = 0, backIdx = -1, cnt = 0;

public:
    explicit CircularQueue(int capacity) : buf(capacity), cap(capacity) {}

    bool isFull() const { return cnt == cap; }
    bool isEmpty() const { return cnt == 0; }

    void enqueue(const T& value) {           // O(1)
        if (isFull()) throw std::runtime_error("circular queue full");
        backIdx = (backIdx + 1) % cap;
        buf[backIdx] = value;
        cnt++;
    }
    T dequeue() {                            // O(1)
        if (isEmpty()) throw std::runtime_error("circular queue empty");
        T value = buf[frontIdx];
        frontIdx = (frontIdx + 1) % cap;
        cnt--;
        return value;
    }
    // Rotate: dequeue then immediately re-enqueue the same value -- this is
    // the "move to the back of the rotation" operation the baseline uses.
    T rotate() {
        T value = dequeue();
        enqueue(value);
        return value;
    }
    int size() const { return cnt; }
};

// ---------------------------------------------------------------------
// Stack (LIFO), vector-based.
// Used for: the history of accepted scheduling decisions, so the engine
// can undo the most recent decision, and so the explanation report can be
// printed most-recent-first without re-sorting anything.
// ---------------------------------------------------------------------
template <typename T>
class Stack {
    std::vector<T> data;
public:
    void push(const T& value) { data.push_back(value); }       // O(1) amortized
    T pop() {                                                    // O(1)
        if (data.empty()) throw std::runtime_error("pop from empty stack");
        T value = data.back();
        data.pop_back();
        return value;
    }
    const T& top() const { return data.back(); }
    bool empty() const { return data.empty(); }
    size_t size() const { return data.size(); }
    const std::vector<T>& raw() const { return data; }
};

// ---------------------------------------------------------------------
// Binary Search Tree, keyed by an int (movie ID).
// Used for: O(log n) average lookup of a movie by ID -- e.g. when the
// explanation report or a future frontend needs "give me movie #7"'s
// full record without a linear scan. No self-balancing (AVL/Red-Black) is
// implemented; this is stated plainly in the README as a fair,
// deliberate simplification, since the movie list is small (tens, not
// thousands, of movies) and near-random insertion order in practice.
// ---------------------------------------------------------------------
template <typename T>
class MovieBST {
    struct Node {
        int key;
        T data;
        Node* left = nullptr;
        Node* right = nullptr;
        Node(int k, const T& d) : key(k), data(d) {}
    };
    Node* root = nullptr;

    Node* insert(Node* node, int key, const T& data) {
        if (!node) return new Node(key, data);
        if (key < node->key) node->left = insert(node->left, key, data);
        else if (key > node->key) node->right = insert(node->right, key, data);
        else node->data = data; // duplicate key -> overwrite (validator should prevent this)
        return node;
    }
    const Node* search(const Node* node, int key) const {
        if (!node || node->key == key) return node;
        return key < node->key ? search(node->left, key) : search(node->right, key);
    }
    void inorder(const Node* node, std::vector<T>& out) const {
        if (!node) return;
        inorder(node->left, out);
        out.push_back(node->data);
        inorder(node->right, out);
    }
    void destroy(Node* node) {
        if (!node) return;
        destroy(node->left);
        destroy(node->right);
        delete node;
    }

public:
    ~MovieBST() { destroy(root); }
    void insert(int key, const T& data) { root = insert(root, key, data); } // O(log n) avg
    const T* find(int key) const {                                          // O(log n) avg
        const Node* n = search(root, key);
        return n ? &n->data : nullptr;
    }
    std::vector<T> inorderValues() const {                                   // O(n)
        std::vector<T> out;
        inorder(root, out);
        return out;
    }
};
