/*
 * Concurrency.h
 *
 *  Created on: Jan 12, 2015
 *      Author: Marek Wyborski
 */

#ifndef CONCURRENCY_H_
#define CONCURRENCY_H_

#include "Common.h"
#include <thread>
#include <future>

namespace SystemHelper {

namespace Future
{

template<typename R>
bool IsReady(std::future<R>& f)
{
    return f.valid() && (f.wait_for(std::chrono::seconds { 0 }) == std::future_status::ready);
}

template<typename R>
bool IsReady(std::shared_future<R>& f)
{
    return f.valid() && (f.wait_for(std::chrono::seconds { 0 }) == std::future_status::ready);
}

template<typename R>
std::string ToString(std::future<R>& f)
{
	std::string retVal;
    if (!f.valid())
        return std::string("not valid, no shared state.");
    switch (f.wait_for(std::chrono::seconds { 0 }))
    {
        case std::future_status::ready:
            return std::string("future_status = ready");
        case std::future_status::timeout:
            return std::string("future_status = timeout");
        case std::future_status::deferred:
            return std::string("future_status = deferred");
        default:
            throw std::runtime_error("Unknown future_status.");
    }
}

// Stroustrup
template<typename T>
int wait_for_any(std::vector<std::future<T>>& vf, std::chrono::steady_clock::duration d)
// return index of ready future
// if no future is ready, wait for d before trying again
{
    while (true)
    {
        for (int i = 0; i != vf.size(); ++i)
        {
            if (!vf[i].valid())
                continue;
            switch (vf[i].wait_for(std::chrono::seconds { 0 }))
            {
                case std::future_status::ready:
                    return i;
                case std::future_status::timeout:
                    break;
                case std::future_status::deferred:
                    throw std::runtime_error("wait_for_any(): deferred future");
            }
        }
        std::this_thread::sleep_for(d);
    }
}

// Herb Sutter
template<typename Fut, typename Work>
auto then(Fut f, Work w) -> std::future<decltype(w(f.get()))>
{
    return std::async([=]
    {   w(f.get());});
}

}

namespace Concurrency
{

template<typename F>
void parallel_for(int start, int end, F f, int num_threads = std::thread::hardware_concurrency())
{
    int count = end - start;
    int countPerThread = count / num_threads;
    int rest = count % num_threads;

    std::vector<std::future<void>> futures {};

    for (int t = 0; t < num_threads; t++)
    {
        int start_t, end_t;
        if (t < rest)
        {
            int cpt = countPerThread + 1;
            start_t = (t * cpt);
            end_t = (t + 1) * cpt;
        }
        else
        {
            start_t = (t * countPerThread) + rest;
            end_t = ((t + 1) * countPerThread) + rest;
        }
        futures.push_back(std::async(std::launch::async, [=]()
        {
            for(int i = start_t; i < end_t; i++)
            f(i);
        }));
    }

    for (int fu = 0; fu < futures.size(); fu++)
    {
        futures[fu].get();
    }
}

//http://stackoverflow.com/questions/14650885/how-to-create-timer-events-using-c-11
class later
{
public:
    template<class callable, class ... arguments>
    later(std::chrono::steady_clock::duration durationToWait, bool async, callable&& f, arguments&&... args)
    {
        std::function<typename std::result_of<callable(arguments...)>::type()> task(
                std::bind(std::forward<callable>(f), std::forward<arguments>(args)...));

        if (async)
        {
            std::thread([durationToWait, task]()
            {
                std::this_thread::sleep_for(durationToWait);
                task();
            }).detach();
        }
        else
        {
            std::this_thread::sleep_for(durationToWait);
            task();
        }
    }

};

class timer
{
private:
	std::function<void()> tickFunction;
	std::chrono::steady_clock::duration sleepTime;
	std::chrono::steady_clock::duration waitOnStart;
	std::thread thd;bool done;

public:
    timer(std::function<void()> function, std::chrono::steady_clock::duration interval,
    		std::chrono::steady_clock::duration startDelay = std::chrono::seconds(0)):
    			tickFunction(function), sleepTime(interval), waitOnStart(startDelay), thd(), done(false)
	{};

