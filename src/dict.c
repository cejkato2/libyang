/**
 * @file dict.c
 * @author Radek Krejci <rkrejci@cesnet.cz>
 * @brief libyang dictionary for storing strings
 *
 * Copyright (c) 2015 CESNET, z.s.p.o.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of the Company nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 */

#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "common.h"
#include "dict.h"
#include "context.h"

void
lydict_init(struct dict_table *dict)
{
    if (!dict) {
        ly_errno = LY_EINVAL;
        return;
    }

    dict->hash_mask = DICT_SIZE - 1;
}

void
lydict_clean(struct dict_table *dict)
{
    int i;
    struct dict_rec *chain, *rec;

    if (!dict) {
        ly_errno = LY_EINVAL;
        return;
    }

    for (i = 0; i < DICT_SIZE; i++) {
        rec = &dict->recs[i];
        chain = rec->next;

        free(rec->value);
        while (chain) {
            rec = chain;
            chain = rec->next;

            free(rec->value);
            free(rec);
        }
    }
}

/*
 * Bob Jenkin's one-at-a-time hash
 * http://www.burtleburtle.net/bob/hash/doobs.html
 *
 * Spooky hash is faster, but it works only for little endian architectures.
 */
static uint32_t
dict_hash(const char *key, size_t len)
{
    uint32_t hash, i;

    for (hash = i = 0; i < len; ++i) {
        hash += key[i];
        hash += (hash << 10);
        hash ^= (hash >> 6);
    }
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
    return hash;
}

void
lydict_remove(struct ly_ctx *ctx, const char *value)
{
    size_t len;
    uint32_t index;
    struct dict_rec *record, *prev = NULL;

    if (!ctx || !value) {
        return;
    }

    len = strlen(value);

    index = dict_hash(value, len) & ctx->dict.hash_mask;
    record = &ctx->dict.recs[index];

    while (record && record->value != value) {
        prev = record;
        record = record->next;
    }

    if (!record) {
        /* record not found */
        return;
    }

    record->refcount--;
    if (!record->refcount) {
        free(record->value);
        if (record->next) {
            if (prev) {
                /* change in dynamically allocated chain */
                prev->next = record->next;
                free(record);
            } else {
                /* move dynamically allocated record into the static array */
                prev = record->next;    /* temporary storage */
                memcpy(record, record->next, sizeof *record);
                free(prev);
            }
        } else if (prev) {
            /* removing last record from the dynamically allocated chain */
            prev->next = NULL;
            free(record);
        } else {
            /* clean the static record content */
            memset(record, 0, sizeof *record);
        }
    }
}

static char *
dict_insert(struct ly_ctx *ctx, char *value, size_t len, int zerocopy)
{
    uint32_t index;
    struct dict_rec *record, *new;

    index = dict_hash(value, len) & ctx->dict.hash_mask;
    record = &ctx->dict.recs[index];

    if (!record->value) {
        /* first record with this hash */
        if (zerocopy) {
            record->value = value;
        } else {
            record->value = malloc((len + 1) * sizeof *record->value);
            memcpy(record->value, value, len);
            record->value[len] = '\0';
        }
        record->refcount = 1;
        record->next = NULL;

        LOGDBG("DICT: inserting \"%s\"", record->value);
        return record->value;
    }

    /* collision, search if the value is already in dict */
    while (record) {
        if (!memcmp(value, record->value, len) && record->value[len] == '\0') {
            /* record found */
            record->refcount++;

            if (zerocopy) {
                free(value);
            }

            LOGDBG("DICT: inserting (refcount) \"%s\"", record->value);
            return record->value;
        }

        if (!record->next) {
            /* not present, add as a new record in chain */
            break;
        }

        record = record->next;
    }

    /* create new record and add it behind the last record */
    new = malloc(sizeof *record);
    if (zerocopy) {
        new->value = value;
    } else {
        new->value = malloc((len + 1) * sizeof *record->value);
        memcpy(new->value, value, len);
        new->value[len] = '\0';
    }
    new->refcount = 1;
    new->next = NULL;

    record->next = new;

    LOGDBG("DICT: inserting \"%s\" with collision ", record->value);
    return new->value;
}

const char *
lydict_insert(struct ly_ctx *ctx, const char *value, size_t len)
{
    if (value && !len) {
        len = strlen(value);
    }

    if (!value) {
        return NULL;
    }
    return dict_insert(ctx, (char *)value, len, 0);
}

const char *
lydict_insert_zc(struct ly_ctx *ctx, char *value)
{
    if (!value) {
        return NULL;
    }
    return dict_insert(ctx, value, strlen(value), 1);
}
