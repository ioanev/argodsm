/**
 * @file
 * @brief this is a legacy file from the ArgoDSM prototype
 * @copyright Eta Scale AB. Licensed under the Eta Scale Open Source License. See the LICENSE file for details.
 * @deprecated this file is legacy and will be removed as soon as possible
 * @warning do not rely on functions from this file
 */

#ifndef argo_swdsm_h
#define argo_swdsm_h argo_swdsm_h

/* Includes */
#include <cstdint>
#include <type_traits>
#include <mutex>
#include <functional>
#include <cmath>
#include <vector>
#include <atomic>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <math.h>
#include <mpi.h>
#include <pthread.h>
#include <omp.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "argo.h"

/** @brief Granularity of coherence unit / pagesize  */
#define GRAN 4096L //page size.

#ifndef CACHELINE
/** @brief Size of a ArgoDSM cacheline in number of pages */
#define CACHELINE 1L
#endif

#ifndef NUM_THREADS
/** @brief Number of maximum local threads in each node */
/**@bug this limits amount of local threads because of pthread barriers being initialized at startup*/
#define NUM_THREADS 128
#endif

/** @brief Read a value and always get the latest - 'Read-Through' */
#ifdef __cplusplus
#define ACCESS_ONCE(x) (*static_cast<std::remove_reference<decltype(x)>::type volatile *>(&(x)))
#else
#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))
#endif

/** @brief Hack to avoid warnings when you have unused variables in a function */
#define UNUSED_PARAM(x) (void)(x)

/** @brief Wrapper for unsigned char - basically a byte */
typedef unsigned char argo_byte;

/** @brief Struct for cache control data */
typedef struct myControlData //global cache control data / directory
{
		/** @brief Coherence state, basically only Valid/Invalid now */
		argo_byte state;    //I/P/SW/MW
		/** @brief Tracks if page is dirty or clean */
		argo_byte dirty;   //Is this locally dirty?  
		/** @brief Tracks address of page */
		unsigned long tag;   //addres of global page in distr mem
} control_data;

/** @brief Struct containing statistics */
typedef struct argo_statistics_struct
{
		/** @brief Time spent self invalidating */
		double selfinvtime;
		/** @brief Time spent loading pages */
		double load_time;
		/** @brief Mutex to update load_time */
		std::mutex load_time_mutex;
		/** @brief Time spent storing pages */
		double store_time;
		/** @brief Mutex to update store_time */
		std::mutex store_time_mutex;
		/** @brief Time spent locking the sync lock */
		double sync_lock_time;
		/** @brief Mutex to update sync_lock_time */
		std::mutex sync_lock_time_mutex;
		/** @brief Time spent in global barrier */
		double barriertime; 
		/** @brief Time spent initializing ArgoDSM */
		double inittime;
		/** @brief Time between init and finalize */
		double exectime;
		/** @brief Number of stores */
		std::atomic<std::size_t> write_misses;
		/** @brief Number of loads */
		std::atomic<std::size_t> read_misses;
		/** @brief Number of barriers executed */
		unsigned long barriers; 
		/** @brief Number of locks */
		int locks;
		/** @brief Time spent performing selective acquire */
		double ssi_time;
		/** @brief Mutex to update ssi_time */
		std::mutex ssi_time_mutex;
		/** @brief Time spent performing selective release */
		double ssd_time;
		/** @brief Mutex to update ssd_time */
		std::mutex ssd_time_mutex;
} argo_statistics;

/**
 * @brief Simple lock struct
 * @todo  This should be done in a separate cache class
 */
class alignas(64) cache_lock {
	private:
		/** 
		 * @brief Mutex protecting one cache block 
		 * @todo This should be used through std::scoped_lock
		 * in the future, but this requires minimum C++17.
		 */
		std::mutex c_mutex;

		/** @brief Time spent waiting for lock */
		double wait_time;

		/** @brief Time spent holding the lock */
		double hold_time;

		/** @brief For timekeeping hold time */
		double acquire_time;