    ~timer()
    {
    	    done = true;
    	    thd.detach();
    }

    bool Start()
    {
    	    if (!thd.joinable())
    	    {
    	        done = false;
    	        thd = std::thread([=]()
    	        {
    	        	std::this_thread::sleep_for(waitOnStart);
    	            while(!done)
    	            {
    	                tickFunction();
    	                std::this_thread::sleep_for(sleepTime);
    	            }
    	        });
    	        return true;
    	    }
    	    else
    	        return false;
    }

    bool Stop()
    {
    	    if (thd.joinable())
    	    {
    	        done = true;
    	        return true;
    	    }
    	    else
    	        return false;
    }

    bool IsRunning()
    {
    	return thd.joinable() && !done;
    }
};

// Herb Sutter
template<typename T> class monitor
{
private:
    mutable T t;
    mutable std::mutex m;
public:
    monitor(T t_ = T {}) :
            t { t_ }, m {}
    {
    }

    template<typename F>
    auto lock(F f) const -> decltype(f(t))
    {
    	std::lock_guard<std::mutex> lg { m };
        return f(t);
    }
};

//  Monitor for shared_ptr object
template<typename T> class monitor_shared
{
private:
    mutable std::shared_ptr<T> t;
    mutable std::mutex m;
public:
    monitor_shared(std::shared_ptr<T> t_ = std::make_shared<T>()) :
            t { t_ }, m()
    {
    }

    template<typename F>
    auto lock(F f) const -> decltype(f(*t))
    {
    	std::lock_guard<std::mutex> lg { m };
        return f(*t);
    }

    std::shared_ptr<T> getSharedPtr() const
    {
        return t;
    }
};

// Erweiterung Monitor Template um unique_ptr
/*
 * Beispiel:
 * shared_ptr<monitor_unique<TestClass>> MonitoredTestClass = make_shared<monitor_unique<TestClass>>(unique_ptr<TestClass>(new TestClass("blabla unique")));
 *
 * MonitoredTestClass->exec([](TestClass& tc)
 * 	{
 * 		cout << "tc: " << tc.ToString() << endl;
 * 	});

 */
// Fuer Objekte die keinen Copy Constructor besitzen
template<typename T>
class monitor_unique
{
private:
    mutable std::unique_ptr<T> t;
    mutable std::mutex m;
public:
    monitor_unique(std::unique_ptr<T> t_ = std::unique_ptr<T>(new T())) :
            t(std::move(t_)), m()
    {
    }

    template<typename F>
    auto lock(F f) const -> decltype(f(*t))
    {
    	std::lock_guard<std::mutex> lg { m };
        return f(*t);
    }
};

/* https://sites.google.com/site/kjellhedstrom2/active-object-with-cpp0x
 *  Multiple producer, multiple consumer thread safe queue
 *  Since 'return by reference' is used this queue won't throw */
template<typename T>
class shared_queue
{
	std::queue<T> queue_;
    mutable std::mutex m_;
    std::condition_variable data_cond_;

    shared_queue& operator=(const shared_queue&) = delete;
    shared_queue(const shared_queue& other) = delete;

public:
    shared_queue() :
            queue_ {}, m_ {}, data_cond_ {}
    {
    }

    void push(T item)
    {
    	std::lock_guard<std::mutex> lock(m_);
        queue_.push(item);
        data_cond_.notify_one();
    }

    /// \return immediately, with true if successful retrieval
    bool try_and_pop(T& popped_item)
    {
    	std::lock_guard<std::mutex> lock(m_);
        if (queue_.empty())
        {
            return false;
        }
        popped_item = queue_.front();
        queue_.pop();
        return true;
    }

    /// Try to retrieve, if no items, wait till an item is available and try again
    void wait_and_pop(T& popped_item)
    {
    	std::unique_lock<std::mutex> lock(m_); // note: unique_lock is needed for condition_variable::wait
        while (queue_.empty())
        { //                       The 'while' loop below is equal to
            data_cond_.wait(lock); //data_cond_.wait(lock, [](bool result){return !queue_.empty();});
        }
        popped_item = queue_.front();
        queue_.pop();
    }

    bool empty() const
    {
    	std::lock_guard<std::mutex> lock(m_);
        return queue_.empty();
    }

