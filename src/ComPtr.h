#pragma once

template <typename T> class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    T* get() const { return value_; }
    T** put() { reset(); return &value_; }
    T* operator->() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }
    void reset() { if (value_) { value_->Release(); value_ = nullptr; } }
private:
    T* value_ = nullptr;
};
