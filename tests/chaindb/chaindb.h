#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t table_name_t;
typedef uint64_t index_name_t;
typedef uint64_t account_name_t;

typedef int32_t cursor_t;
static constexpr cursor_t invalid_cursor = (-1);

typedef uint64_t primary_key_t;
static constexpr primary_key_t end_primary_key = (-1);
static constexpr primary_key_t unset_primary_key = (-2);


int32_t chaindb_init(const char*);

cursor_t chaindb_lower_bound(account_name_t code, account_name_t scope, table_name_t, index_name_t, void* key, size_t);
cursor_t chaindb_upper_bound(account_name_t code, account_name_t scope, table_name_t, index_name_t, void* key, size_t);
cursor_t chaindb_find(account_name_t code, account_name_t scope, table_name_t, index_name_t, primary_key_t, void* key, size_t);
cursor_t chaindb_end(account_name_t code, account_name_t scope, table_name_t, index_name_t);
cursor_t chaindb_clone(cursor_t);

void chaindb_close(cursor_t);

primary_key_t chaindb_current(cursor_t);
primary_key_t chaindb_next(cursor_t);
primary_key_t chaindb_prev(cursor_t);

int32_t chaindb_datasize(cursor_t);
primary_key_t chaindb_data(cursor_t, void* data, const size_t size);

primary_key_t chaindb_available_primary_key(account_name_t code, account_name_t scope, table_name_t table);

cursor_t chaindb_insert(account_name_t code, account_name_t scope, account_name_t payer, table_name_t, primary_key_t, void* data, size_t);
primary_key_t chaindb_update(account_name_t code, account_name_t scope, account_name_t payer, table_name_t, primary_key_t, void* data, size_t);
primary_key_t chaindb_delete(account_name_t code, account_name_t scope, table_name_t, primary_key_t);

#ifdef __cplusplus
}
#endif