    unsigned size() const
    {
    	std::lock_guard<std::mutex> lock(m_);
        return queue_.size();
    }
};

// Herb Sutter
// http://channel9.msdn.com/Shows/Going+Deep/C-and-Beyond-2012-Herb-Sutter-Concurrency-and-Parallelism
// active_object<monitor_shared<tcp::socket>> active_monitored_tcpSocket =
// new active_object<monitor_shared<tcp::socket>>(unique_ptr<monitor_shared<tcp::socket>>( new monitor_shared<tcp::socket>(tcpClient_ )))
template<class T> class active_object
{
private:
    mutable std::unique_ptr<T> t;
    mutable shared_queue<std::function<void()>> q;
    bool done;
    std::thread thd;

public:
    active_object(std::unique_ptr<T> t_ = std::unique_ptr<T>(new T())) :
            t { std::move(t_) }, q {}, done { false }, thd { [=]()
            {
                while(!done)
                {
                	std::function<void()> function;
                    q.wait_and_pop(function);
                    function();
                }
            } }
    {
    }

    ~active_object()
    {
        q.push([=]
        {   done = true;});
        thd.join();
    }

    template<typename F>
    auto addJob(F f) const -> std::future<decltype(f(*t))>
    {
        auto p = std::make_shared<std::promise<decltype(f(*t))>>();
        auto ret = p->get_future();
        q.push([=]
        {
            try
            {   set_value(*p,f,*t);}
            catch (...)
            {   p->set_exception(std::current_exception());}
        });
        return ret;
    }

    unsigned getNumJobs() const
    {
        return q.size();
    }

    template<typename F>
    void set_value(std::promise<void>& p, F& f, T& t) const
    {
        f(t);
        p.set_value();
    }

    template<typename Fut, typename F>
    void set_value(std::promise<Fut>& p, F& f, T& t) const
    {
        p.set_value(f(t));
    }
};

// example initialization:
//
// active_object_parallel<monitor<string>> active_string {unique_ptr<monitor<string>>( new monitor<string>() ) };
//
template<class T>
class active_object_parallel
{
private:
    mutable std::unique_ptr<T> t;
    mutable shared_queue<std::function<void()>> q;
    bool done;
    std::vector<std::thread> threadList;

public:
    active_object_parallel(std::unique_ptr<T> t_ = std::unique_ptr<T>(new T()), uint32_t numParallelWorkerThreads =
    		std::thread::hardware_concurrency()) :
            t { move(t_) }, q {}, done { false }, threadList {}
    {
        if (numParallelWorkerThreads < 1)
            throw std::runtime_error("active_object_parallel needs at least 1 WokerThread");

        threadList.reserve(numParallelWorkerThreads);
        for (int i = 0; i < numParallelWorkerThreads; i++)
            threadList.push_back(std::thread { [=]
            {
                while(!done)
                {
                	std::function<void()> function;
                    q.wait_and_pop(function);
                    function();
                }
            } });
    }

    ~active_object_parallel()
    {
        // This will finish only one thread, as the others will still be waiting
        q.push([=]
        {   done = true;});

        // So finish the others by signalling with an empty lambda
        for (size_t i = 0; i < threadList.size() - 1; i++)
            q.push([]
            {});

        for (size_t i = 0; i < threadList.size(); i++)
            threadList[i].join();
    }

    template<typename F>
    auto addJob(F f) const -> std::future<decltype(f(*t))>
    {
        auto p = std::make_shared<std::promise<decltype(f(*t))>>();
        auto ret = p->get_future();
        q.push([=]
        {
            try
            {   set_value(*p,f,*t);}
            catch (...)
            {   p->set_exception(std::current_exception());}
        });
        return ret;
    }

    unsigned getNumJobs() const
    {
        return q.size();
    }

    template<typename F>
    void set_value(std::promise<void>& p, F& f, T& t) const
    {
        f(t);
        p.set_value();
    }

    template<typename Fut, typename F>
    void set_value(std::promise<Fut>& p, F& f, T& t) const
    {
        p.set_value(f(t));
    }
};

}

} // End SystemHelperFunctions namespace



#endif /* CONCURRENCY_H_ */
