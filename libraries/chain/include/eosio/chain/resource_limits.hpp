#pragma once
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/types.hpp>
#include <eosio/chain/stake_object.hpp>

#include <eosio/chain/abi_def.hpp>
#include <eosio/chain/snapshot.hpp>

#include <chainbase/chainbase.hpp>
#include <set>

#include <cyberway/chaindb/common.hpp>

namespace eosio { namespace chain { namespace resource_limits {
   namespace impl {
      template<typename T>
      struct ratio {
         static_assert(std::is_integral<T>::value, "ratios must have integral types");
         T numerator;
         T denominator;
      };
   }

   using ratio = impl::ratio<uint64_t>;

   struct elastic_limit_parameters {
      uint64_t target;           // the desired usage
      uint64_t max;              // the maximum usage
      uint32_t periods;          // the number of aggregation periods that contribute to the average usage

      uint32_t max_multiplier;   // the multiplier by which virtual space can oversell usage when uncongested
      ratio    contract_rate;    // the rate at which a congested resource contracts its limit
      ratio    expand_rate;       // the rate at which an uncongested resource expands its limits

      void validate()const; // throws if the parameters do not satisfy basic sanity checks
   };

   struct account_resource_limit {
      int64_t used = 0; ///< quantity used in current window
      int64_t available = 0; ///< quantity available in current window (based upon fractional reserve)
      int64_t max = 0; ///< max per window under current congestion
   };

   class resource_limits_manager {
      public:
         explicit resource_limits_manager(chainbase::database& db, cyberway::chaindb::chaindb_controller& chaindb)
         :_db(db), _chaindb(chaindb)
         {
         }

         void add_indices();
         void initialize_database();
         void add_to_snapshot( const snapshot_writer_ptr& snapshot ) const;
         void read_from_snapshot( const snapshot_reader_ptr& snapshot );

         void initialize_account( const account_name& account );
         void set_block_parameters( const elastic_limit_parameters& cpu_limit_parameters, const elastic_limit_parameters& net_limit_parameters );

         void update_account_usage( const flat_set<account_name>& accounts, uint32_t ordinal );
         void add_transaction_usage( const flat_set<account_name>& accounts, uint64_t cpu_usage, uint64_t net_usage, uint32_t ordinal );

         void add_pending_ram_usage( const account_name account, int64_t ram_delta );
         void verify_account_ram_usage( const account_name accunt )const;

         /// set_account_limits returns true if new ram_bytes limit is more restrictive than the previously set one
         bool set_account_limits( const account_name& account, int64_t ram_bytes, int64_t net_weight, int64_t cpu_weight);
         void get_account_limits( const account_name& account, int64_t& ram_bytes, int64_t& net_weight, int64_t& cpu_weight) const;

         void update_proxied(int64_t now, symbol purpose_symbol, const account_name& account, int64_t frame_length, bool force);

         void process_account_limit_updates();
         void process_block_usage( uint32_t block_num );

         // accessors
         uint64_t get_virtual_block_cpu_limit() const;
         uint64_t get_virtual_block_net_limit() const;

         uint64_t get_block_cpu_limit() const;
         uint64_t get_block_net_limit() const;

         int64_t get_account_cpu_limit( const account_name& name, bool elastic = true) const;
         int64_t get_account_net_limit( const account_name& name, bool elastic = true) const;

         account_resource_limit get_account_cpu_limit_ex( const account_name& name, bool elastic = true) const;
         account_resource_limit get_account_net_limit_ex( const account_name& name, bool elastic = true) const;

         int64_t get_account_ram_usage( const account_name& name ) const;

      private:
         chainbase::database& _db;
         cyberway::chaindb::chaindb_controller& _chaindb;
         
         using AgentsIdx = decltype(_db.get_mutable_index<stake_agent_index>().indices().get<stake_agent_object::by_key>());
         using GrantsIdx = decltype(_db.get_mutable_index<stake_grant_index>().indices().get<stake_grant_object::by_key>());
         
