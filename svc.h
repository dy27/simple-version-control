#ifndef svc_h
#define svc_h

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>

// The resolution objects stores modifications to be made to files during
// the merging process.
typedef struct resolution {
    // NOTE: DO NOT MODIFY THIS STRUCT
    char *file_name;
    char *resolved_file;
} resolution;

// File objects store the hash and the file path
struct file {
    int hash;
    char *file_name;
};

// A change object stores a pointer to the removed file and the added file.
// If both are non-NULL, then the change is a modification.
// If only the removed file is NULL, the change is an addition.
// If only the added file is NULL, the change is a deletion.
struct change {
    struct file *removed_file; // Index of prev file if rem or mod
    struct file *added_file; // New file if add or mod
};

// The commit object stores the associated information for a single commit.
struct commit {
    char *commit_id;
    char *message;
    size_t parent;
    size_t parent2;
    struct file *files;
    size_t n_files;
    char *branch_name;
};

// Each branch object contains its name and a reference to a commit object.
struct branch {
    char *branch_name;
    size_t ref_commit;
};

// Memory objects represent a memory region allocated by the mmap() function.
// They store a pointer to the allocated memory region and the size of
// that memory region in pages.
struct memory {
    void *ptr;
    size_t n_pages;
};

// The helper object is initialised at the beginning of the program, and holds
// all the information that is passed between functions.
struct helper {
    size_t head;  // Index of the head in the branch array

    struct branch *branches;  // Array of all branches
    size_t n_branches;
    size_t branches_cap;

    struct commit *commits;  // Array of all commits
    size_t n_commits;
    size_t commits_cap;

    struct file *index;  // Array of all tracked files
    size_t index_size;
    size_t index_cap;

    struct memory *mem_list;  // Array of all memory objects
    size_t n_mem;
    size_t offset;  // The offset from the current active memory region
    size_t page_size;

    char *stdout_buffer;  // Pointer to store the location of the manually
                          // allocated buffer for stdout.
};


struct helper *memory_init(void);

void *memory_add(void *helper, size_t n_pages);

void *allocate(void *helper, size_t n);

void *reallocate(void *helper, void *ptr, size_t old_n, size_t n);

void *svc_init(void);

void cleanup(void *helper);

char *str_dup(void *helper, char *string);

struct file *files_dup(void *helper, struct file *files, size_t n_files);

void *array_add(void *helper, void *array, size_t *array_size, size_t *array_cap, void *element, size_t n);

int file_exists(char *file_path);

void file_copy(char *file_path, char *new_file_path);

void update_database(struct file *files, size_t n_files);

void update_working_directory(struct file *files, size_t n_files, int overwrite);

int hash_file(void *helper, char *file_path);

char *svc_commit(void *helper, char *message);

void *get_commit(void *helper, char *commit_id);

char **get_prev_commits(void *helper, void *commit, int *n_prev);

void print_commit(void *helper, char *commit_id);

int svc_branch(void *helper, char *branch_name);

int svc_checkout(void *helper, char *branch_name);

char **list_branches(void *helper, int *n_branches);

int svc_add(void *helper, char *file_name);

int svc_rm(void *helper, char *file_name);

int svc_reset(void *helper, char *commit_id);

char *svc_merge(void *helper, char *branch_name, resolution *resolutions, int n_resolutions);

#endif
