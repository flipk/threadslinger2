
/** base type for use with pxfe_shared_ptr */
class pxfe_shared_ptr_base {
    template <typename T> friend class pxfe_shared_ptr;
    std::atomic_int __pxfe_sp_refcount;
public:
    pxfe_shared_ptr_base(void)
        : __pxfe_sp_refcount(0) { /*nothing*/ }
    virtual ~pxfe_shared_ptr_base(void) { /* nothing*/ }
    /** returns the current usage counter;
     * note this is only advisory because if there's more
     * than one thread holding this object, this value can
     * change. you can only really trust it if it's 1. */
    int use_count(void) const {
        return std::atomic_load(&__pxfe_sp_refcount);
    }
};

/** a shared pointer object, like std::shared_ptr but different.
 * 1. the counter is in the object itself (must be derived from
 *    pxfe_shared_ptr_base class) and so does not require a separate
 *    administrative data structure like std::shared_ptr does.
 * 2. has "give" and "take" methods so you can put an object into a
 *    shared_ptr and take an object out without destroying it.
 * 3. has casting methods so it's easy to manage heirarchicaly-derived
 *    classes, just copy-construct or assign, and if it comes out
 *    NULL, then it wasn't a polymorphic base type (dynamic_cast
 *    failed). */
template <class T>
class pxfe_shared_ptr {
    T * ptr;
    void ref(void)
    {
        if (ptr)
            ptr->__pxfe_sp_refcount ++;
    }
    void deref(void)
    {
        if (ptr && ptr->__pxfe_sp_refcount-- <= 1)
        {
            delete ptr;
            ptr = NULL;
        }
    }
public:
    /** normal constructor, adds a ref to the object */
    pxfe_shared_ptr<T>(T * _ptr = NULL)
    {
        ptr = _ptr;
        ref();
    }
    /** casting constructor, if dynamic_cast to the new type
     * succeeds, takes a ref, otherwise sets to empty/NULL */
    template <class BaseT>
    pxfe_shared_ptr<T>(const pxfe_shared_ptr<BaseT> &other)
    {
        ptr = dynamic_cast<T*>(*other);
        ref();
    }
    /** move constructor, transfers ownership */
    pxfe_shared_ptr<T>(pxfe_shared_ptr<T> &&other)
    {
        ptr = other.ptr;
        other.ptr = NULL;
    }
    /** destructor which derefs the object (deleting it if ref==0) */
    ~pxfe_shared_ptr<T>(void)
    {
        deref();
    }
    /** point this object to something else,
     *  deref the old and ref the new */
    void reset(T * _ptr = NULL)
    {
        deref();
        ptr = _ptr;
        ref();
    }
    /** give an object to this class for safe keeping; assumes
     * the reference count is already set properly and does not
     * change it (but does deref anything this class previously held) */
    void _give(T * _ptr)
    {
        deref();
        ptr = _ptr;
        // the caller is passing ownership to us,
        // presumably they had a refcount,
        // which they are giving to us,
        // so don't modify the refcount here.
    }
    /** takes an object away from this class (does not deref it) */
    T * _take(void)
    {
        T * ret = ptr;
        // we are letting the caller take ownership from us,
        // so the refcount we have is being given to them,
        // so don't modify the refcount here.
        ptr = NULL;
        return ret;
    }
    /** casting assignment operator, attempts dynamic_cast. if
     * casting fails, this object is now empty (NULL) */
    template <class BaseT>
    pxfe_shared_ptr<T> &operator=(const pxfe_shared_ptr<BaseT> &other)
    {
        deref();
        ptr = dynamic_cast<T*>(*other);
        ref();
        return *this;
    }
    /** move assignment operator, takes over ownership */
    pxfe_shared_ptr<T> &operator=(pxfe_shared_ptr<T> &&other)
    {
        deref();
        ptr = other.ptr;
        other.ptr = NULL;
        return *this;
    }
    /** if this is the only shared ptr referencing this object */
    bool unique(void) const {
        if (ptr)
            return (ptr->use_count() == 1);
        return false;
    }
    /** return if this is managing something (true) or empty (false).
     * useful for null checking just like a regular ptr, using the
     * syntax "if (sp)" */
    operator bool() const { return (ptr != NULL); }
    /** accessor that returns the pointer within */
    T * operator->(void) const { return ptr; }
    /** accessor that returns the pointer within */
    T * operator*(void) const { return ptr; }
    /** accessor that returns the pointer within */
    T * get(void) const { return ptr; }
};
