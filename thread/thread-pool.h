/*
Copyright 2022 The Photon Authors

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#pragma once
#include <photon/thread/thread.h>
#include <photon/common/identity-pool.h>

namespace photon
{
    class ThreadPoolBase;

    struct TPControl
    {
        thread* th;
        ThreadPoolBase* pool;
        thread_entry start;
        void* arg;
        condition_variable cvar;
        bool joinable, joining;
        photon::spinlock m_mtx;
    };

    class ThreadPoolBase : protected IdentityPool0<TPControl>
    {
    public:
        using IdentityPool0<TPControl>::enable_autoscale;
        using IdentityPool0<TPControl>::disable_autoscale;
        thread* thread_create(thread_entry start, void* arg)
        {
            return thread_create_ex(start, arg)->th;
        }

        // returns a TPControl* that can be used for join; need not be deleted;
        TPControl* thread_create_ex(thread_entry start, void* arg, bool joinable = false);

        void join(TPControl* pCtrl);   // joinable = false  不应该主动调用

        static ThreadPoolBase* new_thread_pool(
            uint32_t capacity, uint64_t stack_size = DEFAULT_STACK_SIZE)
        {
            auto p = B::new_identity_pool(capacity);   // 设定容量
            auto pool = (ThreadPoolBase*) p;
            pool->init(stack_size);   // 设置构造析构函数和栈大小
            return pool;
        }

        static void delete_thread_pool(ThreadPoolBase* p)
        {
            B::delete_identity_pool((B*)p);
        }

    protected:
        typedef IdentityPool0<TPControl> B;
        static void* stub(void* arg);
        static int ctor(ThreadPoolBase*, TPControl**);   // static
        static int dtor(ThreadPoolBase*, TPControl*);
        static bool wait_for_work(TPControl &ctrl);
        static bool after_work_done(TPControl &ctrl);
        static bool do_thread_join(TPControl* pCtrl);
        void init(uint64_t stack_size)
        {
            set_ctor({this, &ctor});   // 设置构造析构
            set_dtor({this, &dtor});
            m_reserved = (void*)stack_size;
        }
        ThreadPoolBase(uint32_t capacity, uint64_t stack_size) : B(capacity)
        {
            init(stack_size);
        }
        // ThreadPoolBase should destruct by calling delete_thread_pool
        // delete ThreadPoolBase* is not allowed, so dtor is protected
        ~ThreadPoolBase() {}
    };

    inline ThreadPoolBase* new_thread_pool(
        uint32_t capacity, uint64_t stack_size = DEFAULT_STACK_SIZE)
    {
        return ThreadPoolBase::new_thread_pool(capacity, stack_size);
    }

    inline void delete_thread_pool(ThreadPoolBase* p)
    {
        ThreadPoolBase::delete_thread_pool(p);
    }


    template<uint32_t CAPACITY>    // 非类型模板参数
    class ThreadPool : public ThreadPoolBase
    {
    public:
        ThreadPool(uint64_t stack_size = DEFAULT_STACK_SIZE) :
            ThreadPoolBase(CAPACITY, stack_size) { }

    protected:
        thread* m_threads[CAPACITY];
    };

    inline void* __example_of_thread_pool__(void*)
    {
        auto p1 = ThreadPoolBase::new_thread_pool(100);
        auto th1 = p1->thread_create(&__example_of_thread_pool__, nullptr);
        (void)th1;

        ThreadPool<400> p2;
        auto th2 = p2.thread_create(&__example_of_thread_pool__, nullptr);
        return th2;
    }
}
