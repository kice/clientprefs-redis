#pragma once

#include "concurrentqueue.h"
#include "blockingconcurrentqueue.h"

/**
* Specification for a threaded Redis Query
*/
class IThreadQuery
{
public:
    /**
    * @brief Must return the object owning this threaded operation.
    * This is never called inside the thread.
    *
    * @return			IdentityToken_t pointer.
    */
    // virtual SourceMod::IdentityToken_t *GetOwner() = 0;

    virtual void SetDatabase(void *db) = 0;

    /**
    * @brief Called inside the thread; this is where any blocking
    * or threaded operations must occur.
    */
    virtual void RunThreadPart() = 0;

    /**
    * @brief Called in a server frame after the thread operation
    * has completed.  This is the non-threaded completion callback,
    * which although optional, is useful for pumping results back
    * to normal game API.
    */
    virtual void RunThinkPart() = 0;

    /**
    * @brief If RunThinkPart() is not called, this will be called
    * instead.  Note that RunThreadPart() is ALWAYS called regardless,
    * and this is only called when Core requests that the operation
    * be scrapped (for example, the database driver might be unloading).
    */
    virtual void CancelThinkPart() = 0;

    /**
    * @brief Called when the operation is finalized and any resources
    * can be released.
    */
    virtual void Destroy() = 0;
};

class TQueue
{
public:
    bool AddToThreadQueue(IThreadQuery *op, int prio)
    {
        return queryQueue.enqueue(op);
    }

    IThreadQuery *GetQuery()
    {
        IThreadQuery *op;
        queryQueue.wait_dequeue(op);
        return op;
    }

    bool PutResult(IThreadQuery *op)
    {
        return resultQueue.enqueue(op);
    }

    IThreadQuery *GetResult()
    {
        if (resultQueue.size_approx()) {
            IThreadQuery *op;
            if (resultQueue.try_dequeue(op)) {
                return op;
            }
        }
        return nullptr;
    }

    moodycamel::BlockingConcurrentQueue<IThreadQuery *> queryQueue;
    moodycamel::ConcurrentQueue<IThreadQuery *> resultQueue;
};
