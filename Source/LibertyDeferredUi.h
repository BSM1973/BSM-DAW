#pragma once
// No JUCE object may be constructed during C++ static initialization.
// Registry nodes contain pointers only; real controllers are created on the
// JUCE message thread from JUCEApplication::initialise, and destroyed before
// JUCE's desktop and timer infrastructure is shut down.
namespace LibertyDeferredUi
{
struct Node;
inline Node* head = nullptr;
struct Node
{
    using Fn = void (*)(Node&);
    Node* next;
    Fn start, stop;
    bool early;
    Node(Fn s, Fn d, bool e) noexcept : next(head), start(s), stop(d), early(e)
    { head = this; }
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
};

template<class T> struct Deferred final : Node
{
    T* object = nullptr;
    explicit Deferred(bool earlyShutdown = false) noexcept
        : Node(&create, &destroy, earlyShutdown) {}
    T* operator->() const noexcept { return object; }
private:
    static void create(Node& n)
    {
        auto& self = static_cast<Deferred&>(n);
        if (self.object == nullptr) self.object = new T();
    }
    static void destroy(Node& n)
    {
        auto& self = static_cast<Deferred&>(n);
        delete self.object;
        self.object = nullptr;
    }
};

inline void startAll()
{
    for (auto* n = head; n != nullptr; n = n->next) n->start(*n);
}
inline void stopEarly()
{
    for (auto* n = head; n != nullptr; n = n->next) if (n->early) n->stop(*n);
}
inline void stopAll()
{
    for (auto* n = head; n != nullptr; n = n->next) n->stop(*n);
}
}
