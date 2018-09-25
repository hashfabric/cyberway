#include <eosiolib/eosio.hpp>
#include <eosiolib/multi_index.hpp>

#include <eosio.system/native.hpp>

using namespace eosio;

class hello : public eosio::contract {
  public:
      using contract::contract;

      /// @abi action 
      void hi( account_name user ) {
         require_auth(_self);
         print( "Hello, ", name{user} );
      }


      std::string to_hex(const char* ptr, uint32_t size) {
         std::string data;
         static const char* table = "0123456789ABCDEF";
         for(unsigned i = 0; i < size; i++, ptr++) {
            data += table[((*ptr) >> 4) & 0xF];
            data += table[(*ptr) & 0xF];
         }
         return data;
      }


      /// Action for voting for *user* with weight *count*.
      /// 5 users with most votes added in *post* authority of
      /// smart-contract account.
      /// Note: add "<account>@eosio.code" into *active* authority for 
      /// smart-contract account (change <account> with actual name).

      /// @abi action
      void vote( account_name user, uint32_t count) {
         votes votes(_self, _self);

         auto itr = votes.find(user);
         if (itr == votes.end()) {
            votes.emplace(user, [&](auto& a) {
                a.account = user;
                a.votes = count;
            });
         } else {
            votes.modify(itr, 0, [&](auto& a) {
               a.votes += count;
            });
         }

         auto vidx = votes.get_index<N(byvote)>();

         eosiosystem::authority auth;
         auth.threshold = 3;

         std::set<account_name> accounts;

         for(const auto& item : vidx) {
            print("  ", name{item.account}, "   ", item.votes, "\n");
            accounts.insert(item.account);
            if(accounts.size() >= 5) break;
         }

         for(const auto& item : accounts) {
            print("-> ", name{item}, "\n");
            auth.accounts.push_back({{item,N(active)},1});
         }

         if(auth.accounts.size() < auth.threshold)
            auth.threshold = auth.accounts.size();

         print("Total auth: ", auth.accounts.size());
         for(const auto& it : auth.accounts) {
            print("   ", name{it.permission.actor}, "@", name{it.permission.permission}, /*"=", it.weight,*/ "\n");
         }

         const auto& act = action(
            permission_level{_self,N(active)},
            N(eosio), N(updateauth),
            std::make_tuple(_self, N(post), N(active), auth)
         );
         act.send();

         auto serialized = pack(act);
         print("Data: ", to_hex(serialized.data(), serialized.size()).c_str(), "\n");

         action act2 = unpack<action>(serialized.data(), serialized.size());
         print("Action data: ", to_hex(act2.data.data(), act2.data.size()).c_str(), "\n");

      }

      /// @abi table
      struct voteobj {
         account_name   account;
         uint64_t       votes;

         auto primary_key() const {return account;}
         uint64_t get_votes() const {return (~0ul)-votes;}

         EOSLIB_SERIALIZE(voteobj, (account)(votes))
      };

      using votes = eosio::multi_index<N(voteobj), voteobj,
         indexed_by<N(byvote), const_mem_fun<voteobj, uint64_t, &voteobj::get_votes>>>;
};

EOSIO_ABI( hello, (hi)(vote) )
