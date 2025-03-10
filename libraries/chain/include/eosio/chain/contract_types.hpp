#pragma once

#include <eosio/chain/authority.hpp>
#include <eosio/chain/chain_config.hpp>
#include <eosio/chain/config.hpp>
#include <eosio/chain/types.hpp>

namespace eosio { namespace chain {

using action_name    = eosio::chain::action_name;

struct newaccount {
   account_name                     creator;
   account_name                     name;
   authority                        owner;
   authority                        active;

   static account_name get_account() {
      return config::system_account_name;
   }

   static action_name get_name() {
      return N(newaccount);
   }
};

struct setcode {
   account_name                     account;
   uint8_t                          vmtype = 0;
   uint8_t                          vmversion = 0;
   bytes                            code;

   static account_name get_account() {
      return config::system_account_name;
   }

   static action_name get_name() {
      return N(setcode);
   }
};

struct setabi {
   account_name                     account;
   bytes                            abi;

   static account_name get_account() {
      return config::system_account_name;
   }

   static action_name get_name() {
      return N(setabi);
   }
};


struct updateauth {
   account_name                      account;
   permission_name                   permission;
   permission_name                   parent;
   authority                         auth;

   static account_name get_account() {
      return config::system_account_name;
   }

   static action_name get_name() {
      return N(updateauth);
   }
};

struct deleteauth {
   deleteauth() = default;
   deleteauth(const account_name& account, const permission_name& permission)
   :account(account), permission(permission)
   {}

   account_name                      account;
   permission_name                   permission;

   static account_name get_account() {
      return config::system_account_name;
   }

   static action_name get_name() {
      return N(deleteauth);
   }
};

struct linkauth {
   linkauth() = default;
   linkauth(const account_name& account, const account_name& code, const action_name& type, const permission_name& requirement)
   :account(account), code(code), type(type), requirement(requirement)
   {}

   account_name                      account;
   account_name                      code;
   action_name                       type;
   permission_name                   requirement;

   static account_name get_account() {
      return config::system_account_name;
   }

   static action_name get_name() {
      return N(linkauth);
   }
};

struct unlinkauth {
   unlinkauth() = default;
   unlinkauth(const account_name& account, const account_name& code, const action_name& type)
   :account(account), code(code), type(type)
   {}

   account_name                      account;
   account_name                      code;
   action_name                       type;

   static account_name get_account() {
      return config::system_account_name;
   }

   static action_name get_name() {
      return N(unlinkauth);
   }
};

struct canceldelay {
   permission_level      canceling_auth;
   transaction_id_type   trx_id;

   static account_name get_account() {
      return config::system_account_name;
   }

   static action_name get_name() {
      return N(canceldelay);
   }
};

struct providebw {
    account_name    provider;
    account_name    account;

    providebw() = default;
    providebw(const account_name& provider, const account_name& account)
    : provider(provider), account(account)
    {}

    static account_name get_account() {
        return config::system_account_name;
    }

    static action_name get_name() {
        return config::provide_bw_action;
    }
};

struct requestbw {
    account_name    provider;
    account_name    account;

    requestbw() = default;
    requestbw(const account_name& provider, const account_name& account)
    : provider(provider), account(account)
    {}

    static account_name get_account() {
        return config::system_account_name;
    }

    static action_name get_name() {
        return config::request_bw_action;
    }
};

struct provideram {
    account_name        provider;
    account_name        account;
    std::vector<account_name>   contracts;

    provideram() = default;
    provideram(const account_name& provider, const account_name& account, std::vector<name> contracts)
        : provider(provider), account(account), contracts(std::move(contracts))
    {}

    static account_name get_account() {
        return config::system_account_name;
    }

    static action_name get_name() {
        return N(provideram);
    }
};


// it's ugly, but removes boilerplate. TODO: write better
#define SYS_ACTION_STRUCT(NAME) struct NAME { \
    NAME() = default; \
    static account_name get_account() { return config::domain_account_name; } \
    static action_name get_name()     { return N(NAME); }
#define SYS_ACTION_STRUCT_END };

SYS_ACTION_STRUCT(newdomain)
   account_name creator;
   domain_name name;
SYS_ACTION_STRUCT_END;

SYS_ACTION_STRUCT(passdomain)
   account_name from;
   account_name to;
   domain_name name;
SYS_ACTION_STRUCT_END;

SYS_ACTION_STRUCT(linkdomain)
   account_name owner;
   account_name to;
   domain_name name;
SYS_ACTION_STRUCT_END;

SYS_ACTION_STRUCT(unlinkdomain)
   account_name owner;
   domain_name name;
SYS_ACTION_STRUCT_END;

SYS_ACTION_STRUCT(newusername)
   account_name creator;
   account_name owner;
   username name;
SYS_ACTION_STRUCT_END;

#undef SYS_ACTION_STRUCT
#undef SYS_ACTION_STRUCT_END


struct onerror {
   uint128_t      sender_id;
   bytes          sent_trx;

   onerror( uint128_t sid, const char* data, size_t len )
   :sender_id(sid),sent_trx(data,data+len){}

   static account_name get_account() {
      return config::system_account_name;
   }

   static action_name get_name() {
      return N(onerror);
   }
};

struct approvebw {
    account_name    account;

    approvebw() = default;
    approvebw(const account_name& account)
    : account(account)
    {}

    static account_name get_account() {
       return config::system_account_name;
    }

    static action_name get_name() {
        return config::approve_bw_action;
    }
};

} } /// namespace eosio::chain

FC_REFLECT( eosio::chain::newaccount                       , (creator)(name)(owner)(active) )
FC_REFLECT( eosio::chain::setcode                          , (account)(vmtype)(vmversion)(code) )
FC_REFLECT( eosio::chain::setabi                           , (account)(abi) )
FC_REFLECT( eosio::chain::updateauth                       , (account)(permission)(parent)(auth) )
FC_REFLECT( eosio::chain::deleteauth                       , (account)(permission) )
FC_REFLECT( eosio::chain::linkauth                         , (account)(code)(type)(requirement) )
FC_REFLECT( eosio::chain::unlinkauth                       , (account)(code)(type) )
FC_REFLECT( eosio::chain::providebw                        , (provider)(account) )
FC_REFLECT( eosio::chain::requestbw                        , (provider)(account) )
FC_REFLECT( eosio::chain::approvebw                        , (account) )
FC_REFLECT( eosio::chain::provideram                       , (provider)(account)(contracts) )
FC_REFLECT( eosio::chain::canceldelay                      , (canceling_auth)(trx_id) )
FC_REFLECT( eosio::chain::onerror                          , (sender_id)(sent_trx) )

FC_REFLECT(eosio::chain::newdomain,    (creator)(name))
FC_REFLECT(eosio::chain::passdomain,   (from)(to)(name))
FC_REFLECT(eosio::chain::linkdomain,   (owner)(to)(name))
FC_REFLECT(eosio::chain::unlinkdomain, (owner)(name))
FC_REFLECT(eosio::chain::newusername,  (creator)(owner)(name))