		/** @brief Number of times held */
		std::size_t num_locks;

	public:
		/**
		 * @brief Constructor
		 */
		cache_lock()
			: wait_time(0),
			hold_time(0),
			acquire_time(0),
			num_locks(0)
		{ };

		/**
		 * @brief Copy constructor
		 * @param _other cache_lock to copy from
		 */
		cache_lock( const cache_lock& _other )
			: wait_time(_other.wait_time),
			hold_time(_other.hold_time),
			acquire_time(_other.acquire_time),
			num_locks(_other.num_locks)
		{ };

		/**
		 * @brief Move constructor
		 * @param _other cache_lock to move
		 */
		cache_lock( const cache_lock&& _other )
			: wait_time(std::move(_other.wait_time)),
			hold_time(std::move(_other.hold_time)),
			acquire_time(std::move(_other.acquire_time)),
			num_locks(std::move(_other.num_locks))
		{ };

		/** @brief Destructor */
		~cache_lock() { };

		/** @brief Acquire a cache lock */
		void lock(){
			double start = MPI_Wtime();
			c_mutex.lock();
			double end = MPI_Wtime();
			wait_time += end-start;
			acquire_time = end;
			num_locks++;
		}

		/**
		 * @brief Attempt to acquire a cache lock
		 * @return True if successful, else false
		 */
		bool try_lock(){
			return c_mutex.try_lock();
		}

		/** @brief Release a cache lock */
		void unlock(){
			hold_time += MPI_Wtime()-acquire_time;
			c_mutex.unlock();
		}

		/**
		 * @brief	Get the time spent waiting for the lock
		 * @return	The time in seconds
		 */
		double get_lock_time() const {
			return wait_time;
		}

		/**
		 * @brief	Get the time spent holding the lock
		 * @return	The time in seconds
		 */
		double get_hold_time() const {
			return hold_time;
		}

		/**
		 * @brief	Get the number of times this lock was held
		 * @return	The number of times this lock was held
		 */
		std::size_t get_num_locks() const {
			return num_locks;
		}

		/**
		 * @brief Clear all recorded statistics
		 */
		void reset_stats(){
			wait_time = 0;
			hold_time = 0;
			num_locks = 0;
		}
};

/*constants for control values*/
/** @brief Constant for invalid states */
static const argo_byte INVALID=0;
/** @brief Constant for valid states */
static const argo_byte VALID=1;
/** @brief Constant for clean states */
static const argo_byte CLEAN=2;
/** @brief Constant for dirty states */
static const argo_byte DIRTY=3;
/** @brief Constant for writer states */
static const argo_byte WRITER=4;
/** @brief Constant for reader states */
static const argo_byte READER=5;

/**
 * @brief The size of a hardware memory page
 * @note  This should be better centralized for all
 *        modules and backend implementations
 */
constexpr std::size_t page_size = 4096;

/*Handler*/
/**
 * @brief Catches memory accesses to memory not yet cached in ArgoDSM. Launches remote requests for memory not present.
 * @param sig unused param
 * @param si contains information about faulting instruction such as memory address
 * @param context the context used when the signal was received
 * @see signal.h
 */
void handler(int sig, siginfo_t *si, void *context);
/**
 * @brief Sets up ArgoDSM's signal handler
 */
void set_sighandler();

/*ArgoDSM init and finish*/
/**
 * @brief Initializes ArgoDSM runtime
 * @param argo_size Size of wanted global address space in bytes
 * @param cache_size Size in bytes of your cache, will be rounded to nearest multiple of cacheline size (in bytes)
 */
void argo_initialize(std::size_t argo_size, std::size_t cache_size);

/**
 * @brief Shutting down ArgoDSM runtime
 */
void argo_finalize();

/*Global alloc*/
/**
 * @brief Allocates memory in global address space
 * @param size amount of memory to allocate
 * @return pointer to allocated memory or NULL if failed.
 */
void * argo_gmalloc(unsigned long size);

