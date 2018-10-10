#include <bsoncxx/builder/basic/kvp.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/exception/exception.hpp>
#include <bsoncxx/json.hpp>

#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/exception/operation_exception.hpp>
#include <mongocxx/exception/logic_error.hpp>
#include <mongocxx/exception/query_exception.hpp>

#include <bson.h>

#include <eosio/chain/abi_serializer.hpp>

#include <eosio/chain/chaindb.h>

#include <eosio/chain/multi_index_includes.hpp>

#define _chaindb_internal_assert(_EXPR, ...) EOS_ASSERT(_EXPR, fc::assert_exception, __VA_ARGS__)
#define scope_kvp(__SCOPE) _detail::kvp(_detail::get_scope_key(), _detail::name(__SCOPE).to_string())
#define payer_kvp(__PAYER) _detail::kvp(_detail::get_payer_key(), _detail::name(__PAYER).to_string())
#define id_kvp(__PK) _detail::kvp(_detail::get_id_key(), _detail::b_int64{static_cast<int64_t>(__PK)})

namespace _detail {
    using eosio::chain::abi_def;
    using eosio::chain::table_def;
    using eosio::chain::index_def;
    using eosio::chain::struct_def;
    using eosio::chain::field_def;
    using eosio::chain::abi_serializer;
    using eosio::chain::name;
    using eosio::chain::string_to_name;
    using eosio::chain::bytes;

    using fc::optional;
    using fc::blob;
    using fc::variant;
    using fc::variants;
    using fc::variant_object;
    using fc::mutable_variant_object;
    using fc::microseconds;

    using bsoncxx::builder::basic::make_document;
    using bsoncxx::builder::basic::document;
    using bsoncxx::builder::basic::sub_document;
    using bsoncxx::builder::basic::sub_array;
    using bsoncxx::builder::basic::kvp;

    using bsoncxx::document::element;
    using document_view = bsoncxx::document::view;
    using array_view = bsoncxx::array::view;

    using bsoncxx::types::b_null;
    using bsoncxx::types::b_oid;
    using bsoncxx::types::b_bool;
    using bsoncxx::types::b_double;
    using bsoncxx::types::b_int64;
    using bsoncxx::types::b_binary;
    using bsoncxx::types::b_array;
    using bsoncxx::types::b_document;

    using bsoncxx::type;
    using bsoncxx::binary_sub_type;
    using bsoncxx::oid;

    using mongocxx::model::insert_one;
    using mongocxx::model::update_one;
    using mongocxx::collection;
    using mongocxx::query_exception;
    namespace options = mongocxx::options;

    using std::string;
    using std::tuple;

    variant_object build_variant(const document_view&);

    blob build_blob(const b_binary& src) {
        blob dst;
        if (src.sub_type != binary_sub_type::k_binary) return dst;
        dst.data.assign(src.bytes, src.bytes + src.size);
        return dst;
    }

    variants build_variant(const array_view& src) {
        variants dst;
        for (auto& item: src) {
            switch (item.type()) {
                case type::k_null:
                    dst.emplace_back(variant());
                    break;
                case type::k_int32:
                    dst.emplace_back(item.get_int32().value);
                    break;
                case type::k_int64:
                    dst.emplace_back(item.get_int64().value);
                    break;
                case type::k_decimal128:
                    // TODO
                    break;
                case type::k_double:
                    dst.emplace_back(item.get_double().value);
                    break;
                case type::k_utf8:
                    dst.emplace_back(item.get_utf8().value.to_string());
                    break;
                case type::k_date:
                    // TODO
                    break;
                case type::k_timestamp:
                    // TODO
                    break;
                case type::k_document:
                    dst.emplace_back(build_variant(item.get_document().value));
                    break;
                case type::k_array:
                    dst.emplace_back(build_variant(item.get_array().value));
                    break;
                case type::k_binary:
                    dst.emplace_back(build_blob(item.get_binary()));
                    break;
                case type::k_bool:
                    dst.emplace_back(item.get_bool().value);
                    break;

                    // SKIP
                case type::k_code:
                case type::k_codewscope:
                case type::k_symbol:
                case type::k_dbpointer:
                case type::k_regex:
                case type::k_oid:
                case type::k_maxkey:
                case type::k_minkey:
                case type::k_undefined:
                    break;
            }
        }
        return dst;
    }

