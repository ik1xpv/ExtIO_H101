#if !defined FIFOUCHAR_H
#define FIFOUCHAR_H
#include <queue>
#include "windows.h"

class Mutex
{
    friend class Lock;
public:
    Mutex () { InitializeCriticalSection (&_critSection); }
    ~Mutex () { DeleteCriticalSection (&_critSection); }
private:
    void Acquire () 
    { 
        EnterCriticalSection (&_critSection);
    }
    void Release () 
    { 
        LeaveCriticalSection (&_critSection);
    }

    CRITICAL_SECTION _critSection;
};

class Lock 
{
public:
	// Acquire the state of the semaphore
	Lock ( Mutex& mutex ) 
		: _mutex(mutex) 
	{
		_mutex.Acquire();
	}
	// Release the state of the semaphore
	~Lock ()
	{
		_mutex.Release();
	}
private:
	Mutex& _mutex;
};


class FifoUchar
{
public:
	FifoUchar(){;}
	virtual	~FifoUchar(){};	
	void sampleIn(unsigned char pin)
	{
		Lock lock (_mutex);;
			_queue.push(pin);
	}
	bool sampleOut(unsigned char* pout)
	{
	Lock lock (_mutex);
	if (_queue.size() < 1) 
		return false;
	*pout =_queue.front();
	_queue.pop();
	return true;
	}
	int	 size()
	{
	return _queue.size();
	}
	void clear(void)
	{
		while (!_queue.empty()) _queue.pop();
	}


private:
	std::queue<unsigned char> _queue;
	Mutex		_mutex;
};


#endif FIFOUCHAR
