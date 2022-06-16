/**
 * @file
 * @brief This file implements the handling of environment variables
 * @copyright Eta Scale AB. Licensed under the Eta Scale Open Source License. See the LICENSE file for details.
 */

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "env.hpp"

namespace {
	/* file constants */
	/**
	 * @brief default requested memory size (if environment variable is unset)
	 * @see @ref ARGO_MEMORY_SIZE
	 */
	const std::size_t default_memory_size = 8ul*(1ul<<30); // default: 8GB

	/**
	 * @brief default requested cache size (if environment variable is unset)
	 * @see @ref ARGO_CACHE_SIZE
	 */
	const std::size_t default_cache_size = 1ul<<30; // default: 1GB

	/**
	 * @brief default requested write buffer size (if environment variable is unset)
	 * @see @ref ARGO_WRITE_BUFFER_SIZE
	 */
	const std::size_t default_write_buffer_size = 512; // default: 512 cache blocks

	/**
	 * @brief default requested write buffer write back size (if environment variable is unset)
	 * @see @ref ARGO_WRITE_BUFFER_WRITE_BACK_SIZE
	 */
	const std::size_t default_write_buffer_write_back_size = 32; // default: 32 pages

	/**
	 * @brief default requested allocation policy (if environment variable is unset)
	 * @see @ref ARGO_ALLOCATION_POLICY
	 */
	const std::size_t default_allocation_policy = 0; // default: naive

	/**
	 * @brief default requested allocation block size (if environment variable is unset)
	 * @see @ref ARGO_ALLOCATION_BLOCK_SIZE
	 */
	const std::size_t default_allocation_block_size = 1ul<<4; // default: 16

	/**
	 * @brief default requested load size (if environment variable is unset)
	 * @see @ref ARGO_LOAD_SIZE
	 */
	const std::size_t default_load_size = 8;

	/**
	 * @brief default number of MPI windows (if environment variable is unset)
	 * @see @ref ARGO_MPI_WINDOWS_PER_NODE
	 */
	const std::size_t default_mpi_windows_per_node = 4;

	/**
	 * @brief default requested statistics level (if environment variable is unset)
	 * @see @ref ARGO_PRINT_STATISTICS
	 */
	const std::size_t default_print_statistics = 2; // default 2 is basic node statistics

	/**
	 * @brief environment variable used for requesting memory size
	 * @see @ref ARGO_MEMORY_SIZE
	 */
	const std::string env_memory_size = "ARGO_MEMORY_SIZE";

	/**
	 * @brief environment variable used for requesting cache size
	 * @see @ref ARGO_CACHE_SIZE
	 */
	const std::string env_cache_size = "ARGO_CACHE_SIZE";

	/**
	 * @brief environment variable used for requesting write buffer size
	 * @see @ref ARGO_WRITE_BUFFER_SIZE
	 */
	const std::string env_write_buffer_size = "ARGO_WRITE_BUFFER_SIZE";

	/**
	 * @brief environment variable used for requesting write buffer write back size
	 * @see @ref ARGO_WRITE_BUFFER_WRITE_BACK_SIZE
	 */
	const std::string env_write_buffer_write_back_size = "ARGO_WRITE_BUFFER_WRITE_BACK_SIZE";

	/**
	 * @brief environment variable used for requesting allocation policy
	 * @see @ref ARGO_ALLOCATION_POLICY
	 */
	const std::string env_allocation_policy = "ARGO_ALLOCATION_POLICY";

	/**
	 * @brief environment variable used for requesting allocation block size
	 * @see @ref ARGO_ALLOCATION_BLOCK_SIZE
	 */
	const std::string env_allocation_block_size = "ARGO_ALLOCATION_BLOCK_SIZE";

	/**
	 * @brief environment variable used for requesting load size
	 * @see @ref ARGO_LOAD_SIZE
	 */
	const std::string env_load_size = "ARGO_LOAD_SIZE";

	/**
	 * @brief environment variable used for requesting the number of MPI windows
	 * @see @ref ARGO_MPI_WINDOWS_PER_NODE
	 */
	const std::string env_mpi_windows_per_node = "ARGO_MPI_WINDOWS_PER_NODE";

	/**
	 * @brief environment variable used for requesting statistics level
	 * @see @ref ARGO_PRINT_STATISTICS
	 */
	const std::string env_print_statistics = "ARGO_PRINT_STATISTICS";

	/** @brief error message string */
	const std::string msg_uninitialized = "argo::env::init() must be called before accessing environment values";
	/** @brief error message string */
	const std::string msg_illegal_format = "An environment variable could not be converted to a number: ";
	/** @brief error message string */
	const std::string msg_out_of_range = "An environment variable contains a number outside the possible range: ";

	/* file variables */
	/**
	 * @brief memory size requested through the environment variable @ref ARGO_MEMORY_SIZE
	 */
	std::size_t value_memory_size;