/*Synchronization*/

/**
 * @brief Self-Invalidates all memory that has potential writers
 */
void self_invalidation();

/**
 * @brief Global barrier for ArgoDSM - needs to be called by every thread in the
 *        system that need coherent view of the memory
 * @param n number of local thread participating
 */
void swdsm_argo_barrier(int n);

/**
 * @brief acquire function for ArgoDSM (Acquire according to Release Consistency)
 */
void argo_acquire();
/**
 * @brief Release function for ArgoDSM (Release according to Release Consistency)
 */
void argo_release();

/**
 * @brief acquire-release function for ArgoDSM (Both acquire and release
 *        according to Release Consistency)
 */
void argo_acq_rel();

/**
 * @brief stores a page remotely - only writing back what has been written locally since last synchronization point
 * @param index index in local page cache
 * @param addr address to page in global address space
 */
void storepageDIFF(unsigned long index, unsigned long addr);

/*Statistics*/
/**
 * @brief Resets all ArgoDSM statistics
 */
void argo_reset_stats();

/**
 * @brief wrapper for MPI_Wtime();
 * @return Wall time
 */
double argo_wtime();

/**
 * @brief Prints collected statistics
 */
void print_statistics();

/**
 * @brief Resets current coherence. Collective function called by all threads on all nodes.
 * @param n number of threads currently spawned on each node. 
 */
void argo_reset_coherence(int n);

/**
 * @brief Gives the ArgoDSM node id for the local process
 * @return Returns the ArgoDSM node id for the local process
 * @deprecated Should use argo_get_nid() instead and eventually remove this
 * @see argo_get_nid()
 */
argo::node_id_t getID();

/**
 * @brief Gives the ArgoDSM node id for the local process
 * @return Returns the ArgoDSM node id for the local process
 */
argo::node_id_t argo_get_nid();

/**
 * @brief Gives number of ArgoDSM nodes
 * @return Number of ArgoDSM nodes
 */
unsigned int argo_get_nodes();

/**
 * @brief returns the maximum number of threads per ArgoDSM node (defined by NUM_THREADS)
 * @return NUM_THREADS 
 * @bug NUM_THREADS is not defined properly. DO NOT USE!
 */
unsigned int getThreadCount();

/**
 * @brief Gives a pointer to the global address space
 * @return Start address of the global address space
 */
void *argo_get_global_base();

/**
 * @brief Size of global address space
 * @return Size of global address space
 */
size_t argo_get_global_size();

/**
 * @brief Gives out a local thread ID for a local thread assuming NUM_THREADS per node
 * @return a local thread ID for the local thread 
 */
int argo_get_local_tid();

/**
 * @brief Gives out a global thread ID for a local thread assuming NUM_THREADS per node
 * @return a global thread ID for the local thread 
 * @bug NUM_THREADS is not defined properly. DO NOT USE!
 */
int argo_get_global_tid();
/**
 * @brief Registers the local thread to ArgoDSM and gets a local thread ID.
 * @bug NUM_THREADS is not defined properly. DO NOT USE!
 */
void argo_register_thread();
/**
 * @brief Pins and registers the local thread
 * @see argo_register_thread()
 */
void argo_pin_threads();

/*MPI*/
/**
 * @brief Initializes the MPI environment
 */
void initmpi();
/**
 * @brief Initializes a mpi data structure for writing cacheb control data over the network * @brief 
 */
void init_mpi_struct(void);
/**
 * @brief Initializes a mpi data structure for writing cacheblocks over the network
 */
void init_mpi_cacheblock(void);
/**
 * @brief Checks if something is power of 2
 * @param x a non-negative integer
 * @return 1 if x is 0 or a power of 2, otherwise return 0
 */
unsigned long isPowerOf2(unsigned long x);
/**
 * @brief Gets cacheindex for a given address
 * @param addr Address in the global address space
 * @return cacheindex where addr should map to in the ArgoDSM page cache
 */
