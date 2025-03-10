/**
 *  @file
 *  @copyright defined in eos/LICENSE
 */
#pragma once
#include <chainbase/chainbase.hpp>
#include <eosio/chain/transaction.hpp>
#include <eosio/chain/config.hpp>

#include <type_traits>

namespace eosio { namespace chain {


struct permission_level_weight {
   permission_level  permission;
   weight_type       weight;

   friend bool operator == ( const permission_level_weight& lhs, const permission_level_weight& rhs ) {
      return tie( lhs.permission, lhs.weight ) == tie( rhs.permission, rhs.weight );
   }
};

struct key_weight {
   public_key_type key;
   weight_type     weight;

   friend bool operator == ( const key_weight& lhs, const key_weight& rhs ) {
      return tie( lhs.key, lhs.weight ) == tie( rhs.key, rhs.weight );
   }
};

struct wait_weight {
   uint32_t     wait_sec;
   weight_type  weight;

   friend bool operator == ( const wait_weight& lhs, const wait_weight& rhs ) {
      return tie( lhs.wait_sec, lhs.weight ) == tie( rhs.wait_sec, rhs.weight );
   }
};

namespace config {
   template<>
   struct billable_size<permission_level_weight> {
      static const uint64_t value = 24; ///< over value of weight for safety
   };

   template<>
   struct billable_size<key_weight> {
      static const uint64_t value = 8; ///< over value of weight for safety, dynamically sizing key
   };

   template<>
   struct billable_size<wait_weight> {
      static const uint64_t value = 16; ///< over value of weight and wait_sec for safety
   };
}

struct authority {
   authority( public_key_type k, uint32_t delay_sec = 0 )
   :threshold(1),keys({{k,1}})
   {
      if( delay_sec > 0 ) {
         threshold = 2;
         waits.push_back(wait_weight{delay_sec, 1});
      }
   }

   authority( uint32_t t, vector<key_weight> k, vector<permission_level_weight> p = {}, vector<wait_weight> w = {} )
   :threshold(t),keys(move(k)),accounts(move(p)),waits(move(w)){}
   authority(){}

   uint32_t                          threshold = 0;
   vector<key_weight>                keys;
   vector<permission_level_weight>   accounts;
   vector<wait_weight>               waits;

   friend bool operator == ( const authority& lhs, const authority& rhs ) {
      return tie( lhs.threshold, lhs.keys, lhs.accounts, lhs.waits ) == tie( rhs.threshold, rhs.keys, rhs.accounts, rhs.waits );
   }

   friend bool operator != ( const authority& lhs, const authority& rhs ) {
      return tie( lhs.threshold, lhs.keys, lhs.accounts, lhs.waits ) != tie( rhs.threshold, rhs.keys, rhs.accounts, rhs.waits );
   }
};


struct shared_authority {
   shared_authority() = default;

   shared_authority& operator=(const authority& a) {
      threshold = a.threshold;
      keys = decltype(keys)(a.keys.begin(), a.keys.end(), keys.get_allocator());
      accounts = decltype(accounts)(a.accounts.begin(), a.accounts.end(), accounts.get_allocator());
      waits = decltype(waits)(a.waits.begin(), a.waits.end(), waits.get_allocator());
      return *this;
   }

   uint32_t                            threshold = 0;
   vector<key_weight>                  keys;
   vector<permission_level_weight>     accounts;
   vector<wait_weight>                 waits;

   operator authority()const { return to_authority(); }
   authority to_authority()const {
      authority auth;
      auth.threshold = threshold;
      auth.keys.reserve(keys.size());
      auth.accounts.reserve(accounts.size());
      auth.waits.reserve(waits.size());
      for( const auto& k : keys ) { auth.keys.emplace_back( k ); }
      for( const auto& a : accounts ) { auth.accounts.emplace_back( a ); }
      for( const auto& w : waits ) { auth.waits.emplace_back( w ); }
      return auth;
   }

   size_t get_billable_size() const {
      size_t accounts_size = accounts.size() * config::billable_size_v<permission_level_weight>;
      size_t waits_size = waits.size() * config::billable_size_v<wait_weight>;
      size_t keys_size = 0;
      for (const auto& k: keys) {
         keys_size += config::billable_size_v<key_weight>;
         keys_size += fc::raw::pack_size(k.key);  ///< serialized size of the key
      }

      return accounts_size + waits_size + keys_size;
   }
};

namespace config {
   template<>
   struct billable_size<shared_authority> {
      static const uint64_t value = (3 * config::fixed_overhead_shared_vector_ram_bytes) + 4;
   };
}

/**
 * Makes sure all keys are unique and sorted and all account permissions are unique and sorted and that authority can
 * be satisfied
 */
template<typename Authority>
inline bool validate( const Authority& auth ) {
   decltype(auth.threshold) total_weight = 0;

   static_assert( std::is_same<decltype(auth.threshold), uint32_t>::value &&
                  std::is_same<weight_type, uint16_t>::value &&
                  std::is_same<typename decltype(auth.keys)::value_type, key_weight>::value &&
                  std::is_same<typename decltype(auth.accounts)::value_type, permission_level_weight>::value &&
                  std::is_same<typename decltype(auth.waits)::value_type, wait_weight>::value,
                  "unexpected type for threshold and/or weight in authority" );

   if( ( auth.keys.size() + auth.accounts.size() + auth.waits.size() ) > (1 << 16) )
      return false; // overflow protection (assumes weight_type is uint16_t and threshold is of type uint32_t)

   if( auth.threshold == 0 )
      return false;

   {
      const key_weight* prev = nullptr;
      for( const auto& k : auth.keys ) {
         if( prev && !(prev->key < k.key) ) return false; // TODO: require keys to be sorted in ascending order rather than descending (requires modifying many tests)
         total_weight += k.weight;
         prev = &k;
      }
   }
   {
      const permission_level_weight* prev = nullptr;
      for( const auto& a : auth.accounts ) {
         if( prev && ( prev->permission >= a.permission ) ) return false; // TODO: require permission_levels to be sorted in ascending order rather than descending (requires modifying many tests)
         total_weight += a.weight;
         prev = &a;
      }
   }
   {
      const wait_weight* prev = nullptr;
      if( auth.waits.size() > 0 && auth.waits.front().wait_sec == 0 )
         return false;
      for( const auto& w : auth.waits ) {
         if( prev && ( prev->wait_sec >= w.wait_sec ) ) return false;
         total_weight += w.weight;
         prev = &w;
      }
   }

   return total_weight >= auth.threshold;
}

} } // namespace eosio::chain


FC_REFLECT(eosio::chain::permission_level_weight, (permission)(weight) )
FC_REFLECT(eosio::chain::key_weight, (key)(weight) )
FC_REFLECT(eosio::chain::wait_weight, (wait_sec)(weight) )
FC_REFLECT(eosio::chain::authority, (threshold)(keys)(accounts)(waits) )
FC_REFLECT(eosio::chain::shared_authority, (threshold)(keys)(accounts)(waits) )