    void build_variant(mutable_variant_object& dst, string key, const element& src) {
        switch (src.type()) {
            case type::k_null:
                dst.set(std::move(key), variant());
                break;
            case type::k_int32:
                dst.set(std::move(key), src.get_int32().value);
                break;
            case type::k_int64:
                dst.set(std::move(key), src.get_int64().value);
                break;
            case type::k_decimal128:
                // TODO
                break;
            case type::k_double:
                dst.set(std::move(key), src.get_double().value);
                break;
            case type::k_utf8:
                dst.set(std::move(key), src.get_utf8().value.to_string());
                break;
            case type::k_date:
                // TODO
                break;
            case type::k_timestamp:
                // TODO
                break;
            case type::k_document:
                dst.set(std::move(key), build_variant(src.get_document().value));
                break;
            case type::k_array:
                dst.set(std::move(key), build_variant(src.get_array().value));
                break;
            case type::k_binary:
                dst.set(std::move(key), build_blob(src.get_binary()));
                break;
            case type::k_bool:
                dst.set(std::move(key), src.get_bool().value);
                break;

            // SKIP
            case type::k_code:
            case type::k_codewscope:
            case type::k_symbol:
            case type::k_dbpointer:
            case type::k_regex:
            case type::k_oid:
            case type::k_maxkey:
            case type::k_minkey:
            case type::k_undefined:
                break;
        }
    }

    variant_object build_variant(const document_view& src) {
        mutable_variant_object dst;
        for (auto& item: src) {
            build_variant(dst, item.key().to_string(), item);
        }
        return dst;
    }

    sub_document& build_document(sub_document&, const variant_object&);

    b_binary build_binary(const blob& src) {
        auto size = uint32_t(src.data.size());
        auto data = (uint8_t*) src.data.data();
        return b_binary{binary_sub_type::k_binary, size, data};
    }

    sub_array& build_document(sub_array& dst, const variants& src) {
        for (auto& item: src) {
            switch (item.get_type()) {
                case variant::null_type:
                    dst.append(b_null());
                    break;
                case variant::int64_type:
                    dst.append(b_int64{item.as_int64()});
                    break;
                case variant::uint64_type:
                    dst.append(b_int64{int64_t(item.as_uint64())});
                    break;
                // TODO
                // case variant::int128_type:
                //     break;
                // case variant::uint128_type:
                //     break;
                case variant::double_type:
                    dst.append(b_double{item.as_double()});
                    break;
                case variant::bool_type:
                    dst.append(b_bool{item.as_bool()});
                    break;
                case variant::string_type:
                    dst.append(item.as_string());
                    break;
                // TODO
                // case variant::time_point_type:
                //     break;
                // case variant::time_point_sec_type:
                //     break;
                case variant::array_type:
                    dst.append([&](sub_array array){ build_document(array, item.get_array()); });
                    break;
                case variant::object_type:
                    dst.append([&](sub_document sub_doc){ build_document(sub_doc, item.get_object()); });
                    break;
                case variant::blob_type:
                    dst.append(build_binary(item.as_blob()));
                    break;
            }
        }
        return dst;
    }

    sub_document& build_document(sub_document& dst, const string& key, const variant& src) {
        switch (src.get_type()) {
            case variant::null_type:
                dst.append(kvp(key, b_null()));
                break;
            case variant::int64_type:
                dst.append(kvp(key, b_int64{src.as_int64()}));
                break;
            case variant::uint64_type:
                dst.append(kvp(key, b_int64{int64_t(src.as_uint64())}));
                break;
            // TODO
            // case variant::int128_type:
            //     break;
            // case variant::uint128_type:
            //     break;
            case variant::double_type:
                dst.append(kvp(key, b_double{src.as_double()}));
                break;
            case variant::bool_type:
                dst.append(kvp(key, b_bool{src.as_bool()}));
                break;
            case variant::string_type:
                dst.append(kvp(key, src.as_string()));
                break;
            // TODO
            // case variant::time_point_type:
            //     break;
            // case variant::time_point_sec_type:
            //     break;
            case variant::array_type:
                dst.append(kvp(key, [&](sub_array array){ build_document(array, src.get_array()); }));
                break;
            case variant::object_type:
                dst.append(kvp(key, [&](sub_document sub_doc){ build_document(sub_doc, src.get_object()); }));
                break;
            case variant::blob_type:
                dst.append(kvp(key, build_binary(src.as_blob())));
                break;
        }
        return dst;
    }

