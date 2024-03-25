#pragma once

namespace colite {
    namespace callable_detail {
        // 可调用目标包装器的基类
        template<typename R, typename... Args>
        class target {
        public:
            virtual ~target() = default;
            virtual void copy_to(void *ptr) const = 0;
            virtual void move_to(void *ptr) = 0;
            virtual auto operator()(Args&&... args) const -> R = 0;
        };

        // 可调用目标包装器的实现类
        template<typename C, typename R, typename... Args>
        class target_impl: public target<R, Args...> {
        public:
            explicit target_impl(const C& target): target(target) { }
            explicit target_impl(C&& target): target(std::move(target)) { }

            ~target_impl() override = default;

            void copy_to(void *ptr) const override {
                ::new (ptr) target_impl (target);
            }

            void move_to(void *ptr) override {
                ::new (ptr) target_impl (std::move(target));
            }

            auto operator()(Args&&... args) const -> R override {
                return std::invoke(target, std::forward<Args>(args)...);
            }

        private:
            C target;
        };
    }

    template<typename F, typename Alloc = colite::port::allocator>
    class callable;

    namespace callable_traits {
        template<typename F>
        struct target_traits;

        template<typename R, typename... Args>
        struct target_traits<R(Args...)> {
            using target_type = callable_detail::target<R, Args...>;

            template<typename C>
            static constexpr bool is_callable_target_of_it = std::is_invocable_r_v<R, C, Args...>;

            template<typename C>
            using target_impl_type = callable_detail::target_impl<C, R, Args...>;
        };

        template<typename T>
        struct is_callable: std::false_type { };

        template<typename F, typename Alloc>
        struct is_callable<callable<F, Alloc>>: std::true_type { };

        template<typename T>
        inline constexpr bool is_callable_v = is_callable<T>::value;
    }

    template<typename F, typename Alloc>
    class callable {
    public:
        using byte_allocator = std::allocator_traits<Alloc>::template rebind_alloc<std::byte>;
        using target_type = callable_traits::target_traits<F>::target_type;

        template<typename OtherF, typename OtherAlloc>
        friend class callable;

        explicit callable(const Alloc& alloc = {}): allocator(byte_allocator(alloc)) {  }

        // 必须约束 C 不为 callable<F, 任意类型>，且 C 可作为 F 的可调用对象，因为转发引用在重载决议的优先级中高于隐式转换，不这样做会覆盖掉拷贝和移动构造
        template<typename C>
            requires (!callable_traits::is_callable_v<std::remove_cvref_t<C>>)
                && callable_traits::target_traits<F>::template is_callable_target_of_it<std::remove_cvref_t<C>>
        callable(C&& fn, const Alloc& alloc = {}):
            allocator(byte_allocator(alloc))
        {
            using impl_type = typename callable_traits::target_traits<F>::template target_impl_type<C>;
            target_size = sizeof(impl_type);
            target_ptr = (target_type*)std::allocator_traits<byte_allocator>::allocate(allocator, target_size);
            ::new (target_ptr) impl_type(std::forward<C>(fn));
            target_deleter = [](void *ptr) { ((impl_type*)(ptr))->~impl_type(); };
        }

        ~callable() {
            if (target_ptr == nullptr) {
                return;
            }
            target_deleter(target_ptr);

            using byte_allocator_pointer = typename std::allocator_traits<byte_allocator>::pointer;
            std::allocator_traits<byte_allocator>::deallocate(allocator, (byte_allocator_pointer)target_ptr, target_size);
            target_ptr = nullptr;
        }

        callable(const callable& other):
            allocator(other.allocator),
            target_deleter(other.target_deleter),
            target_size(other.target_size)
        {
            if (!other) {
                return;
            }
            // 复制
            target_ptr = (target_type*)std::allocator_traits<byte_allocator>::allocate(allocator, target_size);
            other.target_ptr->copy_to(target_ptr);
        }

        template<typename OtherAlloc>
        callable(const callable<F, OtherAlloc>& other):
            target_size(other.target_size),
            target_deleter(other.target_deleter)
        {
            if (!other) {
                return;
            }
            // 复制但不复制分配器
            target_ptr = (target_type*)std::allocator_traits<byte_allocator>::allocate(allocator, target_size);
            other.target_ptr->copy_to(target_ptr);
        }

        callable(callable&& other) noexcept:
            allocator(std::move(other.allocator)),
            target_deleter(std::move(other.target_deleter)),
            target_size(std::move(other.target_size)),
            target_ptr(std::move(other.target_ptr))
        {
            // 使对方释放手中的对象，避免析构掉目标指针
            other.target_ptr = nullptr;
        }

