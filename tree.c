#include "pes.h"
#include "tree.h"
#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmp_entries(const void *a, const void *b) {
    return strcmp(((const TreeEntry *)a)->name, ((const TreeEntry *)b)->name);
}

int tree_serialize(const Tree *tree, void **data_out, size_t *len_out) {
    /* make a sorted copy */
    Tree sorted = *tree;
    qsort(sorted.entries, sorted.count, sizeof(TreeEntry), cmp_entries);

    size_t total = 0;
    for (int i = 0; i < sorted.count; i++)
        total += 20 + strlen(sorted.entries[i].name) + 1 + HASH_SIZE;

    uint8_t *buf = malloc(total + 1);
    if (!buf) return -1;

    size_t off = 0;
    for (int i = 0; i < sorted.count; i++) {
        const TreeEntry *e = &sorted.entries[i];
        int n = snprintf((char *)buf + off, total - off + 1, "%o %s", e->mode, e->name);
        off += n;
        buf[off++] = '\0';
        memcpy(buf + off, e->hash.hash, HASH_SIZE);
        off += HASH_SIZE;
    }
    *data_out = buf;
    *len_out = off;
    return 0;
}

int tree_parse(const void *data, size_t len, Tree *tree_out) {
    const uint8_t *p = (const uint8_t *)data;
    const uint8_t *end = p + len;
    tree_out->count = 0;

    while (p < end && tree_out->count < MAX_TREE_ENTRIES) {
        /* find the space between mode and name */
        const uint8_t *space = memchr(p, ' ', end - p);
        if (!space) break;

        /* find the null terminator after the name */
        const uint8_t *null_pos = memchr(space + 1, '\0', end - (space + 1));
        if (!null_pos) break;

        TreeEntry *e = &tree_out->entries[tree_out->count];

        /* parse mode (octal) */
        char mode_buf[16] = {0};
        size_t mode_len = space - p;
        if (mode_len >= sizeof(mode_buf)) break;
        memcpy(mode_buf, p, mode_len);
        e->mode = (uint32_t)strtol(mode_buf, NULL, 8);

        /* copy name */
        size_t name_len = null_pos - (space + 1);
        if (name_len >= sizeof(e->name)) break;
        memcpy(e->name, space + 1, name_len);
        e->name[name_len] = '\0';

        p = null_pos + 1;
        if (p + HASH_SIZE > end) break;
        memcpy(e->hash.hash, p, HASH_SIZE);
        p += HASH_SIZE;

        tree_out->count++;
    }
    return 0;
}

int tree_from_index(ObjectID *id_out) {
    Index idx;
    memset(&idx, 0, sizeof(idx));
    index_load(&idx);

    Tree tree;
    memset(&tree, 0, sizeof(tree));
    for (int i = 0; i < idx.count && i < MAX_TREE_ENTRIES; i++) {
        TreeEntry *e = &tree.entries[tree.count];
        e->mode = idx.entries[i].mode;
        strncpy(e->name, idx.entries[i].path, sizeof(e->name) - 1);
        e->name[sizeof(e->name) - 1] = '\0';
        e->hash = idx.entries[i].hash;
        tree.count++;
    }

    void *data;
    size_t len;
    if (tree_serialize(&tree, &data, &len) != 0) return -1;
    int r = object_write(OBJ_TREE, data, len, id_out);
    free(data);
    return r;
}