unsigned long getCacheIndex(unsigned long addr);
/**
 * @brief Gives homenode for a given address
 * @param addr Address in the global address space
 * @return Process ID of the node backing the memory containing addr
 */
argo::node_id_t get_homenode(std::size_t addr);
/**
 * @brief Gives homenode for a given address
 * @param addr Address in the global address space
 * @return Process ID of the node backing the memory containing addr,
 * or argo::data_distribution::invalid_node_id if addr has not been first-touched
 * @note This version does not invoke a first-touch call if an
 * address has not been first-touched
 */
argo::node_id_t peek_homenode(std::size_t addr);
/**
 * @brief Gets the offset of an address on the local nodes part of the global memory
 * @param addr Address in the global address space
 * @return addr-(start address of local process part of global memory)
 */
std::size_t get_offset(std::size_t addr);
/**
 * @brief Gets the offset of an address on the local nodes part of the global memory
 * @param addr Address in the global address space
 * @return addr-(start address of local process part of global memory),
 * or argo::data_distribution::invalid_offset if addr has not been first-touched yet
 * @note This version does not invoke a first-touch call if an
 * address has not been first-touched
 */
std::size_t peek_offset(std::size_t addr);
/**
 * @brief Gives an index to the sharer/writer vector depending on the address
 * @param addr Address in the global address space
 * @return index for sharer vector for the page
 */
unsigned long get_classification_index(uint64_t addr);
/**
 * @brief Check whether a page is either cached on the node or
 * locally backed.
 * @param addr Address in the global address space
 * @return true if cached or locally backed, else false
 * @warning This is strictly meant for testing prefetching
 * @todo This should be moved in to a dedicated cache class
 */
bool _is_cached(std::size_t addr);

/**
 * @brief Locks the correct mpi_lock_sharer, performs operation
 * op and then unlocks the locked mpi_lock_sharer
 * @param lock_type MPI lock type
 * @param rank remote node to perform op on
 * @param offset the offset in to the remote node's globalSharer
 * data structure
 * @param op A function to execute
 */
void sharer_op(int lock_type, int rank, int offset,
		std::function<void(const std::size_t window_index)> op);

/**
 * @brief Gets the sharer window index based on the classification
 * index
 * @param classification_index the page classification index
 * @return the sharer window index
 */
std::size_t get_sharer_win_index(int classification_index);

/**
 * @brief Gets the sharer window offset based on the classification
 * index
 * @param classification_index the page classification index
 * @return the offset in to the sharer window
 */
std::size_t get_sharer_win_offset(int classification_index);

/**
 * @brief Gets the data window index based on the offset into the
 * node's backing store
 * @param offset the page offset in to the remote node's backing
 * store
 * @return the data window index
 */
std::size_t get_data_win_index(std::size_t offset);

/**
 * @brief Gets the sharer window offset based on the classification
 * index
 * @param offset the page offset in to the remote node's backing
 * store
 * @return the offset in to the sharer window
 */
std::size_t get_data_win_offset(std::size_t offset);

/**
 * @brief Check if a data window lock is locked by the caller
 * @param data_win_index The index of the data window
 * @param homenode The id of the node to check the lock for
 * @return True if locked by the caller, else false
 */
bool have_lock(int data_win_index, int homenode);

/**
 * @brief Add a data window index and homenode as locked
 * @param data_win_index The index of the data window
 * @param homenode The id of the node to add the lock for
 */
void add_to_locked(int data_win_index, int homenode);

/**
 * @brief Unlock all data windows locked by the caller
 */
void unlock_windows();

/**
 * @brief Calculate the standard deviation for v
 * @param v a vector of doubles by reference
 * @return The standard deviation for all values in v
 */
double stddev(const std::vector<double> &v);

/**
 * @brief Returns the write buffer (by index) which the
 * cache index should be added to or removed from
 * @param cache_index a valid ArgoDSM cache index
 * @return The write buffer by index
 */
std::size_t get_write_buffer(std::size_t cache_index);
#endif /* argo_swdsm_h */
