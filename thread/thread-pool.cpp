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

#include "thread-pool.h"
#include "thread-key.h"
#include "../common/alog.h"

namespace photon
{
    TPControl* ThreadPoolBase::thread_create_ex(thread_entry start, void* arg, bool joinable)
    {
        auto pCtrl = B::get();   // 没有会调用ctor构造  ThreadPoolBase::ctor;保存的是TPControl指针
        {
            SCOPED_LOCK(pCtrl->m_mtx);
            pCtrl->joinable = joinable;   // 默认false
            pCtrl->joining = false;
            pCtrl->start = start;
            pCtrl->arg = arg;
            pCtrl->cvar.notify_one();   // 有挂起的先唤醒
        }
        return pCtrl;
    }
    bool ThreadPoolBase::wait_for_work(TPControl &ctrl)
    {
        SCOPED_LOCK(ctrl.m_mtx);
        while (!ctrl.start)                     // wait for `create()` to give me
            ctrl.cvar.wait(ctrl.m_mtx);           // thread_entry and argument

        if (ctrl.start == &stub)   // 标记结束
            return false;

        ((partial_thread*) CURRENT)->tls = nullptr;  // 清除
        return true;
    }
    bool ThreadPoolBase::after_work_done(TPControl &ctrl)
    {
        SCOPED_LOCK(ctrl.m_mtx);
        auto ret = !ctrl.joining;
        if (ctrl.joining) {   //
            assert(ctrl.joinable);
            ctrl.cvar.notify_all();
        } else if (ctrl.joinable) {
            ctrl.joining = true;
            ctrl.cvar.wait(ctrl.m_mtx);
        }
        ctrl.joinable = false;
        ctrl.joining = false;
        ctrl.start = nullptr;
        return ret;  // 默认返回true
    }
    void* ThreadPoolBase::stub(void* arg)
    {
        TPControl ctrl;   // 协程结束自动析构
        auto th = *(thread**)arg;   // th指向current
        *(TPControl**)arg = &ctrl;              // tell ctor where my `ctrl` is
        thread_yield_to(th);
        while(wait_for_work(ctrl))   // ctrl已经被设置
        {
            ctrl.start(ctrl.arg);    // 执行
            deallocate_tls();
            auto should_put = after_work_done(ctrl);
            if (should_put) {
                // if has no other joiner waiting
                // collect it into pool
                ctrl.pool->put(&ctrl);   // 超过容量会析构协程
            }
        }
        return nullptr;
    }
    bool ThreadPoolBase::do_thread_join(TPControl* pCtrl)
    {
        SCOPED_LOCK(pCtrl->m_mtx);
        if (!pCtrl->joinable)
            LOG_ERROR_RETURN(EINVAL, false, "thread is not joinable");
        if (!pCtrl->start)
            LOG_ERROR_RETURN(EINVAL, false, "thread is not running");
        if (pCtrl->start == &stub)
            LOG_ERROR_RETURN(EINVAL, false, "thread is dying");

        auto ret = !pCtrl->joining;
        if (pCtrl->joining) {
            pCtrl->cvar.notify_one();
        } else {
            pCtrl->joining = true;
            pCtrl->cvar.wait(pCtrl->m_mtx);
        }
        return ret;
    }
    void ThreadPoolBase::join(TPControl* pCtrl)
    {
        auto should_put = do_thread_join(pCtrl);
        if (should_put)
            pCtrl->pool->put(pCtrl);
    }
    int ThreadPoolBase::ctor(ThreadPoolBase* pool, TPControl** out)  // 二级指针的使用
    {
        auto pCtrl = (TPControl*)CURRENT;
        auto stack_size = (uint64_t)pool->m_reserved;
        auto th = photon::thread_create(&stub, &pCtrl, stack_size);  // 在当前线程创建协程
        thread_yield_to(th);
        assert(pCtrl);   // 指向stub函数中的局部变量
        *out = pCtrl;
        pCtrl->th = th;
        pCtrl->pool = pool;
        pCtrl->start = nullptr;    // ThreadPoolBase::stub会卡住
        return 0;
    }
    int ThreadPoolBase::dtor(ThreadPoolBase* tpb, TPControl* pCtrl)
    {
        {
            SCOPED_LOCK(pCtrl->m_mtx);
            if (pCtrl->start) {     // it's running
                assert(pCtrl->start != &stub);
                pCtrl->joinable = true;
                pCtrl->m_mtx.unlock();
                tpb->join(pCtrl);
                pCtrl->m_mtx.lock();
            }
            pCtrl->start = &stub;    // 标记析构
            pCtrl->cvar.notify_all();
        }
        thread_yield();  // 切换到下一个协程执行
        return 0;
    }
}
