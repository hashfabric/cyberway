#pragma once

#include <fc/variant.hpp>

#include <cyberway/chaindb/controller.hpp>
#include <cyberway/chaindb/object_value.hpp>

namespace cyberway { namespace chaindb {

    using fc::variant;

    using eosio::chain::bytes;

    struct cursor_info {
        cursor_t      id = invalid_cursor;
        index_info    index;
        primary_key_t pk = end_primary_key;
        bytes         blob; // serialized by controller value
    }; // struct cursor_info

    class driver_interface {
    public:
        virtual ~driver_interface();

        virtual void drop_db() = 0;

        virtual std::vector<table_def> db_tables(const account_name& code) const = 0;
        virtual void create_index(const index_info&) const = 0;
        virtual void drop_index(const index_info&) const = 0;
        virtual void drop_table(const table_info&) const = 0;

        virtual const cursor_info& clone(const cursor_request&) = 0;

        virtual void close(const cursor_request&) = 0;
        virtual void close_code_cursors(const account_name& code) = 0;

        virtual void apply_all_changes() = 0;
        virtual void apply_code_changes(const account_name& code) = 0;

        virtual const cursor_info& lower_bound(index_info, variant key) = 0;
        virtual const cursor_info& upper_bound(index_info, variant key) = 0;
        virtual const cursor_info& find(index_info, primary_key_t, variant key) = 0;
        virtual const cursor_info& opt_find_by_pk(index_info, primary_key_t, variant key) = 0;

        virtual const cursor_info& begin(index_info) = 0;
        virtual const cursor_info& end(index_info) = 0;

        virtual const cursor_info& current(const cursor_info&) = 0;
        virtual const cursor_info& current(const cursor_request&) = 0;
        virtual const cursor_info& next(const cursor_request&) = 0;
        virtual const cursor_info& next(const cursor_info&) = 0;
        virtual const cursor_info& prev(const cursor_request&) = 0;
        virtual const cursor_info& prev(const cursor_info&) = 0;

        virtual       object_value  object_by_pk(const table_info&, primary_key_t) = 0;
        virtual const object_value& object_at_cursor(const cursor_info&) = 0;
        virtual       void          set_blob(const cursor_info&, bytes blob) = 0;

        virtual primary_key_t available_pk(const table_info&) = 0;
    }; // class driver_interface

} } // namespace cyberway::chaindb