    sub_document& build_document(sub_document& dst, const variant_object& src) {
        for (auto& item: src) {
            build_document(dst, item.key(), item.value());
        }
        return dst;
    }

    enum class direction: int {
        Forward = 1,
        Backward = -1
    };

    struct cmp_info {
        const direction order;
        const char* from_forward;
        const char* from_backward;
        const char* to_forward;
        const char* to_backward;
    };

    const cmp_info& start_from() {
        static cmp_info value = {
            direction::Forward,
            "$gte", "$lte",
            "$gt",  "$lt" };
        return value;
    }

    const cmp_info& start_after() {
        static cmp_info value = {
            direction::Forward,
            "$gt", "$lt",
            nullptr, nullptr };
        return value;
    }

    const cmp_info& reverse_start_from() {
        static cmp_info value = {
            direction::Backward,
            "$lte", "$gte",
            "$lt",  "$gt" };
        return value;
    }

    bool is_asc_order(const string& name) {
        return name.size() == 3; // asc (vs desc)
    }

    const string& get_scope_key() {
        static string name = "_SCOPE_";
        return name;
    }

    const string& get_payer_key() {
        static string name = "_PAYER_";
        return name;
    }

    const string& get_primary_index_name() {
        //static string name = "primary";
        static string nm = name{chaindb::tag<by_id>::get_name()}.to_string();
        return nm;
    }

    const string& get_id_key() {
        static string name = "id";
        return name;
    }

    primary_key_t get_id_value(const document_view& view) {
        return static_cast<primary_key_t>(view[get_id_key()].get_int64().value);
    }

    string get_index_name(const table_name_t table, const string& index) {
        // types can't use '.', so it's valid custom type
        return name(table).to_string().append(".").append(index);
    }

    string get_index_name(const table_name_t table, const index_name_t index) {
        return get_index_name(table, name(index).to_string());
    }

    struct abi_table_request {
        const account_name_t code;
        const account_name_t scope;
        const table_name_t table;

        string code_name() const {
            return code?name(code).to_string():"_system";
        }

        string scope_name() const {
            return name(scope).to_string();
        }

        string table_name() const {
            return name(table).to_string();
        }
    }; // struct abi_table_request

    struct abi_info {
        std::map<table_name_t, bool> verified_map;
        const account_name_t code = 0;
        abi_def abi;
        abi_serializer serializer;

        abi_info() = default;
        abi_info(const account_name_t code, abi_def def, const fc::microseconds& max_time)
        : code(code),
          abi(std::move(def)) {

            serializer.set_abi(abi, max_time);

            for (auto& table: abi.tables) {
                auto& src = serializer.get_struct(serializer.resolve_type(table.type));
                for (auto& index: table.indexes) {
                    abi.structs.emplace_back();
                    auto& dst = abi.structs.back();
                    dst.name = get_index_name(table.name.value, index.name);
                    for (auto& key: index.key_names) {
                        for (auto& field: src.fields) {
                            if (field.name == key) {
                                dst.fields.push_back(field);
                                break;
                            }
                        }
                    }
                }
            }

            serializer.set_abi(abi, max_time);
        }

        string code_name() const {
            return code?name(code).to_string():"_system";
        }

        variant_object to_object(
            const table_def& table, const void* data, size_t size, const microseconds& max_time
        ) const {
            return to_object_("table", [&]{return table.name.to_string();}, table.type, data, size, max_time);
        }

        variant_object to_object(
            const abi_table_request& request, const index_name_t index,
            const void* data, size_t size, const microseconds& max_time
        ) const {
            auto type = get_index_name(request.table, index);
            return to_object_("index", [&](){return type;}, type, data, size, max_time);
        }

        bytes to_bytes(
            const table_def& table, const variant& value, const microseconds& max_time
        ) const {
            return to_bytes_("table", [&]{return table.name.to_string();}, table.type, value, max_time);
        }