        template<typename OtherAlloc>
        callable(callable<F, OtherAlloc>&& other):
            target_size(std::move(other.target_size)),
            target_deleter(std::move(other.target_deleter))
        {
            if (!other) {
                return;
            }
            // 不必管对方的指针，让对方正确析构掉被移动后的傀儡对象
            target_ptr = (target_type*)std::allocator_traits<byte_allocator>::allocate(allocator, target_size);
            other.target_ptr->move_to(target_ptr);
            other.~callable<F, OtherAlloc>();
        }

        callable& operator = (const callable& other) {
            if (this == std::addressof(other)) {
                return *this;
            }

            // 析构自己的原对象，复制新对象
            this->~callable();
            allocator = other.allocator;
            target_deleter = other.target_deleter;
            target_size = other.target_size;
            if (other) {
                target_ptr = (target_type*)std::allocator_traits<byte_allocator>::allocate(allocator, target_size);
                other.target_ptr->copy_to(target_ptr);
            } else {
                target_ptr = nullptr;
            }
            return *this;
        }

        template<typename OtherAlloc>
        callable& operator = (const callable<F, OtherAlloc>& other) {
            this->~callable();

            // 析构自己的原对象，复制新对象但不复制分配器
            target_size = other.target_size;
            target_deleter = other.target_deleter;
            if (other) {
                target_ptr = (target_type*)std::allocator_traits<byte_allocator>::allocate(allocator, target_size);
                other.target_ptr->copy_to(target_ptr);
            } else {
                target_ptr = nullptr;
            }
            return *this;
        }

        callable& operator = (callable&& other) noexcept {
            if (this == std::addressof(other)) {
                return *this;
            }
            swap(other);
            other.~callable();
            return *this;
        }

        template<typename OtherAlloc>
        callable& operator = (callable<F, OtherAlloc>&& other) {
            swap(std::move(other));
            other.~callable<F, OtherAlloc>();
            return *this;
        }

        void swap(callable& other) {
            std::swap(allocator, other.allocator);
            std::swap(target_deleter, other.target_deleter);
            std::swap(target_ptr, other.target_ptr);
            std::swap(target_size, other.target_size);
        }

        template<typename OtherAlloc>
        void swap(callable<F, OtherAlloc>& other) {
            // 将对方的 target，用自己的分配器分配并移动过来
            const auto temp_other_target_size = other.target_size;
            target_type* temp_other_target_ptr = nullptr;
            if (other) {
                temp_other_target_ptr = (target_type*)std::allocator_traits<byte_allocator>::allocate(allocator, temp_other_target_size);
                other.target_ptr->move_to(temp_other_target_ptr);
            }

            // 将自己的 target，用对方的分配器分配并移动过去
            const auto temp_this_target_size = target_size;
            using other_byte_allocator = typename callable<F, OtherAlloc>::byte_allocator;
            target_type* temp_this_target_ptr = nullptr;
            if (*this) {
                temp_this_target_ptr = (target_type*)std::allocator_traits<other_byte_allocator>::allocate(other.allocator, temp_this_target_size);
                target_ptr->move_to(temp_this_target_ptr);
            }

            // 将各自移动后的指针交给对方
            target_ptr = temp_other_target_ptr;
            target_size = temp_other_target_size;
            other.target_ptr = temp_this_target_ptr;
            other.target_size = temp_this_target_size;

            // 交换删除器
            auto temp_other_deleter = std::move(other.target_deleter);
            other.target_deleter = std::move(target_deleter);
            target_deleter = std::move(temp_other_deleter);
        }

        template<typename OtherAlloc>
        void swap(callable<F, OtherAlloc>&& other) {
            // 将对方的 target，用自己的分配器分配并移动过来，但不必把自己的 target 交给对方
            const auto temp_other_target_size = other.target_size;
            target_type* temp_other_target_ptr = nullptr;
            if (other) {
                temp_other_target_ptr = (target_type*)std::allocator_traits<byte_allocator>::allocate(allocator, temp_other_target_size);
                other.target_ptr->move_to(temp_other_target_ptr);
            }

            // 将移动后的指针交给自己。不必管对方的指针，让对方正确析构掉被移动后的傀儡对象
            target_ptr = temp_other_target_ptr;
            target_size = temp_other_target_size;
            target_deleter = std::move(other.target_deleter);

            other.~callable<F, OtherAlloc>();
        }

        template<typename... Args>
        auto operator()(Args&&... args) {
            return target_ptr->operator()(std::forward<Args>(args)...);
        }

        template<typename... Args>
        auto operator()(Args&&... args) const {
            return target_ptr->operator()(std::forward<Args>(args)...);
        }

        explicit operator bool() const {
            return target_ptr != nullptr;
        }

    private:
        byte_allocator allocator {};
        void (*target_deleter)(void*) = nullptr;

        target_type *target_ptr = nullptr;
        size_t target_size = 0;
    };
}