	/**
	 * @brief cache size requested through the environment variable @ref ARGO_CACHE_SIZE
	 */
	std::size_t value_cache_size;

	/**
	 * @brief write buffer size requested through the environment variable @ref ARGO_WRITE_BUFFER_SIZE
	 */
	std::size_t value_write_buffer_size;

	/**
	 * @brief write buffer write back size requested through the environment variable @ref ARGO_WRITE_BUFFER_WRITE_BACK_SIZE
	 */
	std::size_t value_write_buffer_write_back_size;

	/**
	 * @brief allocation policy requested through the environment variable @ref ARGO_ALLOCATION_POLICY
	 */
	std::size_t value_allocation_policy;

	/**
	 * @brief allocation block size requested through the environment variable @ref ARGO_ALLOCATION_BLOCK_SIZE
	 */
	std::size_t value_allocation_block_size;

	/**
	 * @brief load size requested through the environment variable @ref ARGO_LOAD_SIZE
	 */
	std::size_t value_load_size;

	/**
	 * @brief number of MPI windows requested through the environment variable @ref ARGO_MPI_WINDOWS_PER_NODE
	 */
	std::size_t value_mpi_windows_per_node;

	/**
	 * @brief statistics output level requested through the environment variable @ref ARGO_PRINT_STATISTICS
	 */
	std::size_t value_print_statistics;

	/** @brief flag to allow checking that environment variables have been read before accessing their values */
	bool is_initialized = false;

	/* helper functions */
	/** @brief throw an exception if argo::env::init() has not yet been called */
	void assert_initialized() {
		if(!is_initialized) {
			throw std::logic_error(msg_uninitialized);
		}
	}

	/**
	 * @brief parse an environment variable
	 * @tparam T type of value
	 * @param name the environment variable to parse
	 * @param fallback the default value to use if the environment variable is undefined
	 * @return a pair <env_used, value>, where env_used is true iff the environment variable is set,
	 *         and value is either the value of the environment variable or the fallback value.
	 */
	template<typename T>
	std::pair<bool, T> parse_env(std::string name, T fallback) {
		auto env_get = std::getenv(name.c_str());
		try {
			if(env_get != nullptr) {
				std::string env_string(env_get);
				std::stringstream env_stream(env_string);
				T env_value;
				env_stream >> env_value;
				return std::make_pair(true, env_value);
			} else {
				return std::make_pair(false, fallback);
			}
		} catch (const std::invalid_argument& e) {
			// environment variable exists, but value is not convertable to an unsigned long
			std::cerr << msg_illegal_format << name << std::endl;
			throw;
		} catch (const std::out_of_range& e) {
			// environment variable exists, but value is out of range
			std::cerr << msg_out_of_range << name << std::endl;
			throw;
		}
	}

} // unnamed namespace

namespace argo {
	namespace env {
		void init() {
			value_memory_size = parse_env<std::size_t>(env_memory_size, default_memory_size).second;
			value_cache_size = parse_env<std::size_t>(env_cache_size, default_cache_size).second;
			value_write_buffer_size = parse_env<std::size_t>(
					env_write_buffer_size,
					default_write_buffer_size).second;
			value_write_buffer_write_back_size = parse_env<std::size_t>(
					env_write_buffer_write_back_size,
					default_write_buffer_write_back_size).second;
			// Limit the write buffer write back size to the write buffer size
			if(value_write_buffer_write_back_size > value_write_buffer_size) {
				value_write_buffer_write_back_size = value_write_buffer_size;
			}

			value_allocation_policy = parse_env<std::size_t>(env_allocation_policy, default_allocation_policy).second;
			value_allocation_block_size = parse_env<std::size_t>(env_allocation_block_size, default_allocation_block_size).second;
			value_load_size = parse_env<std::size_t>(env_load_size, default_load_size).second;
			value_mpi_windows_per_node = parse_env<std::size_t>(env_mpi_windows_per_node, default_mpi_windows_per_node).second;
			value_print_statistics = parse_env<std::size_t>(env_print_statistics, default_print_statistics).second;

			is_initialized = true;
		}

		std::size_t memory_size() {
			assert_initialized();
			return value_memory_size;
		}

		std::size_t cache_size() {
			assert_initialized();
			return value_cache_size;
		}

		std::size_t write_buffer_size() {
			assert_initialized();
			return value_write_buffer_size;
		}

		std::size_t write_buffer_write_back_size() {
			assert_initialized();
			return value_write_buffer_write_back_size;
		}

		std::size_t allocation_policy() {
			assert_initialized();
			return value_allocation_policy;
		}

		std::size_t allocation_block_size() {
			assert_initialized();
			return value_allocation_block_size;
		}

		std::size_t load_size() {
			assert_initialized();
			return value_load_size;
		}

		std::size_t mpi_windows_per_node() {
			assert_initialized();
			return value_mpi_windows_per_node;
		}

		std::size_t print_statistics() {
			assert_initialized();
			return value_print_statistics;
		}
	} // namespace env
} // namespace argo