    private:
        template<typename Type>
        variant_object to_object_(
            const char* object_type, Type&& db_type,
            const string& type, const void* data, const size_t size, const microseconds& max_time
        ) const {
            // begin()
            if (nullptr == data && 0 == size) return variant_object();

            fc::datastream<const char*> ds(static_cast<const char*>(data), size);
            auto value = serializer.binary_to_variant(type, ds, max_time);

//            ilog(
//                "${object_type} ${db}.${type}: ${object}",
//                ("object_type", object_type)("db", code_name())("type", db_type())("object", value));

            _chaindb_internal_assert(
                value.is_object(),
                "ABI serializer returns bad type for the ${object_type} ${db}.${type}",
                ("object_type", object_type)("db", code_name())("type", db_type()));

            return value.get_object();
        }

        template<typename Type>
        bytes to_bytes_(
            const char* object_type, Type&& db_type,
            const string& type, const variant& value, const microseconds& max_time
        ) const {
//            ilog(
//                "${object_type} ${db}.${type}: ${object}",
//                ("object_type", object_type)("db", code_name())("type", db_type())("object", value));

            _chaindb_internal_assert(
                value.is_object(),
                "ABI serializer receive wrong type for the ${object_type} ${db}.${type}");
            return serializer.variant_to_binary(type, value, max_time);
        }
    }; // struct abi_info

    struct abi_table_result {
        const account_name_t code = 0;
        const account_name_t scope = 0;
        const table_def* table = nullptr;
        const abi_info* info = nullptr;

        abi_table_result() = default;
        abi_table_result(const abi_table_request src, const table_def* table, const abi_info* info)
        : code(src.code),
          scope(src.scope),
          table(table),
          info(info) { }
    }; // struct abi_table_result

    struct abi_index_result {
        const account_name_t code = 0;
        const account_name_t scope = 0;
        const table_def* table = nullptr;
        const index_def* index = nullptr;
        const abi_info* info = nullptr;

        abi_index_result() = default;
        abi_index_result(const abi_table_result& src, const index_def* idx)
        : code(src.code),
          scope(src.scope),
          table(src.table),
          index(idx),
          info(src.info) { }

    }; // struct abi_index_result

    struct cursor_info {
        const account_name_t code;
        const account_name_t scope;

        cursor_info(const abi_index_result& src, collection db_table)
        : code(src.code),
          scope(src.scope),
          table_(*src.table),
          index_(*src.index),
          info_(*src.info),
          db_table_(std::move(db_table)) { }

        cursor_info(cursor_info&&) = default;

        cursor_info(const cursor_info& src)
        : code(src.code),
          scope(src.scope),
          table_(src.table_),
          index_(src.index_),
          info_(src.info_),
          db_table_(src.db_table_) {
            if (src.find_cmp_->order == direction::Forward) {
                find_cmp_ = &start_from();
            } else {
                find_cmp_ = &reverse_start_from();
            }
            find_key_ = const_cast<cursor_info&>(src).get_key();
            key_ = find_key_;
            find_pk_ = const_cast<cursor_info&>(src).current();
            pk_ = find_pk_;
            object_ = src.object_;
            is_end_ = false;
        }

        void open(const cmp_info& find_cmp, const variant_object& key, const primary_key_t pk) {
            cursor_.reset();
            reset();

            find_cmp_ = &find_cmp;
            find_key_ = key;
            find_pk_ = pk;
            is_end_ = false;
        }

        void open_end() {
            cursor_.reset();
            reset();

            find_cmp_ = &reverse_start_from();
            find_key_ = variant_object();
            find_pk_ = unset_primary_key;
            is_end_ = true;
        }

        primary_key_t next() {
            if (find_cmp_->order == direction::Backward) {
                change_direction(start_from());
            }
            return lazy_next();
        }

        primary_key_t prev() {
            if (find_cmp_->order == direction::Forward) {
                change_direction(reverse_start_from());
            } else if (is_end_) {
                is_end_ = false;
                reset();
                return current();
            }
            return lazy_next();
        }

        primary_key_t current() {
            if (pk_.valid()) return *pk_;
            lazy_open();
            return get_primary_key();
        }

        const bytes& get_object(const microseconds& max_time) {
            lazy_open();
            if (object_.valid()) return *object_;

            auto itr = cursor_->begin();
            if (cursor_->end() != itr) {
                auto& view = *itr;
                auto value = build_variant(view);
                pk_.emplace(get_id_value(view));
                object_.emplace(info_.to_bytes(table_, value, max_time));
            } else {
                pk_.emplace(unset_primary_key);
                object_.emplace(bytes());
            }
            return *object_;
        }

