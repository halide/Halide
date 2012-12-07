#ifndef INTRUSIVE_PTR_H
#define INTRUSIVE_PTR_H

namespace HalideInternal {

    template<typename T>
    struct IntrusivePtr {
    private:
        void incref() {
            if (ptr) {
                ptr->ref_count++;
            }
        };
        void decref() {
            if (ptr) {
                ptr->ref_count--;
                if (ptr->ref_count == 0) {
                    delete ptr;
                    ptr = NULL;
                }
            }
        }

    protected:
        T *ptr;

        ~IntrusivePtr() {
            decref();
        }

        IntrusivePtr() : ptr(NULL) {
        }

        IntrusivePtr(T *p) : ptr(p) {
            incref();
        }

        IntrusivePtr(const IntrusivePtr<T> &other) : ptr(other.ptr) {
            incref();
        }

        IntrusivePtr<T> &operator=(const IntrusivePtr<T> &other) {
            decref();
            ptr = other.ptr;
            incref();
            return *this;
        }

    public:
        /* Handles can be null. This checks that. */
        bool defined() const {
            return ptr;
        }

        /* Check if two handles point to the same ptr. This is
         * equality of reference, not equality of ptr. */
        bool same_as(const IntrusivePtr &other) {
            return ptr == other.ptr;
        }
    };
};

#endif
