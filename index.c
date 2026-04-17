// index.c — Staging area implementation

#include "index.h"
#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

// Forward declaration (since pes.h cannot be modified)
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

// ─── PROVIDED ────────────────────────────────────────────────────────────────

// Find an index entry by path (linear scan).
IndexEntry* index_find(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0)
            return &index->entries[i];
    }
    return NULL;
}

// Remove a file from the index.
int index_remove(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {
            int remaining = index->count - i - 1;
            if (remaining > 0)
                memmove(&index->entries[i], &index->entries[i + 1],
                        remaining * sizeof(IndexEntry));
            index->count--;
            return index_save(index);
        }
    }
    fprintf(stderr, "error: '%s' is not in the index\n", path);
    return -1;
}

// Print status
int index_status(const Index *index) {
    printf("Staged changes:\n");
    int staged = 0;

    for (int i = 0; i < index->count; i++) {
        printf("  staged:     %s\n", index->entries[i].path);
        staged++;
    }

    if (staged == 0) printf("  (nothing to show)\n");
    printf("\n");

    return 0;
}

// ─── TODO IMPLEMENTED ───────────────────────────────────────────────────────

// Load index safely
int index_load(Index *index) {
    if (!index) return -1;

    memset(index, 0, sizeof(Index));

    FILE *fp = fopen(".pes/index", "r");
    if (!fp) return 0;

    while (index->count < MAX_INDEX_ENTRIES) {
        IndexEntry *e = &index->entries[index->count];
        char hash_hex[HASH_HEX_SIZE + 1];

        int rc = fscanf(fp, "%o %64s %lu %u %511s\n",
                        &e->mode,
                        hash_hex,
                        &e->mtime_sec,
                        &e->size,
                        e->path);

        if (rc != 5) break;

        if (hex_to_hash(hash_hex, &e->hash) != 0) {
            fclose(fp);
            return -1;
        }

        index->count++;
    }

    fclose(fp);
    return 0;
}

// Sorting helper
static int compare_index_entries(const void *a, const void *b) {
    return strcmp(((const IndexEntry *)a)->path,
                  ((const IndexEntry *)b)->path);
}

// Save index safely (FIXED: no stack overflow)
int index_save(const Index *index) {
    if (!index) return -1;

    Index *sorted = malloc(sizeof(Index));
    if (!sorted) return -1;

    memcpy(sorted, index, sizeof(Index));

    qsort(sorted->entries, sorted->count,
          sizeof(IndexEntry), compare_index_entries);

    FILE *fp = fopen(".pes/index.tmp", "w");
    if (!fp) {
        free(sorted);
        return -1;
    }

    for (int i = 0; i < sorted->count; i++) {
        char hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&sorted->entries[i].hash, hex);

        fprintf(fp, "%o %s %lu %u %s\n",
                sorted->entries[i].mode,
                hex,
                sorted->entries[i].mtime_sec,
                sorted->entries[i].size,
                sorted->entries[i].path);
    }

    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);

    rename(".pes/index.tmp", ".pes/index");

    free(sorted);
    return 0;
}

// Add file safely
int index_add(Index *index, const char *path) {
    if (!index || !path) return -1;

    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }

    long fsize = ftell(fp);
    if (fsize < 0) {
        fclose(fp);
        return -1;
    }

    rewind(fp);

    size_t size = (size_t)fsize;
    void *buffer = NULL;

    if (size > 0) {
        buffer = malloc(size);
        if (!buffer) {
            fclose(fp);
            return -1;
        }

        if (fread(buffer, 1, size, fp) != size) {
            fclose(fp);
            free(buffer);
            return -1;
        }
    }

    fclose(fp);

    ObjectID id;
    if (object_write(OBJ_BLOB, buffer, size, &id) != 0) {
        free(buffer);
        return -1;
    }

    free(buffer);

    struct stat st;
    if (stat(path, &st) != 0) return -1;

    IndexEntry *e = index_find(index, path);

    if (!e) {
        if (index->count >= MAX_INDEX_ENTRIES) return -1;
        e = &index->entries[index->count++];
    }

    e->mode = st.st_mode;
    e->mtime_sec = st.st_mtime;
    e->size = st.st_size;

    strncpy(e->path, path, sizeof(e->path));
    e->path[sizeof(e->path) - 1] = '\0';

    e->hash = id;

    return index_save(index);
}
