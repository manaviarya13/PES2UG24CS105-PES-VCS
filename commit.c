// commit.c — Commit object implementation

#include "commit.h"
#include <unistd.h>
#include "index.h"
#include "tree.h"
#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Forward declarations
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);
int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out);

// ─── Serialize Commit ───────────────────────────────────────────────────────

int commit_serialize(const Commit *commit, void **data_out, size_t *len_out) {
    char *buffer = malloc(8192);
    if (!buffer) return -1;

    char tree_hex[HASH_HEX_SIZE + 1];
    char parent_hex[HASH_HEX_SIZE + 1];

    hash_to_hex(&commit->tree, tree_hex);

    size_t offset = 0;

    offset += sprintf(buffer + offset, "tree %s\n", tree_hex);

    if (commit->has_parent) {
        hash_to_hex(&commit->parent, parent_hex);
        offset += sprintf(buffer + offset, "parent %s\n", parent_hex);
    }

    offset += sprintf(buffer + offset, "author %s\n", commit->author);
    offset += sprintf(buffer + offset, "date %lu\n",
                      (unsigned long)commit->timestamp);

    offset += sprintf(buffer + offset, "\n%s\n", commit->message);

    *data_out = buffer;
    *len_out = offset;
    return 0;
}

// ─── Parse Commit ───────────────────────────────────────────────────────────

int commit_parse(const void *data, size_t len, Commit *commit_out) {
    memset(commit_out, 0, sizeof(Commit));

    char *content = strndup((const char *)data, len);
    if (!content) return -1;

    char tree_hex[HASH_HEX_SIZE + 1];
    char parent_hex[HASH_HEX_SIZE + 1];

    // tree
    sscanf(content, "tree %64s", tree_hex);
    hex_to_hash(tree_hex, &commit_out->tree);

    // parent (optional)
    char *parent_line = strstr(content, "parent ");
    if (parent_line) {
        sscanf(parent_line, "parent %64s", parent_hex);
        hex_to_hash(parent_hex, &commit_out->parent);
        commit_out->has_parent = 1;
    }

    // author
    char *author_line = strstr(content, "author ");
    if (author_line)
        sscanf(author_line, "author %[^\n]", commit_out->author);

    // date
    char *date_line = strstr(content, "date ");
    if (date_line)
        sscanf(date_line, "date %lu", &commit_out->timestamp);

    // message
    char *msg = strstr(content, "\n\n");
    if (msg) {
        msg += 2;
        snprintf(commit_out->message, sizeof(commit_out->message), "%s", msg);
    }

    free(content);
    return 0;
}

// ─── Create Commit ──────────────────────────────────────────────────────────

int commit_create(const char *message, ObjectID *commit_id_out) {
    if (!message || !commit_id_out) return -1;

    Index index;
    if (index_load(&index) != 0) return -1;

    ObjectID tree_id;
    if (tree_from_index(&tree_id) != 0) return -1;

    Commit commit;
    memset(&commit, 0, sizeof(commit));

    commit.tree = tree_id;
    commit.has_parent = 0;

    ObjectID parent_id;
    if (head_read(&parent_id) == 0) {
        commit.parent = parent_id;
        commit.has_parent = 1;
    }

    snprintf(commit.author, sizeof(commit.author), "%s", pes_author());
    commit.timestamp = (uint64_t)time(NULL);
    snprintf(commit.message, sizeof(commit.message), "%s", message);

    void *data;
    size_t len;

    if (commit_serialize(&commit, &data, &len) != 0)
        return -1;

    if (object_write(OBJ_COMMIT, data, len, commit_id_out) != 0) {
        free(data);
        return -1;
    }

    free(data);

    if (head_update(commit_id_out) != 0)
        return -1;

    return 0;
}

// ─── Walk Commit History ────────────────────────────────────────────────────

int commit_walk(commit_walk_fn callback, void *ctx) {
    ObjectID current;

    if (head_read(&current) != 0)
        return -1;

    while (1) {
        ObjectType type;
        void *data;
        size_t len;

        if (object_read(&current, &type, &data, &len) != 0)
            return -1;

        if (type != OBJ_COMMIT) {
            free(data);
            return -1;
        }

        Commit commit;
        if (commit_parse(data, len, &commit) != 0) {
            free(data);
            return -1;
        }

        callback(&current, &commit, ctx);

        free(data);

        if (!commit.has_parent)
            break;

        current = commit.parent;
    }

    return 0;
}

// ─── HEAD helpers ───────────────────────────────────────────────────────────

int head_read(ObjectID *id_out) {
    FILE *fp = fopen(HEAD_FILE, "r");
    if (!fp) return -1;

    char hex[HASH_HEX_SIZE + 1];

    if (!fgets(hex, sizeof(hex), fp)) {
        fclose(fp);
        return -1;
    }

    fclose(fp);

    hex[strcspn(hex, "\n")] = '\0';

    return hex_to_hash(hex, id_out);
}

int head_update(const ObjectID *new_commit) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(new_commit, hex);

    FILE *fp = fopen(HEAD_FILE ".tmp", "w");
    if (!fp) return -1;

    fprintf(fp, "%s\n", hex);

    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);

    rename(HEAD_FILE ".tmp", HEAD_FILE);

    return 0;
}