        const variant_object& get_key() {
            lazy_open();
            if (key_.valid()) return *key_;

            auto itr = cursor_->begin();
            if (cursor_->end() != itr) {
                auto& view = *itr;
                mutable_variant_object object;
                for (auto& name: index_.key_names) {
                    build_variant(object, name, view[name]);
                }
                key_.emplace(object);
            } else {
                key_.emplace(variant_object());
            }
            return *key_;
        }

    private:
        const table_def& table_;
        const index_def& index_;
        const abi_info& info_;

        collection db_table_;

        const cmp_info* find_cmp_ = &start_from();
        variant_object find_key_;
        primary_key_t find_pk_ = unset_primary_key;

        bool is_end_ = false;
        optional<mongocxx::cursor> cursor_;

        optional<variant_object> key_;
        optional<primary_key_t> pk_;
        optional<bytes> object_;

        void change_direction(const cmp_info& find_cmp) {
            find_cmp_ = &find_cmp;
            find_key_ = get_key();
            find_pk_ = current();

            cursor_.reset();
            lazy_open();
        }

        void reset() {
            key_.reset();
            pk_.reset();
            object_.reset();
        }

        document create_find_document(const char* forward, const char* backward) const {
            document find;

            find.append(scope_kvp(scope));
            if (!find_key_.size()) return find;

            for (int i = 0; i < index_.key_names.size(); ++i) {
                auto& key = index_.key_names[i];
                auto cmp = is_asc_order(index_.key_orders[i]) ? forward : backward;
                find.append(kvp(key, [&](sub_document doc) { build_document(doc, cmp, find_key_[key]); }));
            }
            return find;
        }

        document create_sort_document() const {
            document sort;
            auto order = static_cast<int>(find_cmp_->order);

            for (int i = 0; i < index_.key_names.size(); ++i) {
                if (is_asc_order(index_.key_orders[i])) {
                    sort.append(kvp(index_.key_names[i], order));
                } else {
                    sort.append(kvp(index_.key_names[i], -order));
                }
            }
            if (!index_.unique) sort.append(kvp(get_id_key(), order));
            return sort;
        }

        void lazy_open() {
            if (cursor_) return;

            reset();
            auto find = create_find_document(find_cmp_->from_forward, find_cmp_->from_backward);
            auto sort = create_sort_document();

            cursor_.emplace(db_table_.find(find.view(), options::find().sort(sort.view())));

            const auto find_pk = find_pk_;
            find_pk_ = unset_primary_key;

            if (is_end_) return;
            if (unset_primary_key == find_pk || get_primary_key() == find_pk) return;
            if (index_.unique || !find_cmp_->to_forward) return;

            // locate cursor to primary key

            auto to_find = create_find_document(find_cmp_->to_forward, find_cmp_->to_backward);
            auto to_cursor = db_table_.find(to_find.view(), options::find().sort(sort.view()).limit(1));

            auto to_pk = unset_primary_key;
            auto to_itr = to_cursor.begin();
            if (to_itr != to_cursor.end()) to_pk = get_id_value(*to_itr);

            static constexpr int max_iters = 10000;
            auto itr = cursor_->begin();
            auto etr = cursor_->end();
            for (int i = 0; i < max_iters && itr != etr; ++i, ++itr) {
                pk_.emplace(get_id_value(*itr));
                if (to_pk == *pk_) break;
                // ok, key is found
                if (find_pk == *pk_) return;
            }

            open_end();
        }

        primary_key_t lazy_next() {
            reset();
            lazy_open();

            auto itr = cursor_->begin();
            ++itr;
            return get_primary_key();
        }

        primary_key_t get_primary_key() {
            if (is_end_) {
                pk_.emplace(unset_primary_key);
                return *pk_;
            }

            auto itr = cursor_->begin();
            if (cursor_->end() == itr) {
                if (find_cmp_->order == direction::Forward) {
                    open_end();
                }
                pk_.emplace(unset_primary_key);
            } else {
                pk_.emplace(get_id_value(*itr));
            }
            return *pk_;
        }

    }; // struct cursor_info

