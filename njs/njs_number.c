
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_stub.h>
#include <nxt_lvlhsh.h>
#include <nxt_mem_cache_pool.h>
#include <njscript.h>
#include <njs_vm.h>
#include <njs_number.h>
#include <string.h>
#include <stdio.h>


double
njs_value_to_number(njs_value_t *value)
{
    njs_array_t  *array;

    if (nxt_fast_path(njs_is_numeric(value))) {
        return value->data.u.number;
    }

    if (njs_is_string(value)) {
        return njs_string_to_number(value);
    }

    if (njs_is_array(value)) {

        array = value->data.u.array;

        if (nxt_lvlhsh_is_empty(&array->object.hash)) {

            if (array->length == 0) {
                /* An empty array value is zero. */
                return 0.0;
            }

            if (array->length == 1 && njs_is_valid(&array->start[0])) {
                /* A single value array is the zeroth array value. */
                return njs_value_to_number(&array->start[0]);
            }
        }
    }

    return NJS_NAN;
}


double
njs_number_parse(const u_char **start, const u_char *end)
{
    u_char        c;
    double        num, frac, scale;
    const u_char  *p;

    /* TODO: "1e2" */

    p = *start;
    c = *p++;

    /* Values below '0' become >= 208. */
    c = c - '0';

    num = c;

    while (p < end) {
        c = *p;

        /* Values below '0' become >= 208. */
        c = c - '0';

        if (nxt_slow_path(c > 9)) {
            break;
        }

        num = num * 10 + c;
        p++;
    }

    if (*p == '.') {

        frac = 0;
        scale = 1;

        for (p++; p < end; p++) {
            c = *p;

            /* Values below '0' become >= 208. */
            c = c - '0';

            if (nxt_slow_path(c > 9)) {
                break;
            }

            frac = frac * 10 + c;
            scale *= 10;
        }

        num += frac / scale;
    }

    *start = p;

    return num;
}


njs_ret_t
njs_number_to_string(njs_vm_t *vm, njs_value_t *string,
    const njs_value_t *number)
{
    u_char             *p;
    double             num;
    size_t             size;
    const char         *fmt;
    const njs_value_t  *value;
    char               buf[128];

    num = number->data.u.number;

    if (njs_is_nan(num)) {
        value = &njs_string_nan;

    } else if (njs_is_infinity(num)) {

        if (num < 0) {
            value = &njs_string_minus_infinity;

        } else {
            value = &njs_string_plus_infinity;
        }

    } else {
        if (fabs(num) < 1000000) {
            fmt = "%g";

        } else if (fabs(num) < 1e20) {
            fmt = "%1.f";

        } else {
            fmt = "%1.e";
        }

        size = snprintf(buf, sizeof(buf), fmt, num);

        p = njs_string_alloc(vm, string, size, size);

        if (nxt_fast_path(p != NULL)) {
            memcpy(p, buf, size);
            return NXT_OK;
        }

        return NXT_ERROR;
    }

    *string = *value;

    return NXT_OK;
}


njs_ret_t
njs_number_function(njs_vm_t *vm, njs_param_t *param)
{
    njs_object_t       *object;
    const njs_value_t  *value;

    if (param->nargs == 0) {
        value = &njs_value_zero;

    } else {
        /* TODO: to_number. */
        value = &param->args[0];
    }

    if (vm->frame->ctor) {
        /* value->type is the same as prototype offset. */
        object = njs_object_value_alloc(vm, value, value->type);
        if (nxt_slow_path(object == NULL)) {
            return NXT_ERROR;
        }

        vm->retval.data.u.object = object;
        vm->retval.type = NJS_OBJECT_NUMBER;
        vm->retval.data.truth = 1;

    } else {
        vm->retval = *value;
    }

    return NXT_OK;
}


static const njs_object_prop_t  njs_number_function_properties[] =
{
    { njs_string("Number"),
      njs_string("name"),
      NJS_PROPERTY, 0, 0, 0, },

    { njs_value(NJS_NUMBER, 1, 1.0),
      njs_string("length"),
      NJS_PROPERTY, 0, 0, 0, },

    { njs_getter(njs_object_prototype_create_prototype),
      njs_string("prototype"),
      NJS_NATIVE_GETTER, 0, 0, 0, },
};


nxt_int_t
njs_number_function_hash(njs_vm_t *vm, nxt_lvlhsh_t *hash)
{
    return njs_object_hash_create(vm, hash, njs_number_function_properties,
                                  nxt_nitems(njs_number_function_properties));
}


static njs_ret_t
njs_number_prototype_value_of(njs_vm_t *vm, njs_param_t *param)
{
    njs_value_t  *value;

    value = param->object;

    if (value->type != NJS_NUMBER) {

        if (value->type == NJS_OBJECT_NUMBER) {
            value = &value->data.u.object_value->value;

        } else {
            vm->exception = &njs_exception_type_error;
            return NXT_ERROR;
        }
    }

    vm->retval = *value;

    return NXT_OK;
}


static njs_ret_t
njs_number_prototype_to_string(njs_vm_t *vm, njs_param_t *param)
{
    njs_value_t  *value;

    value = param->object;

    if (value->type != NJS_NUMBER) {

        if (value->type == NJS_OBJECT_NUMBER) {
            value = &value->data.u.object_value->value;

        } else {
            vm->exception = &njs_exception_type_error;
            return NXT_ERROR;
        }
    }

    return njs_number_to_string(vm, &vm->retval, value);
}


static const njs_object_prop_t  njs_number_prototype_properties[] =
{
    { njs_native_function(njs_number_prototype_value_of, 0),
      njs_string("valueOf"),
      NJS_METHOD, 0, 0, 0, },

    { njs_native_function(njs_number_prototype_to_string, 0),
      njs_string("toString"),
      NJS_METHOD, 0, 0, 0, },
};


nxt_int_t
njs_number_prototype_hash(njs_vm_t *vm, nxt_lvlhsh_t *hash)
{
    return njs_object_hash_create(vm, hash, njs_number_prototype_properties,
                                  nxt_nitems(njs_number_prototype_properties));
}