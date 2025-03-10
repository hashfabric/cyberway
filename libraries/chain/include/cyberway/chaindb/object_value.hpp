#pragma once

#include <cyberway/chaindb/common.hpp>

namespace cyberway { namespace chaindb {

    enum class undo_record {
        Unknown,
        OldValue,
        RemovedValue,
        NewValue,
        NextPk,
    }; // enum class undo_type

    struct service_state final {
        primary_key_t pk       = unset_primary_key;
        account_name  payer;
        size_t        size     = 0;

        account_name  code;
        account_name  scope;
        table_name    table;

        revision_t    revision = impossible_revision;

        primary_key_t undo_pk  = unset_primary_key;
        undo_record   undo_rec = undo_record::Unknown;

        revision_t    undo_revision = impossible_revision;
        account_name  undo_payer;
        size_t        undo_size     = 0;

        service_state(const table_info& table, primary_key_t pk)
        : pk(pk), code(table.code), scope(table.scope), table(table.table->name) {
        }

        service_state(const table_info& table, primary_key_t undo_pk, undo_record rec, revision_t rev)
        : code(table.code), scope(table.scope), table(table.table->name),
          revision(rev), undo_pk(undo_pk), undo_rec(rec) {
        }

        service_state() = default;
        service_state(service_state&&) = default;
        service_state(const service_state&) = default;

        service_state& operator=(service_state&&) = default;
        service_state& operator=(const service_state&) = default;

        void clear() {
            *this = {};
        }
    }; // struct service_state

    struct object_value final {
        service_state service;
        fc::variant   value;

        bool is_null() const {
            return value.get_type() == fc::variant::type_id::null_type;
        }

        object_value clone_service() const {
            object_value obj;
            obj.service = service;
            return obj;
        }

        void clear() {
            service.clear();
            value.clear();
        }

        primary_key_t pk() const {
            return service.pk;
        }
    }; // struct object_value

} } // namespace cyberway::chaindb

FC_REFLECT_ENUM(cyberway::chaindb::undo_record, (Unknown)(OldValue)(RemovedValue)(NewValue)(NextPk))