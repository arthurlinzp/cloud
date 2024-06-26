#include <thread>
#include <vector>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>

class CThreadPool {
public:
    CThreadPool(size_t v_numThreads) : stop_(false) {
        for (size_t i = 0; i < v_numThreads; ++i) {
			std::shared_ptr<std::thread> tempThread = std::make_shared<std::thread>(std::bind(&CThreadPool::addthread, this));
			workers_.emplace_back(tempThread);
        }
    }

    template<class F>
	void enqueue(F&& task) {
		std::unique_lock<std::mutex> lock(queueMutex_);
		tasks_.emplace(std::forward<F>(task));
		lock.unlock();
		condition_.notify_one();
	}

    void waitAll() {
		for (std::shared_ptr<std::thread>& worker : workers_){
			if (NULL != worker){
				worker->join();
				worker.reset();
			}
		}
    }

	void release(){
		std::unique_lock<std::mutex> lock(queueMutex_);
		stop_ = true;
		lock.unlock();
		condition_.notify_all();
		waitAll();
	}


    ~CThreadPool() {
		
    }

private:
    std::vector<std::shared_ptr<std::thread>> workers_;
    std::queue<std::function<void()>> tasks_;

    std::mutex queueMutex_;
    std::condition_variable condition_;
    bool stop_;

private:
	void addthread()
	{
		while (!stop_) {
			std::function<void()> task;
			std::unique_lock<std::mutex> lock(queueMutex_);
			condition_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
			if (stop_ && tasks_.empty())
				return;
			task = std::move(tasks_.front());
			tasks_.pop();
			lock.unlock();
			task();
		}
	}
};