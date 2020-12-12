/**
 * @file
 * @brief This file implements the skew-mapp and skew-mapp-block data distributions
 * @copyright Eta Scale AB. Licensed under the Eta Scale Open Source License. See the LICENSE file for details.
 */

#ifndef argo_skew_mapp_distribution_hpp
#define argo_skew_mapp_distribution_hpp argo_skew_mapp_distribution_hpp

#include "base_distribution.hpp"

namespace argo {
	namespace data_distribution {
		/**
		 * @brief the skew-mapp data distribution
		 * @details cyclically distributes a page per round but skips a node
		 *          for every N (number of nodes used) pages allocated
		 */
		template<int instance>
		class skew_mapp_distribution : public base_distribution<instance> {
			public:
				virtual node_id_t homenode (char* const ptr) {
					static const std::size_t zero = (base_distribution<instance>::nodes - 1) * granularity;
					const std::size_t addr = ptr - base_distribution<instance>::start_address;
					const std::size_t lessaddr = (addr >= granularity) ? addr - granularity : zero;
					const std::size_t pagenum = lessaddr / granularity;
					node_id_t homenode = (pagenum + pagenum / base_distribution<instance>::nodes + 1) % base_distribution<instance>::nodes;

					if(homenode >= base_distribution<instance>::nodes) {
						exit(EXIT_FAILURE);
					}
					return homenode;
				}

				virtual std::size_t local_offset (char* const ptr) {
					static constexpr std::size_t zero = 0;
					const std::size_t drift = (ptr - base_distribution<instance>::start_address) % granularity;
					const std::size_t addr = (ptr - base_distribution<instance>::start_address) / granularity * granularity;
					const std::size_t lessaddr = (addr >= granularity) ? addr - granularity : zero;
					const std::size_t pagenum = lessaddr / granularity;
					std::size_t offset = (addr >= granularity && homenode(ptr) == 0)
					? pagenum / base_distribution<instance>::nodes * granularity + granularity + drift
					: pagenum / base_distribution<instance>::nodes * granularity + drift;

					if(offset >= static_cast<std::size_t>(base_distribution<instance>::size_per_node)) {
						exit(EXIT_FAILURE);
					}
					return offset;
				}
		};

		/**
		 * @brief the skew-mapp-block data distribution
		 * @details cyclically distributes a block of pages per round but skips
		 *          a node for every N (number of nodes used) pages allocated
		 */
		template<int instance>
		class skew_mapp_block_distribution : public base_distribution<instance> {
			public:
				virtual node_id_t homenode (char* const ptr) {
					static const std::size_t pageblock = env::allocation_block_size() * granularity;
					static const std::size_t zero = (base_distribution<instance>::nodes - 1) * pageblock;
					const std::size_t addr = ptr - base_distribution<instance>::start_address;
					const std::size_t lessaddr = (addr >= granularity) ? addr - granularity : zero;
					const std::size_t pagenum = lessaddr / pageblock;
					node_id_t homenode = (pagenum + pagenum / base_distribution<instance>::nodes + 1) % base_distribution<instance>::nodes;

					if(homenode >= base_distribution<instance>::nodes) {
						exit(EXIT_FAILURE);
					}
					return homenode;
				}

				virtual std::size_t local_offset (char* const ptr) {
					static constexpr std::size_t zero = 0;
					static const std::size_t pageblock = env::allocation_block_size() * granularity;
					const std::size_t drift = (ptr - base_distribution<instance>::start_address) % granularity;
					const std::size_t addr = (ptr - base_distribution<instance>::start_address) / granularity * granularity;
					const std::size_t lessaddr = (addr >= granularity) ? addr - granularity : zero;
					const std::size_t pagenum = lessaddr / pageblock;
					std::size_t offset = (addr >= granularity && homenode(ptr) == 0)
					? pagenum / base_distribution<instance>::nodes * pageblock + lessaddr % pageblock + granularity + drift
					: pagenum / base_distribution<instance>::nodes * pageblock + lessaddr % pageblock + drift;

					if(offset >= static_cast<std::size_t>(base_distribution<instance>::size_per_node)) {
						exit(EXIT_FAILURE);
					}
					return offset;
				}
		};
	} // namespace data_distribution
} // namespace argo

#endif /* argo_skew_mapp_distribution_hpp */