    class mongodb_ctx {
    private:
        mongocxx::instance mongo_inst_;
        mongocxx::client mongo_conn_;

        std::map<account_name_t /* code */, abi_info> abi_map_;
        std::map<account_name_t /* code */, std::set<cursor_t>> abi_cursor_map_;

        cursor_t last_cursor_ = 0;
        std::map<cursor_t, cursor_info> cursor_map_;

        void verify_table_structure(const abi_table_request& request, abi_info& info, const table_def& table) {
            if (info.verified_map.count(request.table)) return;
            info.verified_map.emplace(request.table, true);

            auto db_table = get_db_table(request);

            // primary key
            db_table.create_index(
                make_document(
                    kvp(get_scope_key(), 1),
                    kvp(get_id_key(), 1)),
                options::index().name(get_primary_index_name()).unique(true));

            for (auto& index: table.indexes) {
                if (index.name == get_primary_index_name() || index.key_names.empty()) continue;

                bool was_primary = false;
                document doc;
                doc.append(kvp(get_scope_key(), 1));
                for (int i = 0; i < index.key_names.size(); ++i) {
                    if (is_asc_order(index.key_orders[i])) {
                        doc.append(kvp(index.key_names[i], 1));
                    } else {
                        doc.append(kvp(index.key_names[i], -1));
                    }
                    was_primary |= (index.key_names[i] == get_id_key());
                }
                if (!was_primary && !index.unique) doc.append(kvp(get_id_key(), 1));

                try {
                    db_table.create_index(doc.view(), options::index().name(index.name).unique(index.unique));
                } catch (const mongocxx::operation_exception& e) {
                    db_table.indexes().drop_one(index.name);
                    db_table.create_index(doc.view(), options::index().name(index.name).unique(index.unique));
                }
            }
        }

        abi_table_result find_table(const abi_table_request& request) {
            auto itr = abi_map_.find(request.code);
            if (abi_map_.end() == itr) return {};

            for (auto& t: itr->second.abi.tables) {
                if (t.name.value == request.table) {
                    verify_table_structure(request, itr->second, t);
                    return {request, &t, &itr->second};
                }
            }
            return {};
        }

        template<typename Index>
        abi_index_result find_index(const abi_table_result& info, Index&& index) {
            if (info.table == nullptr) return {};

            const auto& index_name = index();
            for (auto& i: info.table->indexes) {
                if (i.name == index_name) return {info, &i};
            }
            return {};
        }

    public:
        const fc::microseconds max_time{15*1000*1000};

        void open_db(mongocxx::uri uri) {
            mongo_conn_ = mongocxx::client{uri};
            abi_map_.clear();
        }

        void set_abi(const account_name_t code, abi_def abi) {
            abi_info info(code, std::move(abi), max_time);
            abi_map_.erase(code);
            abi_map_.emplace(code, std::move(info));
        }

        void close_abi(const account_name_t code) {
            close_code(code);
            abi_map_.erase(code);
        }

        void close_code(const account_name_t code) {
            auto itr = abi_cursor_map_.find(code);
            if (abi_cursor_map_.end() != itr) {
                for (auto id: itr->second) {
                    cursor_map_.erase(id);
                }
                abi_cursor_map_.erase(itr);
            }
        }

        collection get_db_table(const abi_table_request& request) {
            return mongo_conn_[request.code_name()][request.table_name()];
        }

        abi_table_result get_table(const abi_table_request& request) {
            auto result = find_table(request);
            _chaindb_internal_assert(
                result.info,
                "ABI table ${db}.${table} doesn't exists",
                ("db", request.code_name())("table", request.table_name()));
            return result;
        }

        template<typename Index>
        abi_index_result get_index(const abi_table_request& request, Index&& index) {
            auto result = find_index(find_table(request), std::forward<Index>(index));
            _chaindb_internal_assert(
                result.info,
                "ABI index ${db}.${table}.${index} doesn't exists",
                ("db", request.code_name())("table", request.table_name())("index", index()));
            return result;
        }

        cursor_t add_cursor(cursor_info info) {
            do {
                ++last_cursor_;
            } while (cursor_map_.count(last_cursor_));

            abi_cursor_map_[info.code].insert(last_cursor_);
            cursor_map_.emplace(last_cursor_, std::move(info));
            return last_cursor_;
        }

