#pragma once

template <typename T> class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    T** put() { reset(); return &value_; }
    T* operator->() const { return value_; }
    void reset() { if (value_) { value_->Release(); value_ = nullptr; } }
private:
    T* value_ = nullptr;
};
