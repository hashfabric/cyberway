#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/resource_limits_private.hpp>
#include <eosio/chain/transaction_metadata.hpp>
#include <eosio/chain/transaction.hpp>
#include <boost/tuple/tuple_io.hpp>
#include <eosio/chain/database_utils.hpp>
#include <algorithm>
#include <eosio/chain/stake_object.hpp>

namespace eosio { namespace chain { namespace resource_limits {

using resource_index_set = index_set<
   resource_limits_table,
   resource_usage_table,
   resource_limits_state_table,
   resource_limits_config_table,
   stake_agent_table,
   stake_grant_table,
   stake_param_table,
   stake_stat_table
>;

static_assert( config::rate_limiting_precision > 0, "config::rate_limiting_precision must be positive" );

static uint64_t update_elastic_limit(uint64_t current_limit, uint64_t average_usage, const elastic_limit_parameters& params) {
   uint64_t result = current_limit;
   if (average_usage > params.target ) {
      result = result * params.contract_rate;
   } else {
      result = result * params.expand_rate;
   }
   return std::min(std::max(result, params.max), params.max * params.max_multiplier);
}

static int64_t get_prop(int64_t arg, int64_t numer, int64_t denom = config::_100percent) {
    return static_cast<int64_t>((static_cast<int128_t>(arg) * numer) / denom);
}

void elastic_limit_parameters::validate()const {
   // At the very least ensure parameters are not set to values that will cause divide by zero errors later on.
   // Stricter checks for sensible values can be added later.
   EOS_ASSERT( periods > 0, resource_limit_exception, "elastic limit parameter 'periods' cannot be zero" );
   EOS_ASSERT( contract_rate.denominator > 0, resource_limit_exception, "elastic limit parameter 'contract_rate' is not a well-defined ratio" );
   EOS_ASSERT( expand_rate.denominator > 0, resource_limit_exception, "elastic limit parameter 'expand_rate' is not a well-defined ratio" );
}


void resource_limits_state_object::update_virtual_cpu_limit( const resource_limits_config_object& cfg ) {
   //idump((average_block_cpu_usage.average()));
   virtual_cpu_limit = update_elastic_limit(virtual_cpu_limit, average_block_cpu_usage.average(), cfg.cpu_limit_parameters);
   //idump((virtual_cpu_limit));
}

void resource_limits_state_object::update_virtual_net_limit( const resource_limits_config_object& cfg ) {
   virtual_net_limit = update_elastic_limit(virtual_net_limit, average_block_net_usage.average(), cfg.net_limit_parameters);
}

void resource_limits_manager::add_indices() {
   resource_index_set::add_indices(_chaindb);
}

void resource_limits_manager::initialize_database() {
    auto& config = _chaindb.emplace<resource_limits_config_object>([](resource_limits_config_object& config){
      // see default settings in the declaration
   });

   _chaindb.emplace<resource_limits_state_object>([&config](resource_limits_state_object& state){
      // see default settings in the declaration

      // start the chain off in a way that it is "congested" aka slow-start
      state.virtual_cpu_limit = config.cpu_limit_parameters.max;
      state.virtual_net_limit = config.net_limit_parameters.max;
   });
}

void resource_limits_manager::add_to_snapshot( const snapshot_writer_ptr& snapshot ) const {
   // TODO: Removed by CyberWay
}

void resource_limits_manager::read_from_snapshot( const snapshot_reader_ptr& snapshot ) {
   // TODO: Removed by CyberWay
}

void resource_limits_manager::initialize_account(const account_name& account) {
   _chaindb.emplace<resource_limits_object>([&]( resource_limits_object& bl ) {
      bl.owner = account;
   });

   _chaindb.emplace<resource_usage_object>([&]( resource_usage_object& bu ) {
      bu.owner = account;
   });
}

void resource_limits_manager::set_block_parameters(const elastic_limit_parameters& cpu_limit_parameters, const elastic_limit_parameters& net_limit_parameters ) {
   cpu_limit_parameters.validate();
   net_limit_parameters.validate();
   const auto& config = _chaindb.get<resource_limits_config_object>();
   _chaindb.modify(config, [&](resource_limits_config_object& c){
      c.cpu_limit_parameters = cpu_limit_parameters;
      c.net_limit_parameters = net_limit_parameters;
   });
}

void resource_limits_manager::update_account_usage(const flat_set<account_name>& accounts, uint32_t time_slot ) {
   const auto& config = _chaindb.get<resource_limits_config_object>();
   auto usage_table = _chaindb.get_table<resource_usage_object>();
   auto owner_idx = usage_table.get_index<by_owner>();
   for( const auto& a : accounts ) {
      const auto& usage = owner_idx.get( a );
      usage_table.modify( usage, [&]( auto& bu ){
          bu.net_usage.add( 0, time_slot, config.account_net_usage_average_window );
          bu.cpu_usage.add( 0, time_slot, config.account_cpu_usage_average_window );
      });
   }
}

void resource_limits_manager::add_transaction_usage(const flat_set<account_name>& accounts, uint64_t cpu_usage, uint64_t net_usage, uint32_t time_slot ) {
   auto state_table = _chaindb.get_table<resource_limits_state_object>();
   const auto& state = state_table.get();
   const auto& config = _chaindb.get<resource_limits_config_object>();
   auto usage_table = _chaindb.get_table<resource_usage_object>();
   auto owner_idx = usage_table.get_index<by_owner>();

   for( const auto& a : accounts ) {

      const auto& usage = owner_idx.get( a );
      int64_t unused;
      int64_t net_weight;
      int64_t cpu_weight;
      get_account_limits( a, unused, net_weight, cpu_weight );

      usage_table.modify( usage, [&]( auto& bu ){
          bu.net_usage.add( net_usage, time_slot, config.account_net_usage_average_window );
          bu.cpu_usage.add( cpu_usage, time_slot, config.account_cpu_usage_average_window );
      });

      if( cpu_weight >= 0 && state.total_cpu_weight > 0 ) {
         uint128_t window_size = config.account_cpu_usage_average_window;
         auto virtual_network_capacity_in_window = (uint128_t)state.virtual_cpu_limit * window_size;
         auto cpu_used_in_window                 = ((uint128_t)usage.cpu_usage.value_ex * window_size) / (uint128_t)config::rate_limiting_precision;

         uint128_t user_weight     = (uint128_t)cpu_weight;
         uint128_t all_user_weight = state.total_cpu_weight;

         auto max_user_use_in_window = (virtual_network_capacity_in_window * user_weight) / all_user_weight;

         EOS_ASSERT( cpu_used_in_window <= max_user_use_in_window,
                     tx_cpu_usage_exceeded,
                     "authorizing account '${n}' has insufficient cpu resources for this transaction",
                     ("n", name(a))
                     ("cpu_used_in_window",cpu_used_in_window)
                     ("max_user_use_in_window",max_user_use_in_window) );
      }

      if( net_weight >= 0 && state.total_net_weight > 0) {

         uint128_t window_size = config.account_net_usage_average_window;
         auto virtual_network_capacity_in_window = (uint128_t)state.virtual_net_limit * window_size;
         auto net_used_in_window                 = ((uint128_t)usage.net_usage.value_ex * window_size) / (uint128_t)config::rate_limiting_precision;

         uint128_t user_weight     = (uint128_t)net_weight;
         uint128_t all_user_weight = state.total_net_weight;

         auto max_user_use_in_window = (virtual_network_capacity_in_window * user_weight) / all_user_weight;

         EOS_ASSERT( net_used_in_window <= max_user_use_in_window,
                     tx_net_usage_exceeded,
                     "authorizing account '${n}' has insufficient net resources for this transaction",
                     ("n", name(a))
                     ("net_used_in_window",net_used_in_window)
                     ("max_user_use_in_window",max_user_use_in_window) );

      }
   }

   // account for this transaction in the block and do not exceed those limits either
   state_table.modify(state, [&](resource_limits_state_object& rls){
      rls.pending_cpu_usage += cpu_usage;
      rls.pending_net_usage += net_usage;
   });

   EOS_ASSERT( state.pending_cpu_usage <= config.cpu_limit_parameters.max, block_resource_exhausted, "Block has insufficient cpu resources" );
   EOS_ASSERT( state.pending_net_usage <= config.net_limit_parameters.max, block_resource_exhausted, "Block has insufficient net resources" );
}

void resource_limits_manager::add_pending_ram_usage( const account_name account, int64_t ram_delta ) {
   if (ram_delta == 0) {
      return;
   }
   const auto& usage  = _chaindb.get<resource_usage_object,by_owner>( account );

   EOS_ASSERT( ram_delta <= 0 || UINT64_MAX - usage.ram_usage >= (uint64_t)ram_delta, transaction_exception,
              "Ram usage delta would overflow UINT64_MAX");
   EOS_ASSERT(ram_delta >= 0 || usage.ram_usage >= (uint64_t)(-ram_delta), transaction_exception,
              "Ram usage delta would underflow UINT64_MAX");

   _chaindb.modify( usage, [&]( auto& u ) {
     u.ram_usage += ram_delta;
   });
}

void resource_limits_manager::verify_account_ram_usage( const account_name account )const {
   int64_t ram_bytes; int64_t net_weight; int64_t cpu_weight;
   get_account_limits( account, ram_bytes, net_weight, cpu_weight );

   if( ram_bytes >= 0 ) {
      const auto ram_usage  = get_account_ram_usage( account );
      EOS_ASSERT( ram_usage <= ram_bytes, ram_usage_exceeded,
                  "account ${account} has insufficient ram; needs ${needs} bytes has ${available} bytes",
                  ("account", account)("needs",ram_usage)("available",ram_bytes)              );
   }
}

int64_t resource_limits_manager::get_account_ram_usage( const account_name& name )const {
   return _chaindb.get<resource_usage_object,by_owner>( name ).ram_usage;
}

bool resource_limits_manager::set_account_limits( const account_name& account, int64_t ram_bytes, int64_t net_weight, int64_t cpu_weight) {
   //const auto& usage = _db.get<resource_usage_object,by_owner>( account );
   /*
    * Since we need to delay these until the next resource limiting boundary, these are created in a "pending"
    * state or adjusted in an existing "pending" state.  The chain controller will collapse "pending" state into
    * the actual state at the next appropriate boundary.
    */
   auto limit_table = _chaindb.get_table<resource_limits_object>();
   auto owner_idx = limit_table.get_index<by_owner>();
   auto find_or_create_pending_limits = [&]() -> const resource_limits_object& {
      auto pitr = owner_idx.find( boost::make_tuple(true, account) );
      if (owner_idx.end() == pitr) {
         const auto& limits = owner_idx.get( boost::make_tuple(false, account) );
         auto res = limit_table.emplace([&](resource_limits_object& pending_limits){
            pending_limits.owner = limits.owner;
            pending_limits.ram_bytes = limits.ram_bytes;
            pending_limits.net_weight = limits.net_weight;
            pending_limits.cpu_weight = limits.cpu_weight;
            pending_limits.pending = true;
         });
         return res.obj;
      } else {
         return *pitr;
      }
   };

   // update the users weights directly
   auto& limits = find_or_create_pending_limits();

   bool decreased_limit = false;

   if( ram_bytes >= 0 ) {

      decreased_limit = ( (limits.ram_bytes < 0) || (ram_bytes < limits.ram_bytes) );

      /*
      if( limits.ram_bytes < 0 ) {
         EOS_ASSERT(ram_bytes >= usage.ram_usage, wasm_execution_error, "converting unlimited account would result in overcommitment [commit=${c}, desired limit=${l}]", ("c", usage.ram_usage)("l", ram_bytes));
      } else {
         EOS_ASSERT(ram_bytes >= usage.ram_usage, wasm_execution_error, "attempting to release committed ram resources [commit=${c}, desired limit=${l}]", ("c", usage.ram_usage)("l", ram_bytes));
      }
      */
   }

    limit_table.modify( limits, [&]( resource_limits_object& pending_limits ){
      pending_limits.ram_bytes = ram_bytes;
      pending_limits.net_weight = net_weight;
      pending_limits.cpu_weight = cpu_weight;
   });

   return decreased_limit;
}

void resource_limits_manager::get_account_limits( const account_name& account, int64_t& ram_bytes, int64_t& net_weight, int64_t& cpu_weight ) const {
   auto limit_table = _chaindb.get_table<resource_limits_object>();
   auto owner_idx = limit_table.get_index<by_owner>();
   auto itr = owner_idx.find( boost::make_tuple(true, account) );
   if (owner_idx.end() != itr) {
      ram_bytes  = itr->ram_bytes;
      net_weight = itr->net_weight;
      cpu_weight = itr->cpu_weight;
   } else {
      const auto& buo = owner_idx.get( boost::make_tuple( false, account ) );
      ram_bytes  = buo.ram_bytes;
      net_weight = buo.net_weight;
      cpu_weight = buo.cpu_weight;
   }
}

void resource_limits_manager::process_account_limit_updates() {
   auto limits_table = _chaindb.get_table<resource_limits_object>();
   auto owner_idx = limits_table.get_index<by_owner>();

   // convenience local lambda to reduce clutter
   auto update_state_and_value = [](uint64_t &total, int64_t &value, int64_t pending_value, const char* debug_which) -> void {
      if (value > 0) {
         EOS_ASSERT(total >= value, rate_limiting_state_inconsistent, "underflow when reverting old value to ${which}", ("which", debug_which));
         total -= value;
      }

      if (pending_value > 0) {
         EOS_ASSERT(UINT64_MAX - total >= pending_value, rate_limiting_state_inconsistent, "overflow when applying new value to ${which}", ("which", debug_which));
         total += pending_value;
      }

      value = pending_value;
   };

   auto state_table = _chaindb.get_table<resource_limits_state_object>();
   auto& state = state_table.get();
   state_table.modify(state, [&](resource_limits_state_object& rso){
      auto itr = owner_idx.lower_bound(true);
      for(auto etr = owner_idx.end(); etr != itr && itr->pending; ) {
         const auto& actual_entry = owner_idx.get(boost::make_tuple(false, itr->owner));
         limits_table.modify(actual_entry, [&](resource_limits_object& rlo){
            update_state_and_value(rso.total_ram_bytes,  rlo.ram_bytes,  itr->ram_bytes, "ram_bytes");
            update_state_and_value(rso.total_cpu_weight, rlo.cpu_weight, itr->cpu_weight, "cpu_weight");
            update_state_and_value(rso.total_net_weight, rlo.net_weight, itr->net_weight, "net_weight");
         });

         limits_table.erase(*itr);
      }
   });
}

void resource_limits_manager::process_block_usage(uint32_t block_num) {
   auto state_table = _chaindb.get_table<resource_limits_state_object>();
   const auto& s = state_table.get();
   const auto& config = _chaindb.get<resource_limits_config_object>();
   state_table.modify(s, [&](resource_limits_state_object& state){
      // apply pending usage, update virtual limits and reset the pending

      state.average_block_cpu_usage.add(state.pending_cpu_usage, block_num, config.cpu_limit_parameters.periods);
      state.update_virtual_cpu_limit(config);
      state.pending_cpu_usage = 0;

      state.average_block_net_usage.add(state.pending_net_usage, block_num, config.net_limit_parameters.periods);
      state.update_virtual_net_limit(config);
      state.pending_net_usage = 0;

   });

}

uint64_t resource_limits_manager::get_virtual_block_cpu_limit() const {
   const auto& state = _chaindb.get<resource_limits_state_object>();
   return state.virtual_cpu_limit;
}

uint64_t resource_limits_manager::get_virtual_block_net_limit() const {
   const auto& state = _chaindb.get<resource_limits_state_object>();
   return state.virtual_net_limit;
}

uint64_t resource_limits_manager::get_block_cpu_limit() const {
   const auto& state = _chaindb.get<resource_limits_state_object>();
   const auto& config = _chaindb.get<resource_limits_config_object>();
   return config.cpu_limit_parameters.max - state.pending_cpu_usage;
}

uint64_t resource_limits_manager::get_block_net_limit() const {
   const auto& state = _chaindb.get<resource_limits_state_object>();
   const auto& config = _chaindb.get<resource_limits_config_object>();
   return config.net_limit_parameters.max - state.pending_net_usage;
}

int64_t resource_limits_manager::get_account_cpu_limit( const account_name& name, bool elastic ) const {
   auto arl = get_account_cpu_limit_ex(name, elastic);
   return arl.available;
}

account_resource_limit resource_limits_manager::get_account_cpu_limit_ex( const account_name& name, bool elastic) const {

   const auto& state = _chaindb.get<resource_limits_state_object>();
   const auto& usage = _chaindb.get<resource_usage_object, by_owner>(name);
   const auto& config = _chaindb.get<resource_limits_config_object>();

   int64_t cpu_weight, x, y;
   get_account_limits( name, x, y, cpu_weight );

   if( cpu_weight < 0 || state.total_cpu_weight == 0 ) {
      return { -1, -1, -1 };
   }

   account_resource_limit arl;

   uint128_t window_size = config.account_cpu_usage_average_window;

   uint128_t virtual_cpu_capacity_in_window = (uint128_t)(elastic ? state.virtual_cpu_limit : config.cpu_limit_parameters.max) * window_size;
   uint128_t user_weight     = (uint128_t)cpu_weight;
   uint128_t all_user_weight = (uint128_t)state.total_cpu_weight;

   auto max_user_use_in_window = (virtual_cpu_capacity_in_window * user_weight) / all_user_weight;
   auto cpu_used_in_window  = impl::integer_divide_ceil((uint128_t)usage.cpu_usage.value_ex * window_size, (uint128_t)config::rate_limiting_precision);

   if( max_user_use_in_window <= cpu_used_in_window )
      arl.available = 0;
   else
      arl.available = impl::downgrade_cast<int64_t>(max_user_use_in_window - cpu_used_in_window);

   arl.used = impl::downgrade_cast<int64_t>(cpu_used_in_window);
   arl.max = impl::downgrade_cast<int64_t>(max_user_use_in_window);
   return arl;
}

int64_t resource_limits_manager::get_account_net_limit( const account_name& name, bool elastic) const {
   auto arl = get_account_net_limit_ex(name, elastic);
   return arl.available;
}

account_resource_limit resource_limits_manager::get_account_net_limit_ex( const account_name& name, bool elastic) const {
   const auto& config = _chaindb.get<resource_limits_config_object>();
   const auto& state  = _chaindb.get<resource_limits_state_object>();
   const auto& usage  = _chaindb.get<resource_usage_object, by_owner>(name);

   int64_t net_weight, x, y;
   get_account_limits( name, x, net_weight, y );

   if( net_weight < 0 || state.total_net_weight == 0) {
      return { -1, -1, -1 };
   }

   account_resource_limit arl;

   uint128_t window_size = config.account_net_usage_average_window;

   uint128_t virtual_network_capacity_in_window = (uint128_t)(elastic ? state.virtual_net_limit : config.net_limit_parameters.max) * window_size;
   uint128_t user_weight     = (uint128_t)net_weight;
   uint128_t all_user_weight = (uint128_t)state.total_net_weight;


   auto max_user_use_in_window = (virtual_network_capacity_in_window * user_weight) / all_user_weight;
   auto net_used_in_window  = impl::integer_divide_ceil((uint128_t)usage.net_usage.value_ex * window_size, (uint128_t)config::rate_limiting_precision);

   if( max_user_use_in_window <= net_used_in_window )
      arl.available = 0;
   else
      arl.available = impl::downgrade_cast<int64_t>(max_user_use_in_window - net_used_in_window);

   arl.used = impl::downgrade_cast<int64_t>(net_used_in_window);
   arl.max = impl::downgrade_cast<int64_t>(max_user_use_in_window);
   return arl;
}

static inline auto agent_key(symbol_code purpose_code, symbol_code token_code, const account_name& agent_name) {
    return boost::make_tuple(purpose_code, token_code, agent_name);
}
static inline auto grant_key(
    symbol_code purpose_code, symbol_code token_code, const account_name& grantor_name,
    const account_name& agent_name = account_name()
) {
    return boost::make_tuple(purpose_code, token_code, grantor_name, agent_name);
}

template<typename AgentIndex>
static const stake_agent_object* get_agent(
    symbol_code purpose_code, symbol_code token_code, const AgentIndex& agents_idx, const account_name& agent_name
) {
    auto agent = agents_idx.find(agent_key(purpose_code, token_code, agent_name));
    EOS_ASSERT(agent != agents_idx.end(), transaction_exception, "agent doesn't exist");
    return &(*agent);
}

template<typename AgentIndex, typename GrantIndex>
int64_t recall_proxied_traversal(
    const ram_payer_info& ram, symbol_code purpose_code, symbol_code token_code,
    const AgentIndex& agents_idx, const GrantIndex& grants_idx,
    const account_name& agent_name, int64_t share, int16_t break_fee
) {

    auto agent = get_agent(purpose_code, token_code, agents_idx, agent_name);

    EOS_ASSERT(share >= 0, transaction_exception, "SYSTEM: share can't be negative");
    EOS_ASSERT(share <= agent->shares_sum, transaction_exception, "SYSTEM: incorrect share val");
    if(share == 0)
        return 0;
    auto share_fee = get_prop(share, std::min(agent->fee, break_fee));
    auto share_net = share - share_fee;
    auto balance_ret = get_prop(agent->balance, share_net, agent->shares_sum);
    EOS_ASSERT(balance_ret <= agent->balance, transaction_exception, "SYSTEM: incorrect balance_ret val");

    auto proxied_ret = 0;
    auto grant_itr = grants_idx.lower_bound(grant_key(purpose_code, token_code, agent->account));
    while ((grant_itr != grants_idx.end()) &&
           (grant_itr->purpose_code   == purpose_code) &&
           (grant_itr->token_code   == token_code) &&
           (grant_itr->grantor_name == agent->account))
    {
        auto to_recall = get_prop(share_net, grant_itr->share, agent->shares_sum);
        proxied_ret += recall_proxied_traversal(ram, purpose_code, token_code, agents_idx, grants_idx, grant_itr->agent_name, to_recall, grant_itr->break_fee);
        grants_idx.modify(*grant_itr, [&](auto& g) { g.share -= to_recall; });
        ++grant_itr;
    }
    EOS_ASSERT(proxied_ret <= agent->proxied, transaction_exception, "SYSTEM: incorrect proxied_ret val");

    agents_idx.modify(*agent, [&](auto& a) {
        a.balance -= balance_ret;
        a.proxied -= proxied_ret;
        a.own_share += share_fee;
        a.shares_sum -= share_net;
    });
    return balance_ret + proxied_ret;
}

template<typename AgentIndex, typename GrantIndex>
void update_proxied_traversal(
    const ram_payer_info& ram, int64_t now, symbol_code purpose_code, symbol_code token_code,
    const AgentIndex& agents_idx, const GrantIndex& grants_idx,
    const stake_agent_object* agent, int64_t frame_length, bool force
) {

    if ((now - agent->last_proxied_update.sec_since_epoch() >= frame_length) || force) {
        int64_t new_proxied = 0;
        int64_t unstaked = 0;

        auto grant_itr = grants_idx.lower_bound(grant_key(purpose_code, token_code, agent->account));

        while ((grant_itr != grants_idx.end()) &&
               (grant_itr->purpose_code == purpose_code) &&
               (grant_itr->token_code   == token_code) &&
               (grant_itr->grantor_name == agent->account))
        {
            auto proxy_agent = get_agent(purpose_code, token_code, agents_idx, grant_itr->agent_name);
            update_proxied_traversal(ram, now, purpose_code, token_code, agents_idx, grants_idx, proxy_agent, frame_length, force);

            if (proxy_agent->proxy_level < agent->proxy_level &&
                grant_itr->break_fee >= proxy_agent->fee &&
                grant_itr->break_min_own_staked <= proxy_agent->min_own_staked)
            {
                if (proxy_agent->shares_sum)
                    new_proxied += get_prop(proxy_agent->get_total_funds(), grant_itr->share, proxy_agent->shares_sum);
                ++grant_itr;
            }
            else {
                unstaked += recall_proxied_traversal(ram, purpose_code, token_code, agents_idx, grants_idx, grant_itr->agent_name, grant_itr->share, grant_itr->break_fee);
                const auto &cur_grant = *grant_itr;
                ++grant_itr;
                grants_idx.erase(cur_grant, ram);
            }
        }
        agents_idx.modify(*agent, [&](auto& a) {
            a.balance += unstaked;
            a.proxied = new_proxied;
            a.last_proxied_update = time_point_sec(now);
        });
    }
}

void resource_limits_manager::update_proxied(
    const ram_payer_info& ram, int64_t now, symbol_code purpose_code, symbol_code token_code,
    const account_name& account, int64_t frame_length, bool force
) {
    auto agents_table = _chaindb.get_table<stake_agent_object>();
    auto grants_table = _chaindb.get_table<stake_grant_object>();
    auto agents_idx = agents_table.get_index<stake_agent_object::by_key>();
    auto grants_idx = grants_table.get_index<stake_grant_object::by_key>();
    update_proxied_traversal(ram, now, purpose_code, token_code, agents_idx, grants_idx,
        get_agent(purpose_code, token_code, agents_idx, account),
        frame_length, force);
}

void resource_limits_manager::recall_proxied(
    const ram_payer_info& ram, int64_t now, symbol_code purpose_code, symbol_code token_code,
    account_name grantor_name, account_name agent_name, int16_t pct
) {
                        
    EOS_ASSERT(1 <= pct && pct <= config::_100percent, transaction_exception, "pct must be between 0.01% and 100% (1-10000)");
    const auto* param = _chaindb.find<stake_param_object, by_id>(token_code.value);
    EOS_ASSERT(param, transaction_exception, "no staking for token");
    EOS_ASSERT(std::find(param->purposes.begin(), param->purposes.end(), purpose_code) != param->purposes.end(), transaction_exception, "unknown purpose");

    auto agents_table = _chaindb.get_table<stake_agent_object>();
    auto grants_table = _chaindb.get_table<stake_grant_object>();
    auto agents_idx = agents_table.get_index<stake_agent_object::by_key>();
    auto grants_idx = grants_table.get_index<stake_grant_object::by_key>();

    auto grantor_as_agent = get_agent(purpose_code, token_code, agents_idx, grantor_name);
    
    update_proxied_traversal(ram, now, purpose_code, token_code, agents_idx, grants_idx, grantor_as_agent, param->frame_length, false);
    
    int64_t amount = 0;
    auto grant_itr = grants_idx.lower_bound(grant_key(purpose_code, token_code, grantor_name));
    while ((grant_itr != grants_idx.end()) &&
           (grant_itr->purpose_code   == purpose_code) &&
           (grant_itr->token_code   == token_code) &&
           (grant_itr->grantor_name == grantor_name))
    {
        if (grant_itr->agent_name == agent_name) {
            auto to_recall = get_prop(grant_itr->share, pct);
            amount = recall_proxied_traversal(ram, purpose_code, token_code, agents_idx, grants_idx, grant_itr->agent_name, to_recall, grant_itr->break_fee);
            if (grant_itr->pct || grant_itr->share > to_recall) {
                grants_table.modify(*grant_itr, [&](auto& g) { g.share -= to_recall; });
                ++grant_itr;
            }
            else {
                const auto &cur_grant = *grant_itr;
                ++grant_itr;
                grants_table.erase(cur_grant, ram);
            }
            break;
        }
        else
            ++grant_itr;
    }
    
    EOS_ASSERT(amount > 0, transaction_exception, "amount to recall must be positive");
    agents_table.modify(*grantor_as_agent, [&](auto& a) {
        a.balance += amount;
        a.proxied -= amount;
    });
}


} } } /// eosio::chain::resource_limits
