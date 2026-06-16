/**
 * @file builtins.h
 * @brief Built-in registrations.
 * 
 * Declares the initialization functions for JS globals and standard library.
 */
#ifndef BUILTINS_H
#define BUILTINS_H

#include "vm.h"

Value js_array_at(VM* vm, Value this_val, int arg_count, Value* args);
Value js_array_concat_method(VM* vm, Value this_val, int arg_count, Value* args);
Value js_array_constructor(VM* vm, Value this_val, int arg_count, Value* args);
Value js_array_every(VM* vm, Value this_val, int arg_count, Value* args);
Value js_array_fill(VM* vm, Value this_val, int arg_count, Value* args);
Value js_array_find(VM* vm, Value this_val, int arg_count, Value* args);
Value js_array_find_index(VM* vm, Value this_val, int arg_count, Value* args);
Value js_array_flat(VM* vm, Value this_val, int arg_count, Value* args);
Value js_array_flat_map(VM* vm, Value this_val, int arg_count, Value* args);
Value js_array_for_each(VM* vm, Value this_val, int arg_count, Value* args);
Value js_array_from(VM* vm, Value this_val, int arg_count, Value* args);
Value js_array_includes(VM* vm, Value this_val, int arg_count, Value* args);
Value js_array_index_of(VM* vm, Value this_val, int arg_count, Value* args);
Value js_array_is_array(VM* vm, Value this_val, int arg_count, Value* args);
Value js_array_join(VM* vm, Value this_val, int arg_count, Value* args);
Value js_array_of(VM* vm, Value this_val, int arg_count, Value* args);
Value js_array_pop(VM* vm, Value this_val, int arg_count, Value* args);
Value js_array_push_method(VM* vm, Value this_val, int arg_count, Value* args);
Value js_array_reduce(VM* vm, Value this_val, int arg_count, Value* args);
Value js_array_reverse(VM* vm, Value this_val, int arg_count, Value* args);
Value js_array_slice(VM* vm, Value this_val, int arg_count, Value* args);
Value js_array_some(VM* vm, Value this_val, int arg_count, Value* args);
Value js_array_sort(VM* vm, Value this_val, int arg_count, Value* args);
Value js_array_splice(VM* vm, Value this_val, int arg_count, Value* args);
Value js_array_to_string(VM* vm, Value this_val, int arg_count, Value* args);
Value js_error_constructor(VM* vm, Value this_val, int arg_count, Value* args);
Value js_global_is_finite(VM* vm, Value this_val, int arg_count, Value* args);
Value js_global_is_nan(VM* vm, Value this_val, int arg_count, Value* args);
Value js_json_parse(VM* vm, Value this_val, int arg_count, Value* args);
Value js_json_stringify(VM* vm, Value this_val, int arg_count, Value* args);
Value js_math_abs(VM* vm, Value this_val, int arg_count, Value* args);
Value js_math_acos(VM* vm, Value this_val, int arg_count, Value* args);
Value js_math_asin(VM* vm, Value this_val, int arg_count, Value* args);
Value js_math_atan(VM* vm, Value this_val, int arg_count, Value* args);
Value js_math_atan2(VM* vm, Value this_val, int arg_count, Value* args);
Value js_math_cbrt(VM* vm, Value this_val, int arg_count, Value* args);
Value js_math_ceil(VM* vm, Value this_val, int arg_count, Value* args);
Value js_math_clz32(VM* vm, Value this_val, int arg_count, Value* args);
Value js_math_cos(VM* vm, Value this_val, int arg_count, Value* args);
Value js_math_exp(VM* vm, Value this_val, int arg_count, Value* args);
Value js_math_f16round(VM* vm, Value this_val, int arg_count, Value* args);
Value js_math_floor(VM* vm, Value this_val, int arg_count, Value* args);
Value js_math_hypot(VM* vm, Value this_val, int arg_count, Value* args);
Value js_math_imul(VM* vm, Value this_val, int arg_count, Value* args);
Value js_math_log(VM* vm, Value this_val, int arg_count, Value* args);
Value js_math_log10(VM* vm, Value this_val, int arg_count, Value* args);
Value js_math_log2(VM* vm, Value this_val, int arg_count, Value* args);
Value js_math_max(VM* vm, Value this_val, int arg_count, Value* args);
Value js_math_min(VM* vm, Value this_val, int arg_count, Value* args);
Value js_math_pow(VM* vm, Value this_val, int arg_count, Value* args);
Value js_math_random(VM* vm, Value this_val, int arg_count, Value* args);
Value js_math_round(VM* vm, Value this_val, int arg_count, Value* args);
Value js_math_sign(VM* vm, Value this_val, int arg_count, Value* args);
Value js_math_sin(VM* vm, Value this_val, int arg_count, Value* args);
Value js_math_sqrt(VM* vm, Value this_val, int arg_count, Value* args);
Value js_math_tan(VM* vm, Value this_val, int arg_count, Value* args);
Value js_math_trunc(VM* vm, Value this_val, int arg_count, Value* args);
Value js_number_constructor(VM* vm, Value this_val, int arg_count, Value* args);
Value js_number_is_finite(VM* vm, Value this_val, int arg_count, Value* args);
Value js_number_is_integer(VM* vm, Value this_val, int arg_count, Value* args);
Value js_number_is_nan(VM* vm, Value this_val, int arg_count, Value* args);
Value js_number_is_safe_integer(VM* vm, Value this_val, int arg_count, Value* args);
Value js_object_assign(VM* vm, Value this_val, int arg_count, Value* args);
Value js_object_constructor(VM* vm, Value this_val, int arg_count, Value* args);
Value js_object_create(VM* vm, Value this_val, int arg_count, Value* args);
Value js_object_entries(VM* vm, Value this_val, int arg_count, Value* args);
Value js_object_freeze(VM* vm, Value this_val, int arg_count, Value* args);
Value js_object_from_entries(VM* vm, Value this_val, int arg_count, Value* args);
Value js_object_keys(VM* vm, Value this_val, int arg_count, Value* args);
Value js_object_values(VM* vm, Value this_val, int arg_count, Value* args);
Value js_parse_float(VM* vm, Value this_val, int arg_count, Value* args);
Value js_parse_int(VM* vm, Value this_val, int arg_count, Value* args);
Value js_promise_with_resolvers(VM* vm, Value this_val, int arg_count, Value* args);
Value js_range_error_constructor(VM* vm, Value this_val, int arg_count, Value* args);
Value js_reference_error_constructor(VM* vm, Value this_val, int arg_count, Value* args);
Value js_regexp_constructor(VM* vm, Value this_val, int arg_count, Value* args);
Value js_regexp_exec(VM* vm, Value this_val, int arg_count, Value* args);
Value js_regexp_test(VM* vm, Value this_val, int arg_count, Value* args);
Value js_s(VM* vm, Value this_val, int arg_count, Value* args);
Value js_set_for_each(VM* vm, Value this_val, int arg_count, Value* args);
Value js_set_is_disjoint_from(VM* vm, Value this_val, int arg_count, Value* args);
Value js_set_is_subset_of(VM* vm, Value this_val, int arg_count, Value* args);
Value js_set_is_superset_of(VM* vm, Value this_val, int arg_count, Value* args);
Value js_set_symmetric_difference(VM* vm, Value this_val, int arg_count, Value* args);
Value js_set_values(VM* vm, Value this_val, int arg_count, Value* args);
Value js_string_at(VM* vm, Value this_val, int arg_count, Value* args);
Value js_string_char_at(VM* vm, Value this_val, int arg_count, Value* args);
Value js_string_char_code_at(VM* vm, Value this_val, int arg_count, Value* args);
Value js_string_concat_method(VM* vm, Value this_val, int arg_count, Value* args);
Value js_string_constructor(VM* vm, Value this_val, int arg_count, Value* args);
Value js_string_ends_with(VM* vm, Value this_val, int arg_count, Value* args);
Value js_string_from_char_code(VM* vm, Value this_val, int arg_count, Value* args);
Value js_string_includes(VM* vm, Value this_val, int arg_count, Value* args);
Value js_string_index_of(VM* vm, Value this_val, int arg_count, Value* args);
Value js_string_last_index_of(VM* vm, Value this_val, int arg_count, Value* args);
Value js_string_pad_end(VM* vm, Value this_val, int arg_count, Value* args);
Value js_string_pad_start(VM* vm, Value this_val, int arg_count, Value* args);
Value js_string_repeat(VM* vm, Value this_val, int arg_count, Value* args);
Value js_string_replace(VM* vm, Value this_val, int arg_count, Value* args);
Value js_string_replace_all(VM* vm, Value this_val, int arg_count, Value* args);
Value js_string_slice(VM* vm, Value this_val, int arg_count, Value* args);
Value js_string_split(VM* vm, Value this_val, int arg_count, Value* args);
Value js_string_starts_with(VM* vm, Value this_val, int arg_count, Value* args);
Value js_string_substring(VM* vm, Value this_val, int arg_count, Value* args);
Value js_string_to_lower(VM* vm, Value this_val, int arg_count, Value* args);
Value js_string_to_string(VM* vm, Value this_val, int arg_count, Value* args);
Value js_string_to_upper(VM* vm, Value this_val, int arg_count, Value* args);
Value js_string_trim(VM* vm, Value this_val, int arg_count, Value* args);
Value js_string_trim_end(VM* vm, Value this_val, int arg_count, Value* args);
Value js_string_trim_start(VM* vm, Value this_val, int arg_count, Value* args);
Value js_suppressed_error_constructor(VM* vm, Value this_val, int arg_count, Value* args);
Value js_symbol_constructor(VM* vm, Value this_val, int arg_count, Value* args);
Value js_syntax_error_constructor(VM* vm, Value this_val, int arg_count, Value* args);
Value js_type_error_constructor(VM* vm, Value this_val, int arg_count, Value* args);
Value js_process_dlopen(VM* vm, Value this_val, int arg_count, Value* args);
Value js_process_require(VM* vm, Value this_val, int arg_count, Value* args);

#endif // BUILTINS_H
