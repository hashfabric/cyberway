#include <algorithm>
#include <eosio/chain/apply_context.hpp>
#include <eosio/chain/controller.hpp>
#include <eosio/chain/transaction_context.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/wasm_interface.hpp>
#include <eosio/chain/generated_transaction_object.hpp>
#include <eosio/chain/authorization_manager.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/account_object.hpp>
#include <eosio/chain/global_property_object.hpp>
#include <boost/container/flat_set.hpp>

#include <cyberway/chaindb/controller.hpp>

using boost::container::flat_set;

namespace eosio { namespace chain {

static inline void print_debug(account_name receiver, const action_trace& ar) {
   if (!ar.console.empty()) {
      auto prefix = fc::format_string(
                                      "\n[(${a},${n})->${r}]",
                                      fc::mutable_variant_object()
                                      ("a", ar.act.account)
                                      ("n", ar.act.name)
                                      ("r", receiver));
      dlog(prefix + ": CONSOLE OUTPUT BEGIN =====================\n"
           + ar.console
           + prefix + ": CONSOLE OUTPUT END   =====================" );
   }
}

void apply_context::exec_one( action_trace& trace )
{
   auto start = fc::time_point::now();

   action_receipt r;
   r.receiver         = receiver;
   r.act_digest       = digest_type::hash(act);

   trace.trx_id = trx_context.id;
   trace.block_num = control.pending_block_state()->block_num;
   trace.block_time = control.pending_block_time();
   trace.producer_block_id = control.pending_producer_block_id();
   trace.act = act;
   trace.context_free = context_free;

   const auto& cfg = control.get_global_properties().configuration;
   try {
      try {
         const auto& a = control.get_account( receiver );
         privileged = a.privileged;
         auto native = control.find_apply_handler( receiver, act.account, act.name );
         if( native ) {
            (*native)( *this );
         }

         if( a.code.size() > 0
             && !(act.account == config::system_account_name && act.name == N( setcode ) &&
                  receiver == config::system_account_name) ) {
            try {
               cyberway::chaindb::chaindb_guard guard(chaindb, receiver);
               control.get_wasm_interface().apply( a.code_version, a.code, *this );
               chaindb.apply_all_changes();
            } catch( const wasm_exit& ) {}
         }
      } FC_RETHROW_EXCEPTIONS(warn, "pending console output: ${console}", ("console", _pending_console_output.str()))
   } catch( fc::exception& e ) {
      trace.receipt = r; // fill with known data
      trace.except = e;
      finalize_trace( trace, start );
      throw;
   }

   r.global_sequence  = next_global_sequence();
   r.recv_sequence    = next_recv_sequence( receiver );

   const auto& account_sequence = chaindb.get<account_sequence_object, by_name>(act.account);
   r.code_sequence    = account_sequence.code_sequence; // could be modified by action execution above
   r.abi_sequence     = account_sequence.abi_sequence;  // could be modified by action execution above

   for( const auto& auth : act.authorization ) {
      r.auth_sequence[auth.actor] = next_auth_sequence( auth.actor );
   }

   trace.receipt = r;

   trx_context.executed.emplace_back( move(r) );

   finalize_trace( trace, start );

   if ( control.contracts_console() ) {
      print_debug(receiver, trace);
   }
}

void apply_context::lazy_init_chaindb_abi(account_name code) {
   if (chaindb.has_abi(code)) return;

   const auto& a = control.get_account(code);
   EOS_ASSERT(a.abi.size() > 0, cyberway::chaindb::no_abi_exception,
       "Account ${a} doesn't have ABI description", ("a", code));

   chaindb.add_abi(code, a.get_abi());
}

void apply_context::finalize_trace( action_trace& trace, const fc::time_point& start )
{
   trace.account_ram_deltas = std::move( _account_ram_deltas );
   _account_ram_deltas.clear();

   trace.console = _pending_console_output.str();
   reset_console();

   trace.elapsed = fc::time_point::now() - start;

   trace.events = _events;
   _events.clear();
}

void apply_context::exec( action_trace& trace )
{
   _notified.push_back(receiver);
   exec_one( trace );
   for( uint32_t i = 1; i < _notified.size(); ++i ) {
      receiver = _notified[i];
      trace.inline_traces.emplace_back( );
      exec_one( trace.inline_traces.back() );
   }

   if( _cfa_inline_actions.size() > 0 || _inline_actions.size() > 0 ) {
      EOS_ASSERT( recurse_depth < control.get_global_properties().configuration.max_inline_action_depth,
                  transaction_exception, "max inline action depth per transaction reached" );
   }

   for( const auto& inline_action : _cfa_inline_actions ) {
      trace.inline_traces.emplace_back();
      trx_context.dispatch_action( trace.inline_traces.back(), inline_action, inline_action.account, true, recurse_depth + 1 );
   }

   for( const auto& inline_action : _inline_actions ) {
      trace.inline_traces.emplace_back();
      trx_context.dispatch_action( trace.inline_traces.back(), inline_action, inline_action.account, false, recurse_depth + 1 );
   }

} /// exec()


bool apply_context::is_domain(const domain_name& domain) const {
   return nullptr != chaindb.find<domain_object,by_name>(domain);
}
bool apply_context::is_username(const account_name& scope, const username& name) const {
   return nullptr != chaindb.find<username_object,by_scope_name>(boost::make_tuple(scope,name));
}
account_name apply_context::get_domain_owner(const domain_name& domain) const {
   return control.get_domain(domain).owner;
}
account_name apply_context::resolve_domain(const domain_name& domain) const {
   return control.get_domain(domain).linked_to;
}
account_name apply_context::resolve_username(const account_name& scope, const username& name) const {
   return control.get_username(scope, name).owner;
}


bool apply_context::is_account( const account_name& account )const {
   return nullptr != chaindb.find<account_object,by_name>( account );
}

void apply_context::require_authorization( const account_name& account ) {
   for( uint32_t i=0; i < act.authorization.size(); i++ ) {
     if( act.authorization[i].actor == account ) {
        used_authorizations[i] = true;
        return;
     }
   }
   EOS_ASSERT( false, missing_auth_exception, "missing authority of ${account}", ("account",account));
}

bool apply_context::has_authorization( const account_name& account )const {
   for( const auto& auth : act.authorization )
     if( auth.actor == account )
        return true;
  return false;
}

void apply_context::require_authorization(const account_name& account,
                                          const permission_name& permission) {
  for( uint32_t i=0; i < act.authorization.size(); i++ )
     if( act.authorization[i].actor == account ) {
        if( act.authorization[i].permission == permission ) {
           used_authorizations[i] = true;
           return;
        }
     }
  EOS_ASSERT( false, missing_auth_exception, "missing authority of ${account}/${permission}",
              ("account",account)("permission",permission) );
}

bool apply_context::has_recipient( account_name code )const {
   for( auto a : _notified )
      if( a == code )
         return true;
   return false;
}

void apply_context::require_recipient( account_name recipient ) {
   if( !has_recipient(recipient) ) {
      _notified.push_back(recipient);
   }
}


/**
 *  This will execute an action after checking the authorization. Inline transactions are
 *  implicitly authorized by the current receiver (running code). This method has significant
 *  security considerations and several options have been considered:
 *
 *  1. priviledged accounts (those marked as such by block producers) can authorize any action
 *  2. all other actions are only authorized by 'receiver' which means the following:
 *         a. the user must set permissions on their account to allow the 'receiver' to act on their behalf
 *
 *  Discarded Implemenation:  at one point we allowed any account that authorized the current transaction
 *   to implicitly authorize an inline transaction. This approach would allow privelege escalation and
 *   make it unsafe for users to interact with certain contracts.  We opted instead to have applications
 *   ask the user for permission to take certain actions rather than making it implicit. This way users
 *   can better understand the security risk.
 */
void apply_context::execute_inline( action&& a ) {
   auto* code = chaindb.find<account_object, by_name>(a.account);
   EOS_ASSERT( code != nullptr, action_validate_exception,
               "inline action's code account ${account} does not exist", ("account", a.account) );

   flat_set<account_name> actors;

   bool disallow_send_to_self_bypass = false; // eventually set to whether the appropriate protocol feature has been activated
   bool send_to_self = (a.account == receiver);
   bool inherit_parent_authorizations = (!disallow_send_to_self_bypass && send_to_self && (receiver == act.account) && control.is_producing_block());

   flat_set<permission_level> inherited_authorizations;
   if( inherit_parent_authorizations ) {
      inherited_authorizations.reserve( a.authorization.size() );
   }

   for( const auto& auth : a.authorization ) {
      auto* actor = chaindb.find<account_object, by_name>(auth.actor);
      EOS_ASSERT( actor != nullptr, action_validate_exception,
                  "inline action's authorizing actor ${account} does not exist", ("account", auth.actor) );
      EOS_ASSERT( control.get_authorization_manager().find_permission(auth) != nullptr, action_validate_exception,
                  "inline action's authorizations include a non-existent permission: ${permission}",
                  ("permission", auth) );

      if( inherit_parent_authorizations && std::find(act.authorization.begin(), act.authorization.end(), auth) != act.authorization.end() ) {
         inherited_authorizations.insert( auth );
      }
   }

   // No need to check authorization if replaying irreversible blocks or contract is privileged
   if( !control.skip_auth_check() && !privileged ) {
      try {
         control.get_authorization_manager()
                .check_authorization( {a},
                                      {},
                                      {{receiver, config::eosio_code_name}},
                                      control.pending_block_time() - trx_context.published,
                                      std::bind(&transaction_context::checktime, &this->trx_context),
                                      false,
                                      inherited_authorizations
                                    );

         //QUESTION: Is it smart to allow a deferred transaction that has been delayed for some time to get away
         //          with sending an inline action that requires a delay even though the decision to send that inline
         //          action was made at the moment the deferred transaction was executed with potentially no forewarning?
      } catch( const fc::exception& e ) {
         if( disallow_send_to_self_bypass || !send_to_self ) {
            throw;
         } else if( control.is_producing_block() ) {
            subjective_block_production_exception new_exception(FC_LOG_MESSAGE( error, "Authorization failure with inline action sent to self"));
            for (const auto& log: e.get_log()) {
               new_exception.append_log(log);
            }
            throw new_exception;
         }
      } catch( ... ) {
         if( disallow_send_to_self_bypass || !send_to_self ) {
            throw;
         } else if( control.is_producing_block() ) {
            EOS_THROW(subjective_block_production_exception, "Unexpected exception occurred validating inline action sent to self");
         }
      }
   }

   _inline_actions.emplace_back( move(a) );
}

void apply_context::execute_context_free_inline( action&& a ) {
   auto* code = chaindb.find<account_object, by_name>(a.account);
   EOS_ASSERT( code != nullptr, action_validate_exception,
               "inline action's code account ${account} does not exist", ("account", a.account) );

   EOS_ASSERT( a.authorization.size() == 0, action_validate_exception,
               "context-free actions cannot have authorizations" );

   _cfa_inline_actions.emplace_back( move(a) );
}


void apply_context::schedule_deferred_transaction( const uint128_t& sender_id, account_name payer, transaction&& trx, bool replace_existing ) {
   EOS_ASSERT( trx.context_free_actions.size() == 0, cfa_inside_generated_tx, "context free actions are not currently allowed in generated transactions" );
   trx.expiration = control.pending_block_time() + fc::microseconds(999'999); // Rounds up to nearest second (makes expiration check unnecessary)
   trx.set_reference_block(control.head_block_id()); // No TaPoS check necessary

   trx_context.validate_referenced_accounts(trx);

   // Charge ahead of time for the additional net usage needed to retire the deferred transaction
   // whether that be by successfully executing, soft failure, hard failure, or expiration.
   const auto& cfg = control.get_global_properties().configuration;
   trx_context.add_net_usage( static_cast<uint64_t>(cfg.base_per_transaction_net_usage)
                               + static_cast<uint64_t>(config::transaction_id_net_usage) ); // Will exit early if net usage cannot be payed.

   auto delay = fc::seconds(trx.delay_sec);

   if( !control.skip_auth_check() && !privileged ) { // Do not need to check authorization if replayng irreversible block or if contract is privileged
      if( payer != receiver ) {
         require_authorization(payer); /// uses payer's storage
      }

      // Originally this code bypassed authorization checks if a contract was deferring only actions to itself.
      // The idea was that the code could already do whatever the deferred transaction could do, so there was no point in checking authorizations.
      // But this is not true. The original implementation didn't validate the authorizations on the actions which allowed for privilege escalation.
      // It would make it possible to bill RAM to some unrelated account.
      // Furthermore, even if the authorizations were forced to be a subset of the current action's authorizations, it would still violate the expectations
      // of the signers of the original transaction, because the deferred transaction would allow billing more CPU and network bandwidth than the maximum limit
      // specified on the original transaction.
      // So, the deferred transaction must always go through the authorization checking if it is not sent by a privileged contract.
      // However, the old logic must still be considered because it cannot objectively change until a consensus protocol upgrade.

      bool disallow_send_to_self_bypass = false; // eventually set to whether the appropriate protocol feature has been activated

      auto is_sending_only_to_self = [&trx]( const account_name& self ) {
         bool send_to_self = true;
         for( const auto& act : trx.actions ) {
            if( act.account != self ) {
               send_to_self = false;
               break;
            }
         }
         return send_to_self;
      };

      try {
         control.get_authorization_manager()
                .check_authorization( trx.actions,
                                      {},
                                      {{receiver, config::eosio_code_name}},
                                      delay,
                                      std::bind(&transaction_context::checktime, &this->trx_context),
                                      false
                                    );
      } catch( const fc::exception& e ) {
         if( disallow_send_to_self_bypass || !is_sending_only_to_self(receiver) ) {
            throw;
         } else if( control.is_producing_block() ) {
            subjective_block_production_exception new_exception(FC_LOG_MESSAGE( error, "Authorization failure with sent deferred transaction consisting only of actions to self"));
            for (const auto& log: e.get_log()) {
               new_exception.append_log(log);
            }
            throw new_exception;
         }
      } catch( ... ) {
         if( disallow_send_to_self_bypass || !is_sending_only_to_self(receiver) ) {
            throw;
         } else if( control.is_producing_block() ) {
            EOS_THROW(subjective_block_production_exception, "Unexpected exception occurred validating sent deferred transaction consisting only of actions to self");
         }
      }
   }

   uint32_t trx_size = 0;
   if ( auto ptr = chaindb.find<generated_transaction_object,by_sender_id>(boost::make_tuple(receiver, sender_id)) ) {
      EOS_ASSERT( replace_existing, deferred_tx_duplicate, "deferred transaction with the same sender_id and payer already exists" );

      // TODO: Remove the following subjective check when the deferred trx replacement RAM bug has been fixed with a hard fork.
      EOS_ASSERT( !control.is_producing_block(), subjective_block_production_exception,
                  "Replacing a deferred transaction is temporarily disabled." );

// TODO: Removed by CyberWay
//      // TODO: The logic of the next line needs to be incorporated into the next hard fork.
//      // add_ram_usage( ptr->payer, -(config::billable_size_v<generated_transaction_object> + ptr->packed_trx.size()) );

      chaindb.modify( *ptr, {*this, payer}, [&]( auto& gtx ) {
            gtx.sender      = receiver;
            gtx.sender_id   = sender_id;
            gtx.payer       = payer;
            gtx.published   = control.pending_block_time();
            gtx.delay_until = gtx.published + delay;
            gtx.expiration  = gtx.delay_until + fc::seconds(control.get_global_properties().configuration.deferred_trx_expiration_window);

            trx_size = gtx.set( trx );
         });
   } else {
      chaindb.emplace<generated_transaction_object>( {*this, payer}, [&]( auto& gtx ) {
            gtx.trx_id      = trx.id();
            gtx.sender      = receiver;
            gtx.sender_id   = sender_id;
            gtx.payer       = payer;
            gtx.published   = control.pending_block_time();
            gtx.delay_until = gtx.published + delay;
            gtx.expiration  = gtx.delay_until + fc::seconds(control.get_global_properties().configuration.deferred_trx_expiration_window);

            trx_size = gtx.set( trx );
         });
   }

// TODO: Removed by CyberWay
//   EOS_ASSERT( control.is_ram_billing_in_notify_allowed() || (receiver == act.account) || (receiver == payer) || privileged,
//               subjective_block_production_exception, "Cannot charge RAM to other accounts during notify." );
//   add_ram_usage( payer, (config::billable_size_v<generated_transaction_object> + trx_size) );
}

bool apply_context::cancel_deferred_transaction( const uint128_t& sender_id, account_name sender ) {
   auto trx_table = chaindb.get_table<generated_transaction_object>();
   const auto* gto = chaindb.find<generated_transaction_object,by_sender_id>(boost::make_tuple(sender, sender_id));
   if ( gto ) {
// TODO: Removed by CyberWay
//      add_ram_usage( gto->payer, -(config::billable_size_v<generated_transaction_object> + gto->packed_trx.size()) );
      trx_table.erase(*gto, {*this});
   }
   return gto;
}

void apply_context::push_event( event evt ) {
   _events.emplace_back(std::move(evt));
}

// TODO: Removed by CyberWay
//const table_id_object* apply_context::find_table( name code, name scope, name table ) {
//   return db.find<table_id_object, by_code_scope_table>(boost::make_tuple(code, scope, table));
//}
//
//const table_id_object& apply_context::find_or_create_table( name code, name scope, name table, const account_name &payer ) {
//   const auto* existing_tid =  db.find<table_id_object, by_code_scope_table>(boost::make_tuple(code, scope, table));
//   if (existing_tid != nullptr) {
//      return *existing_tid;
//   }
//
//   update_db_usage(payer, config::billable_size_v<table_id_object>);
//
//   return db.create<table_id_object>([&](table_id_object &t_id){
//      t_id.code = code;
//      t_id.scope = scope;
//      t_id.table = table;
//      t_id.payer = payer;
//   });
//}
//
//void apply_context::remove_table( const table_id_object& tid ) {
//   update_db_usage(tid.payer, - config::billable_size_v<table_id_object>);
//   db.remove(tid);
//}

vector<account_name> apply_context::get_active_producers() const {
   const auto& ap = control.active_producers();
   vector<account_name> accounts; accounts.reserve( ap.producers.size() );

   for(const auto& producer : ap.producers )
      accounts.push_back(producer.producer_name);

   return accounts;
}

void apply_context::reset_console() {
   _pending_console_output = std::ostringstream();
   _pending_console_output.setf( std::ios::scientific, std::ios::floatfield );
}

bytes apply_context::get_packed_transaction() {
   auto r = fc::raw::pack( static_cast<const transaction&>(trx_context.trx) );
   return r;
}

void apply_context::update_db_usage( const account_name& payer, int64_t delta ) {
   if( delta > 0 ) {
      if( !(privileged || payer == account_name(receiver)) ) {
         EOS_ASSERT( control.is_ram_billing_in_notify_allowed() || (receiver == act.account),
                     subjective_block_production_exception, "Cannot charge RAM to other accounts during notify." );
         require_authorization( payer );
      }
   }
   add_ram_usage(payer, delta);
}


int apply_context::get_action( uint32_t type, uint32_t index, char* buffer, size_t buffer_size )const
{
   const auto& trx = trx_context.trx;
   const action* act_ptr = nullptr;

   if( type == 0 ) {
      if( index >= trx.context_free_actions.size() )
         return -1;
      act_ptr = &trx.context_free_actions[index];
   }
   else if( type == 1 ) {
      if( index >= trx.actions.size() )
         return -1;
      act_ptr = &trx.actions[index];
   }

   EOS_ASSERT(act_ptr, action_not_found_exception, "action is not found" );

   auto ps = fc::raw::pack_size( *act_ptr );
   if( ps <= buffer_size ) {
      fc::datastream<char*> ds(buffer, buffer_size);
      fc::raw::pack( ds, *act_ptr );
   }
   return ps;
}

int apply_context::get_context_free_data( uint32_t index, char* buffer, size_t buffer_size )const
{
   const auto& trx = trx_context.trx;

   if( index >= trx.context_free_data.size() ) return -1;

   auto s = trx.context_free_data[index].size();
   if( buffer_size == 0 ) return s;

   auto copy_size = std::min( buffer_size, s );
   memcpy( buffer, trx.context_free_data[index].data(), copy_size );

   return copy_size;
}

// TODO: Removed by CyberWay
//int apply_context::db_store_i64( uint64_t scope, uint64_t table, const account_name& payer, uint64_t id, const char* buffer, size_t buffer_size ) {
//   return db_store_i64( receiver, scope, table, payer, id, buffer, buffer_size);
//}
//
//int apply_context::db_store_i64( uint64_t code, uint64_t scope, uint64_t table, const account_name& payer, uint64_t id, const char* buffer, size_t buffer_size ) {
////   require_write_lock( scope );
//   const auto& tab = find_or_create_table( code, scope, table, payer );
//   auto tableid = tab.id;
//
//   EOS_ASSERT( payer != account_name(), invalid_table_payer, "must specify a valid account to pay for new record" );
//
//   const auto& obj = db.create<key_value_object>( [&]( auto& o ) {
//      o.t_id        = tableid;
//      o.primary_key = id;
//      o.value.assign( buffer, buffer_size );
//      o.payer       = payer;
//   });
//
//   db.modify( tab, [&]( auto& t ) {
//     ++t.count;
//   });
//
//   int64_t billable_size = (int64_t)(buffer_size + config::billable_size_v<key_value_object>);
//   update_db_usage( payer, billable_size);
//
//   keyval_cache.cache_table( tab );
//   return keyval_cache.add( obj );
//}
//
//void apply_context::db_update_i64( int iterator, account_name payer, const char* buffer, size_t buffer_size ) {
//   const key_value_object& obj = keyval_cache.get( iterator );
//
//   const auto& table_obj = keyval_cache.get_table( obj.t_id );
//   EOS_ASSERT( table_obj.code == receiver, table_access_violation, "db access violation" );
//
////   require_write_lock( table_obj.scope );
//
//   const int64_t overhead = config::billable_size_v<key_value_object>;
//   int64_t old_size = (int64_t)(obj.value.size() + overhead);
//   int64_t new_size = (int64_t)(buffer_size + overhead);
//
//   if( payer == account_name() ) payer = obj.payer;
//
//   if( account_name(obj.payer) != payer ) {
//      // refund the existing payer
//      update_db_usage( obj.payer,  -(old_size) );
//      // charge the new payer
//      update_db_usage( payer,  (new_size));
//   } else if(old_size != new_size) {
//      // charge/refund the existing payer the difference
//      update_db_usage( obj.payer, new_size - old_size);
//   }
//
//   db.modify( obj, [&]( auto& o ) {
//     o.value.assign( buffer, buffer_size );
//     o.payer = payer;
//   });
//}
//
//void apply_context::db_remove_i64( int iterator ) {
//   const key_value_object& obj = keyval_cache.get( iterator );
//
//   const auto& table_obj = keyval_cache.get_table( obj.t_id );
//   EOS_ASSERT( table_obj.code == receiver, table_access_violation, "db access violation" );
//
////   require_write_lock( table_obj.scope );
//
//   update_db_usage( obj.payer,  -(obj.value.size() + config::billable_size_v<key_value_object>) );
//
//   db.modify( table_obj, [&]( auto& t ) {
//      --t.count;
//   });
//   db.remove( obj );
//
//   if (table_obj.count == 0) {
//      remove_table(table_obj);
//   }
//
//   keyval_cache.remove( iterator );
//}
//
//int apply_context::db_get_i64( int iterator, char* buffer, size_t buffer_size ) {
//   const key_value_object& obj = keyval_cache.get( iterator );
//
//   auto s = obj.value.size();
//   if( buffer_size == 0 ) return s;
//
//   auto copy_size = std::min( buffer_size, s );
//   memcpy( buffer, obj.value.data(), copy_size );
//
//   return copy_size;
//}
//
//int apply_context::db_next_i64( int iterator, uint64_t& primary ) {
//   if( iterator < -1 ) return -1; // cannot increment past end iterator of table
//
//   const auto& obj = keyval_cache.get( iterator ); // Check for iterator != -1 happens in this call
//   const auto& idx = db.get_index<key_value_index, by_scope_primary>();
//
//   auto itr = idx.iterator_to( obj );
//   ++itr;
//
//   if( itr == idx.end() || itr->t_id != obj.t_id ) return keyval_cache.get_end_iterator_by_table_id(obj.t_id);
//
//   primary = itr->primary_key;
//   return keyval_cache.add( *itr );
//}
//
//int apply_context::db_previous_i64( int iterator, uint64_t& primary ) {
//   const auto& idx = db.get_index<key_value_index, by_scope_primary>();
//
//   if( iterator < -1 ) // is end iterator
//   {
//      auto tab = keyval_cache.find_table_by_end_iterator(iterator);
//      EOS_ASSERT( tab, invalid_table_iterator, "not a valid end iterator" );
//
//      auto itr = idx.upper_bound(tab->id);
//      if( idx.begin() == idx.end() || itr == idx.begin() ) return -1; // Empty table
//
//      --itr;
//
//      if( itr->t_id != tab->id ) return -1; // Empty table
//
//      primary = itr->primary_key;
//      return keyval_cache.add(*itr);
//   }
//
//   const auto& obj = keyval_cache.get(iterator); // Check for iterator != -1 happens in this call
//
//   auto itr = idx.iterator_to(obj);
//   if( itr == idx.begin() ) return -1; // cannot decrement past beginning iterator of table
//
//   --itr;
//
//   if( itr->t_id != obj.t_id ) return -1; // cannot decrement past beginning iterator of table
//
//   primary = itr->primary_key;
//   return keyval_cache.add(*itr);
//}
//
//int apply_context::db_find_i64( uint64_t code, uint64_t scope, uint64_t table, uint64_t id ) {
//   //require_read_lock( code, scope ); // redundant?
//
//   const auto* tab = find_table( code, scope, table );
//   if( !tab ) return -1;
//
//   auto table_end_itr = keyval_cache.cache_table( *tab );
//
//   const key_value_object* obj = db.find<key_value_object, by_scope_primary>( boost::make_tuple( tab->id, id ) );
//   if( !obj ) return table_end_itr;
//
//   return keyval_cache.add( *obj );
//}
//
//int apply_context::db_lowerbound_i64( uint64_t code, uint64_t scope, uint64_t table, uint64_t id ) {
//   //require_read_lock( code, scope ); // redundant?
//
//   const auto* tab = find_table( code, scope, table );
//   if( !tab ) return -1;
//
//   auto table_end_itr = keyval_cache.cache_table( *tab );
//
//   const auto& idx = db.get_index<key_value_index, by_scope_primary>();
//   auto itr = idx.lower_bound( boost::make_tuple( tab->id, id ) );
//   if( itr == idx.end() ) return table_end_itr;
//   if( itr->t_id != tab->id ) return table_end_itr;
//
//   return keyval_cache.add( *itr );
//}
//
//int apply_context::db_upperbound_i64( uint64_t code, uint64_t scope, uint64_t table, uint64_t id ) {
//   //require_read_lock( code, scope ); // redundant?
//
//   const auto* tab = find_table( code, scope, table );
//   if( !tab ) return -1;
//
//   auto table_end_itr = keyval_cache.cache_table( *tab );
//
//   const auto& idx = db.get_index<key_value_index, by_scope_primary>();
//   auto itr = idx.upper_bound( boost::make_tuple( tab->id, id ) );
//   if( itr == idx.end() ) return table_end_itr;
//   if( itr->t_id != tab->id ) return table_end_itr;
//
//   return keyval_cache.add( *itr );
//}
//
//int apply_context::db_end_i64( uint64_t code, uint64_t scope, uint64_t table ) {
//   //require_read_lock( code, scope ); // redundant?
//
//   const auto* tab = find_table( code, scope, table );
//   if( !tab ) return -1;
//
//   return keyval_cache.cache_table( *tab );
//}

uint64_t apply_context::next_global_sequence() {
   const auto& p = control.get_dynamic_global_properties();
   chaindb.modify( p, [&]( auto& dgp ) {
      ++dgp.global_action_sequence;
   });
   return p.global_action_sequence;
}

uint64_t apply_context::next_recv_sequence( account_name receiver ) {
   const auto& rs = chaindb.get<account_sequence_object,by_name>( receiver );
   chaindb.modify( rs, [&]( auto& mrs ) {
      ++mrs.recv_sequence;
   });
   return rs.recv_sequence;
}
uint64_t apply_context::next_auth_sequence( account_name actor ) {
   const auto& rs = chaindb.get<account_sequence_object,by_name>( actor );
   chaindb.modify( rs, [&](auto& mrs ){
      ++mrs.auth_sequence;
   });
   return rs.auth_sequence;
}

void apply_context::add_ram_usage( account_name account, int64_t ram_delta ) {
   trx_context.add_ram_usage( account, ram_delta );

   auto p = _account_ram_deltas.emplace( account, ram_delta );
   if( !p.second ) {
      p.first->delta += ram_delta;
   }
}


} } /// eosio::chain