        cursor_t create_cursor(
            const abi_table_request& request, const index_name_t index,
            const cmp_info& cmp, const void* key, const size_t size, const primary_key_t pk = unset_primary_key
        ) {
            auto result = get_index(request, [&](){ return name(index).to_string(); });
            auto object = result.info->to_object(request, index, key, size, max_time);
            cursor_info info(result, get_db_table(request));
            info.open(cmp, object, pk);
            return add_cursor(std::move(info));
        }

        cursor_t create_end_cursor(const abi_table_request& request, const index_name_t index) {
            auto result = get_index(request, [&](){ return name(index).to_string(); });
            cursor_info info(result, get_db_table(request));
            info.open_end();
            return add_cursor(std::move(info));
        }

        cursor_t create_primary_cursor(const abi_table_request& request, const primary_key_t pk) {
            auto result = get_index(request, get_primary_index_name);
            cursor_info info(result, get_db_table(request));
            info.open(start_from(), variant_object(get_id_key(), pk), unset_primary_key);
            return add_cursor(std::move(info));
        }

        cursor_info& get_cursor(const cursor_t id) {
            auto itr = cursor_map_.find(id);
            _chaindb_internal_assert(cursor_map_.end() != itr, "Cursor ${id} doesn't exist", ("id", id));
            return itr->second;
        }

        void close_cursor(const cursor_t id) {
            auto& info = get_cursor(id);
            abi_cursor_map_[info.code].erase(id);
            cursor_map_.erase(id);
        }

        static mongodb_ctx& get_impl() {
            static mongodb_ctx impl;
            return impl;
        }

        bool validate_primary_key(const variant_object& object, const primary_key_t pk) const {
            _chaindb_internal_assert(
                object.end() == object.find(get_scope_key()),
                "Object can't have a field with name ${name}",
                ("name", get_scope_key()));

            _chaindb_internal_assert(
                object.end() == object.find(get_payer_key()),
                "Object can't have a field with name ${name}",
                ("name", get_payer_key()));

            auto itr = object.find(get_id_key());
            if (object.end() != itr) {
                _chaindb_internal_assert(variant::uint64_type == itr->value().get_type(), "Wrong type for primary key");
                _chaindb_internal_assert(pk == itr->value().as_uint64(), "Wrong value for primary key");
                return true;
            }
            return false;
        }


    }; // struct mongodb_ctx
} // namespace _detail

int32_t chaindb_init(const char* uri_str) {
    using namespace _detail;
    try {
        _detail::mongodb_ctx::get_impl().open_db(mongocxx::uri{uri_str});
    } catch (mongocxx::exception & ex) {
        return 0;
    }
    return 1;
}

bool chaindb_set_abi(account_name_t code, eosio::chain::abi_def abi) {
    _detail::mongodb_ctx::get_impl().set_abi(code, std::move(abi));
    return true;
}

bool chaindb_close_abi(account_name_t code) {
    _detail::mongodb_ctx::get_impl().close_abi(code);
    return true;
}

bool chaindb_close_code(account_name_t code) {
    _detail::mongodb_ctx::get_impl().close_code(code);
    return true;
}

cursor_t chaindb_lower_bound(
    account_name_t code, account_name_t scope, table_name_t table, index_name_t index, void* key, size_t size
) {
    using namespace _detail;
    return mongodb_ctx::get_impl().create_cursor({code, scope, table}, index, start_from(), key, size);
}

cursor_t chaindb_upper_bound(
    account_name_t code, account_name_t scope, table_name_t table, index_name_t index, void* key, size_t size
) {
    using namespace _detail;
    return mongodb_ctx::get_impl().create_cursor({code, scope, table}, index, start_after(), key, size);
}

cursor_t chaindb_find(
    account_name_t code, account_name_t scope, table_name_t table, index_name_t index,
    primary_key_t pk, void* key, size_t size
) {
    using namespace _detail;
    return mongodb_ctx::get_impl().create_cursor({code, scope, table}, index, start_from(), key, size, pk);
}

cursor_t chaindb_end(account_name_t code, account_name_t scope, table_name_t table, index_name_t index) {
    using namespace _detail;
    return mongodb_ctx::get_impl().create_end_cursor({code, scope, table}, index);
}

