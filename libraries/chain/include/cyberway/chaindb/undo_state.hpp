#pragma once

#include <cyberway/chaindb/common.hpp>
#include <cyberway/chaindb/object_value.hpp>

namespace cyberway { namespace chaindb {

    class cache_map;
    class driver_interface;
    class journal;
    struct table_info;

    class undo_stack final {
    public:
        undo_stack(chaindb_controller&, driver_interface&, journal&, cache_map&);

        undo_stack(const undo_stack&) = delete;
        undo_stack(undo_stack&&) = delete;

        ~undo_stack();

        void add_abi_tables(eosio::chain::abi_def&) const;

        void restore();

        void clear();

        chaindb_session start_undo_session(bool enabled);

        void set_revision(revision_t rev);
        revision_t revision() const;
        bool enabled() const;

        void apply_changes(revision_t rev);

        /**
         *  Restores the state to how it was prior to the current session discarding all changes
         *  made between the last revision and the current revision.
         */
        void undo(revision_t rev);

        /**
         *  This method works similar to git squash, it merges the change set from the two most
         *  recent revision numbers into one revision number (reducing the head revision number)
         *
         *  This method does not change the state of the index, only the state of the undo buffer.
         */
        void squash(revision_t rev);

        /**
         * Discards all undo history prior to revision
         */
        void commit(revision_t rev);

        /**
         * Unwinds all undo states
         */
        void undo_all();
        void undo();

        /**
         * Event on create objects
         */
        void insert(const table_info&, object_value obj);

        /**
         * Event on modify objects
         */
        void update(const table_info&, object_value orig_obj, object_value obj);

        /**
         * Event on remove objects
         */
        void remove(const table_info&, object_value orig_obj);

    private:
        struct undo_stack_impl_;
        std::unique_ptr<undo_stack_impl_> impl_;
    }; // class table_undo_stack

} } // namespace cyberway::chaindb