        static auto agent_key(symbol purpose_symbol, const account_name& agent_name) {
             return boost::make_tuple(purpose_symbol.decimals(), purpose_symbol.to_symbol_code(), agent_name);
         }
         static auto grant_key(symbol purpose_symbol, const account_name& grantor_name, const account_name& agent_name = account_name()) {
             return boost::make_tuple(purpose_symbol.decimals(), purpose_symbol.to_symbol_code(), grantor_name, agent_name);
         }
         
         static std::string to_str(const stake_agent_object& arg) {
             return "id = " + std::to_string(arg.id._id) + "; key = " +
                arg.account.to_string() + ", " + std::to_string(arg.purpose_id) + 
                ", " + std::to_string(arg.token_code.value)+ "; level = " + std::to_string(arg.proxy_level) + "\n";
         }
         
         static const stake_agent_object* get_agent(symbol purpose_symbol, const AgentsIdx& agents_idx, const account_name& agent_name) {
            auto agent = agents_idx.find(agent_key(purpose_symbol, agent_name));
            EOS_ASSERT(agent != agents_idx.end(), transaction_exception, "agent doesn't exist");
            return &(*agent); 
         }
         
         void update_proxied_traversal(int64_t now, symbol purpose_symbol,
                            const AgentsIdx& agents_idx, const GrantsIdx& grants_idx,
                            const stake_agent_object* agent, int64_t frame_length, bool force) {
            
            if ((now - agent->last_proxied_update.sec_since_epoch() >= frame_length) || force) {
                dlog(("+++++++++++      update_proxied_traversal 0     ++++++++++++++" + agent->account.to_string()).c_str());
                //dlog((" for " + agent->account.to_string() + 
                //    " pre = " + std::to_string(agent->proxied) + "\n").c_str());
                int64_t new_proxied = 0;
                int64_t unstaked = 0;
                
                /*std::string grants_str("grants:\n");
                for (auto& g : grants_idx) {
                    grants_str += g.grantor_name.to_string() + " granted to " + g.agent_name.to_string() + 
                        "; pct = " + std::to_string(g.pct) + "; share = " + std::to_string(g.share) + "\n"; 
                }
                dlog(grants_str.c_str());*/
                
                auto grant_itr = grants_idx.lower_bound(grant_key(purpose_symbol, agent->account));
                //dlog("update_proxied_traversal pre while");
                dlog(("+++++++++++      update_proxied_traversal 1     ++++++++++++++" + agent->account.to_string()).c_str());
                while ((grant_itr != grants_idx.end()) &&
                       (grant_itr->purpose_id   == purpose_symbol.precision()) &&
                       (grant_itr->token_code   == purpose_symbol.to_symbol_code()) &&
                       (grant_itr->grantor_name == agent->account))
                {
                    dlog(("+++++++++++      update_proxied_traversal 2     ++++++++++++++" + agent->account.to_string()).c_str());
                    //dlog(("update_proxied_traversal for " + agent->account.to_string() + 
                    //" new_proxied = " + std::to_string(new_proxied) + "\n").c_str());
                    auto proxy_agent = get_agent(purpose_symbol, agents_idx, grant_itr->agent_name);
                    
                    //dlog(("proxy_agent " + proxy_agent->account.to_string() + "\n").c_str());
                    dlog(("+++++++++++      update_proxied_traversal 3     ++++++++++++++" + agent->account.to_string()).c_str());
                    update_proxied_traversal(now, purpose_symbol, agents_idx, grants_idx, proxy_agent, frame_length, force);
                    dlog(("+++++++++++      update_proxied_traversal 4     ++++++++++++++" + agent->account.to_string()).c_str());
                    //if (proxy_agent->proxy_level < agent->proxy_level && 
                    //    grant_itr->break_fee >= proxy_agent->fee &&
                    //    grant_itr->break_min_own_staked <= proxy_agent->min_own_staked) 
                    {
                        if (proxy_agent->shares_sum)
                            new_proxied += static_cast<int64_t>((static_cast<int128_t>(grant_itr->share) * proxy_agent->get_total_funds()) / proxy_agent->shares_sum);
                        ++grant_itr;
                    }
                    dlog(("+++++++++++      update_proxied_traversal 5     ++++++++++++++" + agent->account.to_string()).c_str());
                    //else {
                    //    unstaked += recall_traversal(purpose_symbol, agents_idx, grants_idx, grant_itr->agent_name, grant_itr->share, grant_itr->break_fee);
                    //    grant_itr = grants_idx.erase(grant_itr);
                    //}
                    
                }
                
                std::string agents_str("agents:\n");
                
                
                /*_db.modify(*agent, [&](auto& a) {
                    a.balance += unstaked;
                    a.proxied = new_proxied;
                    a.last_proxied_update = time_point_sec(now);
                });*/
                
                
                const auto& actual_entry = _db.get<stake_agent_object, by_id>(agent->id);
                dlog(("+++++++++++      update_proxied_traversal 6     ++++++++++++++" + agent->account.to_string()).c_str());
               agents_str += "cur agent: \n   " + to_str(*agent);
                for (auto& g : agents_idx) {
                    agents_str += to_str(g); 
                }
                dlog(agents_str.c_str());
                dlog(("+++++++++++      update_proxied_traversal 7     ++++++++++++++" + agent->account.to_string()).c_str());
                _db.modify(actual_entry, [&](auto& a) {
                    a.balance += unstaked;
                    a.proxied = new_proxied;
                    a.last_proxied_update = time_point_sec(now);
                });
                dlog(("+++++++++++      update_proxied_traversal 8     ++++++++++++++" + agent->account.to_string()).c_str());
                //agents_str += "cur agent from db: \n   " + to_str(actual_entry);
                
                //dlog(("update_proxied_traversal for " + agent->account.to_string() + 
                //    " after = " + std::to_string(agent->proxied) + "\n").c_str());
            }
            /*
            const auto& agent = agent_idx.lower_bound(agent_name.value);
            EOS_ASSERT(agent != agent_idx.end(), transaction_exception, "agent doesn't exist");
            
            if ((now - agent->last_proxied_update.sec_since_epoch() >= frame_length) || force) {
                int64_t new_proxied = 0;
                int64_t unstaked = 0;
                auto grant_itr = grant_idx.lower_bound(agent_name);
                while ((grant_itr != grant_idx.end()) && (grant_itr->grantor_name == agent_name)) {
                    //const auto& proxy_agent = agent_idx.get(grant_itr->agent_name.value);
                    const auto& proxy_agent = agent_idx.lower_bound(grant_itr->agent_name.value);
                    EOS_ASSERT(proxy_agent != agent_idx.end(), transaction_exception, "agent doesn't exist");
                    update_proxied_traversal(now, agent_multi_index, grant_multi_index, agent_idx, grant_idx, proxy_agent->account, frame_length, force);
                    //if (proxy_agent->proxy_level < agent->proxy_level && 
                    //    grant_itr->break_fee >= proxy_agent->fee &&
                    //    grant_itr->break_min_own_staked <= proxy_agent->min_own_staked) 
                    {
                        if (proxy_agent->shares_sum)
                            new_proxied += static_cast<int64_t>((static_cast<int128_t>(grant_itr->share) * proxy_agent->get_total_funds()) / proxy_agent->shares_sum);
                        ++grant_itr;
                        
                    }
                    //TODO!
                    //else {
                    //    unstaked += recall_traversal(agents_table, grants_table, grant_itr->agent_name, grant_itr->share, grant_itr->break_fee);
                    //    grant_itr = grantor_index.erase(grant_itr);
                    //}
                    
                }
                _db.modify(*agent, [&](stake_agent_object& a) {
                    a.balance += unstaked;
                    a.proxied = new_proxied;
                    a.last_proxied_update = time_point_sec(now);
                });
            }*/
        }


   };
} } } /// eosio::chain

FC_REFLECT( eosio::chain::resource_limits::ratio, (numerator)(denominator))
FC_REFLECT( eosio::chain::resource_limits::elastic_limit_parameters, (target)(max)(periods)(max_multiplier)(contract_rate)(expand_rate))
FC_REFLECT( eosio::chain::resource_limits::account_resource_limit, (used)(available)(max) )