cursor_t chaindb_clone(cursor_t id) {
    using namespace _detail;
    if (invalid_cursor == id) return invalid_cursor;

    auto& ctx = mongodb_ctx::get_impl();
    return ctx.add_cursor(cursor_info(ctx.get_cursor(id)));
}

void chaindb_close(cursor_t id) {
    _detail::mongodb_ctx::get_impl().close_cursor(id);
}

primary_key_t chaindb_current(cursor_t id) {
    return _detail::mongodb_ctx::get_impl().get_cursor(id).current();
}

primary_key_t chaindb_next(cursor_t id) {
    return _detail::mongodb_ctx::get_impl().get_cursor(id).next();
}

primary_key_t chaindb_prev(cursor_t id) {
    return _detail::mongodb_ctx::get_impl().get_cursor(id).prev();
}

int32_t chaindb_datasize(cursor_t id) {
    auto& ctx = _detail::mongodb_ctx::get_impl();
    return int32_t(ctx.get_cursor(id).get_object(ctx.max_time).size());
}

primary_key_t chaindb_data(cursor_t id, void* data, const size_t size) {
    auto& ctx = _detail::mongodb_ctx::get_impl();
    auto& cursor = ctx.get_cursor(id);

    auto& object = cursor.get_object(ctx.max_time);
    _chaindb_internal_assert(
        object.size() == size,
        "Wrong data size (${data_size} != ${object_size}) for the cursor ${id}",
        ("id", id)("data_size", size)("object_size", object.size()));

    ::memcpy(data, object.data(), object.size());
    return cursor.current();
}

primary_key_t chaindb_available_primary_key(account_name_t code, account_name_t scope, table_name_t table) {
    using namespace _detail;

    abi_table_request request{code, scope, table};
    auto cursor = mongodb_ctx::get_impl().get_db_table(request).find(
        make_document(scope_kvp(scope)),
        options::find()
            .sort(make_document(kvp(get_id_key(), -1)))
            .limit(1));

    auto itr = cursor.begin();
    if (cursor.end() != itr) return get_id_value(*itr) + 1;
    return 1;
}

cursor_t chaindb_insert(
    account_name_t code, account_name_t scope, account_name_t payer, table_name_t table,
    primary_key_t pk, void* data, size_t size
) {
    using namespace _detail;

    abi_table_request request{code, scope, table};
    auto& ctx = mongodb_ctx::get_impl();
    auto result = ctx.get_table(request);

    auto object = result.info->to_object(*result.table, data, size, ctx.max_time);
    const bool has_pk = ctx.validate_primary_key(object, pk);

    document insert;
    insert.append(scope_kvp(scope), payer_kvp(payer));
    if (!has_pk) insert.append(id_kvp(pk));
    build_document(insert, object);

    ctx.get_db_table(request).insert_one(insert.view());

    return ctx.create_primary_cursor(request, pk);
}

primary_key_t chaindb_update(
    account_name_t code, account_name_t scope, account_name_t payer, table_name_t table,
    primary_key_t pk, void* data, size_t size
) {
    using namespace _detail;

    abi_table_request request{code, scope, table};
    auto& ctx = mongodb_ctx::get_impl();
    auto result = ctx.get_table(request);

    auto object = result.info->to_object(*result.table, data, size, ctx.max_time);
    ctx.validate_primary_key(object, pk);

    document update;
    if (payer != 0) update.append(payer_kvp(payer));
    build_document(update, object);

    auto matched = ctx.get_db_table(request).update_one(
        make_document(scope_kvp(scope), id_kvp(pk)),
        make_document(kvp("$set", update)));

    _chaindb_internal_assert(
        matched && matched->matched_count() == 1,
        "Object ${object} wasn't found", ("obj", object));

    return pk;
}

primary_key_t chaindb_delete(account_name_t code, account_name_t scope, table_name_t table, primary_key_t pk) {
    using namespace _detail;

    abi_table_request request{code, scope, table};
    auto& ctx = mongodb_ctx::get_impl();

    auto matched = ctx.get_db_table(request).delete_one(
        make_document(scope_kvp(scope), id_kvp(pk)));

    _chaindb_internal_assert(
        matched && matched->deleted_count() == 1,
        "Object ${pk} wasn't found", ("obj", pk));

    return pk;
}
