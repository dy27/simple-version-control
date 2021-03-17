#include "svc.h"

#define CAP_INIT 20  // The capacity to initialise arrays at.
#define CAP_GROWTH 2  // The multiplicative factor to expand arrays by.
#define NULL_ID 0xFFFFFFFF  // Represents a NULL value for index references.

/**
* Initialises three pages in virtual memory using mmap() for the helper data
* structure, the stdout buffer and a region to store all dynamic memory
* allocations used by the program. All mmap() calls are private file mappings
* initialised from /dev/zero, a stream of zero values.
*
* @return The Data structure to pass program data between functions.
*/
struct helper *memory_init(void) {
    // Allocate the helper data structure
    int fd = open("/dev/zero", O_RDWR); //
    struct helper *svc = mmap(NULL, sysconf(_SC_PAGE_SIZE),
                              PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    svc->page_size = sysconf(_SC_PAGE_SIZE);

    // Map a page of memory for the stdout buffer and store the pointer
    char *buf = mmap(NULL, svc->page_size,
                     PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    setvbuf(stdout, buf, _IOFBF, svc->page_size);
    svc->stdout_buffer = buf;

    // Initialise mapped memory to store a list of memory objects
    svc->mem_list = (struct memory *)mmap(NULL, svc->page_size,
                    PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    close(fd);
    return svc;
}

/**
* Adds a new memory region to the memory list to use as the new active region,

* @param helper Data structure to pass program data between functions.
* @param n_pages Number of pages of memory to allocate.
* @return A pointer to the allocated memory.
*/
void *memory_add(void *helper, size_t n_pages) {
    struct helper *svc = (struct helper *)helper;
    int fd = open("/dev/zero", O_RDWR);
    void *addr = mmap(NULL, n_pages*svc->page_size,
                     PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    close(fd);
    struct memory new_mem = {addr, n_pages};
    svc->mem_list[svc->n_mem] = new_mem;
    svc->n_mem++;
    svc->offset = 0;
    return addr;
}

/**
* Allocates a specified number of bytes from the memory page for use by the
* program. Memory is allocated sequentially such that each new allocation
* is placed directly after the previous in memory.
*
* @param helper Data structure to pass program data between functions.
* @param n Number of bytes for the allocation.
* @return A pointer to the allocated memory.
*/
void *allocate(void *helper, size_t n) {
    struct helper *svc = (struct helper *)helper;

    // Get the memory object corresponding to the currently used memory region.
    struct memory curr;
    if (svc->n_mem == 0) {
        struct memory temp = {NULL, 0};
        curr = temp;
    } else {
        curr = svc->mem_list[svc->n_mem - 1];
    }

    // Calculate the overflow past the end of the mapped memory region that the
    // allocation will cause. Begin a new memory region if overflow will occur.
    int overflow = (svc->offset + n) - (curr.n_pages * svc->page_size);
    if (overflow >= 0) {
        size_t n_new_pages = (size_t)(overflow / svc->page_size) + 1;
        void *new_addr = memory_add(helper, n_new_pages);
        svc->offset += n;
        return new_addr;
    }

    // Return the address of the memory allocation
    void *addr = curr.ptr + svc->offset;
    svc->offset += n;
    return addr;
}

/**
* Resizes an allocation made by allocate().
*
* @param helper Data structure to pass program data between functions.
* @param old_n Number of bytes to copy from the old allocation to the new.
* @param n Number of bytes for the new allocation.
* @return A pointer to the allocated memory.
*/
void *reallocate(void *helper, void *ptr, size_t old_n, size_t n) {
    void *addr = allocate(helper, n);
    memcpy(addr, ptr, old_n);
    return addr;
}

/**
* Initialises the helper data structure used to pass program data across
* different function calls. Also creates the initial master branch and the
* database directory for storing file versions.
*
* @return A pointer to helper object.
*/
void *svc_init(void) {
    struct helper *svc = memory_init();

    // Create the master branch
    svc->head = NULL_ID;
    svc_branch(svc, "master");
    svc->head = 0; // Set the head to the master branch

    // Create a database directory for file storage with read, write and
    // search permissions.
    mkdir("svc_db", S_IRWXU);
    return (void *)svc;
}

/**
* Unmaps all the virtual memory regions allocated through mmap().
*
* @param helper Data structure to pass program data between functions.
*/
void cleanup(void *helper) {
    struct helper *svc = (struct helper *)helper;

    // Free each of the mapped memory regions
    for (int i=0; i<svc->n_mem; i++) {
        struct memory *m = svc->mem_list + i;
        munmap(m->ptr, m->n_pages * svc->page_size);
    }
    munmap(svc->mem_list, svc->page_size);

    // Flush stdout and free the stdout buffer
    fflush(stdout);
    munmap(svc->stdout_buffer, sysconf(_SC_PAGE_SIZE));

    // Free helper
    munmap(svc, sysconf(_SC_PAGE_SIZE));
}

/**
* Uses allocate() to make a copy of a string.
*
* @param helper Data structure to pass program data between functions.
* @param string The string to be copied.
* @return A pointer to the copied string.
*/
char *str_dup(void *helper, char *string) {
    size_t length = strlen(string) + 1;
    char *new_string = (char *)allocate(helper, length * sizeof(char));
    memcpy(new_string, string, length * sizeof(char));
    return new_string;
}

/**
* Uses allocate() to create a deep copy of an array of file objects.
*
* @param helper Data structure to pass program data between functions.
* @param files The array of files to be copied.
* @param n_files The length of the file array.
* @return The copied list of files.
*/
struct file *files_dup(void *helper, struct file *files, size_t n_files) {
    struct file *new_files = (struct file *)allocate(helper, n_files * sizeof(struct file));
    for (size_t i=0; i<n_files; i++) {
        struct file new_file = {files[i].hash, str_dup(helper, files[i].file_name)};
        new_files[i] = new_file;
    }
    return new_files;
}

/**
* Appends an element to an array. The array is reallocated if there is
* insufficient space to add the new object.
*
* @param helper Data structure to pass program data between functions.
* @param array An array of any data type.
* @param array_size The number of objects in the array.
* @param array_cap The current allocation size of the array.
* @param element Pointer to the object to be appended.
* @return The modified array.
*/
void *array_add(void *helper, void *array, size_t *array_size, size_t *array_cap, void *element, size_t n) {
    // Ensure allocated memory is sufficient, otherwise reallocate
    if (*array_cap == 0) {
        array = allocate(helper, CAP_INIT * n);
        *array_cap = CAP_INIT;
    } else if ((*array_size) + 1 >= *array_cap) {
        (*array_cap) *= CAP_GROWTH;
        array = reallocate(helper, array, (*array_size)*n, (*array_cap)*n);
    }
    // Add element to the array
    void *ptr = array + ((*array_size) * n);
    memcpy(ptr, element, n);
    (*array_size)++;
    return array;
}

/**
* Checks if a file exists.
*
* @param file_path The specified file path.
* @return 1 if the file exists, otherwise 0.
*/
int file_exists(char *file_path) {
    struct stat buffer;
    int exists = stat(file_path, &buffer);
    if (exists == 0) {
        return 1;
    }
    return 0;
}

/**
* Copies a file from one location to another in the filesystem.
*
* @param file_path The file path of the source file.
* @param new_file_path The destination file path to place the copied file.
*/
void file_copy(char *file_path, char *new_file_path) {
    // Open the source file and get the file length
    int src_fd = open(file_path, O_RDONLY);
    size_t file_size = lseek(src_fd, 0, SEEK_END);

    // Create the destination file with the same size as the source file
    int dest_fd = open(new_file_path, O_RDWR | O_CREAT, 0666);
    ftruncate(dest_fd, file_size);

    // Map both files to the virtual address space
    char *src = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, src_fd, 0);
    char *dest = mmap(NULL, file_size, PROT_READ | PROT_WRITE,
                      MAP_SHARED, dest_fd, 0);

    // Copy the contents from source file to the destination file
    memcpy(dest, src, file_size);

    // Unmap the memory and close the files
    munmap(src, file_size);
    munmap(dest, file_size);
    close(src_fd);
    close(dest_fd);
}

/**
* Given an array of file objects, updates the database directory to contain
* those files. All files are stored in the database as their hash to ensure
* different file versions are distinguishable.
*
* @param files The array of file objects to write to the database.
* @param n_files The size of the file array.
*/
void update_database(struct file *files, size_t n_files) {
    for (size_t i=0; i<n_files; i++) {
        // Convert the file hash into a string
        char hash_string[18];
        sprintf(hash_string, "svc_db/%d", files[i].hash);

        if (file_exists(hash_string) == 1) {
            continue;
        }
        file_copy(files[i].file_name, hash_string);
    }
}

/**
* Given an array of file objects, restores the working directory to match the
* files specified in the array. The restored files are obtained from the
* database directory.
*
* @param files The array of file objects to be restored.
* @param n_files The size of the file array.
* @param overwrite Restoration will overwrite existing files if overwrite is 1.
*/
void update_working_directory(struct file *files, size_t n_files, int overwrite) {
    for (size_t i=0; i<n_files; i++) {
        if (overwrite == 0) {
            if (file_exists(files[i].file_name)) {
                continue;
            }
        }
        // Convert the file hash into a string and append it to the name of
        // the database directory to get the file path of the stored file
        char hash_string[18];
        sprintf(hash_string, "svc_db/%d", files[i].hash);
        file_copy(hash_string, files[i].file_name);
    }
}

/**
* Computes the hash of the file at a specified file path.
*
* @param helper Data structure to pass program data between functions.
* @param file_path The file path of the file.
*/
int hash_file(void *helper, char *file_path) {
    if (file_path == NULL) {
        return -1;
    }

    int hash = 0;
    for (int i=0; file_path[i]!='\0'; i++) {
        hash = hash + file_path[i];
        if (hash >= 1000) {
            hash = hash % 1000;
        }
    }

    // Get the file size and map the file to a region of sufficient size in
    // the virtual memory space.
    int fd = open(file_path, O_RDONLY, S_IRUSR | S_IWUSR);
    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        return -2;
    }
    unsigned char *c = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    // Modulo operation is quite computationally expensive and impacts
    // execution time, only run the loop with modulo when the maximum possible
    // hash for the given file size exceeds the modulus value.
    // As the maximum byte value is 256, the hash can only exceed the modulus
    // when the file size is greater than 2000000000/256 = 7812500
    if (sb.st_size >= 7812500) {
        for (int i=0; i<sb.st_size; i+=32) {
            // Computing the addition of multiple numbers increases speed
            hash += (c[i] + c[i+1] + c[i+2] + c[i+3]
                    + c[i+4] + c[i+5] + c[i+6] + c[i+7]
                    + c[i+8] + c[i+9] + c[i+10] + c[i+11]
                    + c[i+12] + c[i+13] + c[i+14] + c[i+15]
                    + c[i+16] + c[i+17] + c[i+18] + c[i+19]
                    + c[i+20] + c[i+21] + c[i+22] + c[i+23]
                    + c[i+24] + c[i+25] + c[i+26] + c[i+27]
                    + c[i+28] + c[i+29] + c[i+30] + c[i+31]);
            // The majority of iterations will not be impacted by the modulo,
            // so wrapping it in an if statement increases efficiency as
            // modulo uses significantly more clock cycles than an if statement
            if (hash >= 2000000000) {
                hash = hash % 2000000000;
            }
        }
    } else {
        for (int i=0; i<sb.st_size; i+=32) {
            hash += (c[i] + c[i+1] + c[i+2] + c[i+3]
                    + c[i+4] + c[i+5] + c[i+6] + c[i+7]
                    + c[i+8] + c[i+9] + c[i+10] + c[i+11]
                    + c[i+12] + c[i+13] + c[i+14] + c[i+15]
                    + c[i+16] + c[i+17] + c[i+18] + c[i+19]
                    + c[i+20] + c[i+21] + c[i+22] + c[i+23]
                    + c[i+24] + c[i+25] + c[i+26] + c[i+27]
                    + c[i+28] + c[i+29] + c[i+30] + c[i+31]);
        }
    }
    close(fd);
    return hash;
}

/**
* Computes the hash of the file at a specified file path.
*
* @param helper Data structure to pass program data between functions.
* @param file_path The file path of the file.
*/
int file_cmp(const void *p1, const void *p2) {
    struct file *a = (struct file *)p1;
    struct file *b = (struct file *)p2;
    char *a_ptr = a->file_name;
    char *b_ptr = b->file_name;
    while (tolower(*a_ptr) == tolower(*b_ptr)) {
        if (*a_ptr == '\0') {
            return 0;
        }
        a_ptr++;
        b_ptr++;
    }
    return (tolower(*a_ptr) - tolower(*b_ptr));
}

/**
* Determines whether there are uncommitted changes by rehashing and comparing
* the tracked files
*
* @param helper Data structure to pass program data between functions.
* @param file_path The file path of the file.
*/
int uncommitted_changes(void *helper) {
    struct helper *svc = (struct helper *)helper;
    if (svc->head != NULL_ID) {
        if (svc->branches[svc->head].ref_commit != NULL_ID) {
            if (svc->index_size != svc->commits[svc->branches[svc->head].ref_commit].n_files) {
                return 1;
            }
            for (size_t i=0; i<svc->index_size; i++) {
                int new_hash = hash_file(helper, svc->commits[svc->branches[svc->head].ref_commit].files[i].file_name);
                if (svc->commits[svc->branches[svc->head].ref_commit].files[i].hash != svc->index[i].hash
                    || svc->commits[svc->branches[svc->head].ref_commit].files[i].hash != new_hash) {
                    return 1;
                }
            }
        } else {
            if (svc->index_size != 0) {
                return 1;
            }
        }
    }
    return 0;
}

/**
* Computes the difference between two lists of sorted file objects as an array
* of change objects. The file lists must be sorted alphabetically. The algorithm
* used to find changes initialises a reference at the beginning of both arrays.
* Due to the sorted property, at each iteration a file can be determined as
* added, removed, modified or unmodified.
*
* @param helper Data structure to pass program data between functions.
* @param changes_ptr A pointer to where the array of changes will be stored.
* @param n_changes_ptr A pointer to where the number of changes will be stored.
* @param old_files The list of old files to find changes relative to.
* @param old_len The length of the old files array.
* @param new_files The list of new files.
* @param new_len The length of the new files array.
*/
void get_changes(void *helper, struct change **changes_ptr, size_t *n_changes_ptr,
                 struct file *old_files, size_t old_len,
                 struct file *new_files, size_t new_len) {

    // Create the changes array
    struct change *changes = NULL;
    size_t n_changes = 0;
    size_t changes_cap = 0;

    // Iterate through both lists simultaneously to find the changes.
    // As the lists are sorted, one pass through all the elements is sufficient.
    size_t i_old = 0;
    size_t i_new = 0;
    while (1) {
        // Exit loop if both arrays have been fully traversed.
        if (i_old == old_len && i_new == new_len) {
            break;
        }
        // If the old files array has been traversed while the new files still
        // has files remaining, all the remaining files in the new files array
        // must be additions as a result of the arrays in sorted order.
        if (i_old == old_len) {
            // Add the remaining files as additions and exit the loop
            while (i_new != new_len) {
                struct change c = {NULL, new_files + i_new};
                changes = (struct change *)array_add(helper, changes, &n_changes, &changes_cap, &c , sizeof(struct change));
                i_new++;
            }
            break;
        }
        // If the new files array has been traversed while the old files still
        // have files remaining, all the remaining files in the old files array
        // must be deletions.
        else if (i_new == new_len) {
            // Add the remaining files as deletions and exit the loop
            while (i_old != old_len) {
                struct change c = {old_files + i_old, NULL};
                changes = (struct change *)array_add(helper, changes, &n_changes, &changes_cap, &c , sizeof(struct change));
                i_old++;
            }
            break;
        }

        // Compare the files at the current indices for i_old and i_new
        struct file *old = old_files + i_old;
        struct file *new = new_files + i_new;
        int cmp = file_cmp(old, new);

        // If the file names match, then check for modification by comparing
        // hash values
        if (cmp == 0) {
            if (old->hash != new->hash) {
                struct change c = {old, new};
                changes = (struct change *)array_add(helper, changes, &n_changes, &changes_cap, &c , sizeof(struct change));
            }
            i_new++;
            i_old++;
        }
        // If the old file is alphabetically ahead of the new file, then the
        // new file must be an addition due to the sorted property.
        else if (cmp > 0) {
            struct change c = {NULL, new};
            changes = (struct change *)array_add(helper, changes, &n_changes, &changes_cap, &c , sizeof(struct change));
            i_new++;
        }
        // If the old file is alphabetically behind the new file, then the old
        // file must have been removed and is no longer in the new list.
        else {
            struct change c = {old, NULL};
            changes = (struct change *)array_add(helper, changes, &n_changes, &changes_cap, &c , sizeof(struct change));
            i_old++;
        }
    }
    // Set the return values
    *changes_ptr = changes;
    *n_changes_ptr = n_changes;
}

/**
* Performs a commit operation in the version control system, storing a snapshot
* of the current workspace in the database directory.
*
* @param helper Data structure to pass program data between functions.
* @param message Message to be associated with the commit.
* @return The ID of the commit as a hexadecimal string.
*/
char *svc_commit(void *helper, char *message) {
    if (message == NULL) {
        return NULL;
    }
    struct helper *svc = helper;

    // Sort new files in the index
    qsort(svc->index, svc->index_size, sizeof(struct file), file_cmp);

    // Rehash the new files
    size_t i = 0;
    while (i < svc->index_size) {
        int new_hash = hash_file(helper, svc->index[i].file_name);
        // If the file is tracked but does not exist anymore, remove the file
        if (new_hash == -2) {
            for (size_t j=i; j<svc->index_size; j++) {
                svc->index[j] = svc->index[j + 1];
            }
            svc->index_size--;
            memset(svc->index + svc->index_size, 0, sizeof(struct file));
            continue;
        }
        svc->index[i].hash = new_hash;
        i++;
    }

    // Find changes between the head commit and the index
    struct change *changes;
    size_t n_changes;
    if (svc->branches[svc->head].ref_commit == NULL_ID) {
        get_changes(helper, &changes, &n_changes,
                    NULL, 0, svc->index, svc->index_size);
    } else {
        get_changes(helper, &changes, &n_changes,
                    svc->commits[svc->branches[svc->head].ref_commit].files,
                    svc->commits[svc->branches[svc->head].ref_commit].n_files,
                    svc->index, svc->index_size);
    }
    if (n_changes == 0) {
        return NULL;
    }

    // Update the files in the version control database
    update_database(svc->index, svc->index_size);

    // Generate the commit ID
    int message_len = 0;
    int id = 0;
    for (int i=0; message[i]!='\0'; i++) {
        message_len++;
        id = (id + (int)message[i]) % 1000;
    }
    for (size_t i=0; i<n_changes; i++) {
        struct change c = changes[i];
        struct file *f;
        if (c.added_file != NULL && c.removed_file == NULL) {
            f = c.added_file;
            id += 376591;
        } else if (c.added_file == NULL && c.removed_file != NULL)  {
            f = c.removed_file;
            id += 85973;
        } else {
            f = c.added_file;
            id += 9573681;
        }
        for (char *ptr = f->file_name; *ptr != '\0'; ptr++) {
            id = ((id * (((int)(unsigned char)(*ptr)) % 37)) % 15485863) + 1;
        }
    }

    // Convert the commit id to a hexadecimal string
    char *commit_id = (char *)allocate(helper, 7*sizeof(char));
    sprintf(commit_id, "%06x", id);

    // Create the new commit and add it to the list of commits
    char *message_copy = str_dup(helper, message);
    struct file *files_copy = files_dup(helper, svc->index, svc->index_size);
    struct commit new_commit = {commit_id, message_copy,
                                svc->branches[svc->head].ref_commit, NULL_ID,
                                files_copy, svc->index_size,
                                svc->branches[svc->head].branch_name};
    svc->commits = array_add(helper, svc->commits, &svc->n_commits,
                             &svc->commits_cap, &new_commit,
                             sizeof(struct commit));

    // Change current branch pointer to the new commit
    svc->branches[svc->head].ref_commit = svc->n_commits-1;

    return commit_id;
}

/**
* Returns the memory address of a commit object given the ID of the commit.
*
* @param helper Data structure to pass program data between functions.
* @param commit_id The ID of the desired commit.
* @return A pointer to the commit object. NULL if commit is not found.
*/
void *get_commit(void *helper, char *commit_id) {
    if (commit_id == NULL) {
        return NULL;
    }
    struct helper *svc = helper;

    // Loop through all the commits to find a matching ID
    struct commit *c = NULL;
    for (size_t i=0; i<svc->n_commits; i++) {
        if (strcmp(svc->commits[i].commit_id, commit_id) == 0) {
            c = svc->commits + i;
        }
    }
    if (c == NULL) {
        return NULL;
    }
    return (void *)c;
}

/**
* Given a commit object, returns a list of its direct parents' commit IDs in
* a dynamically allocated array.
*
* @param helper Data structure to pass program data between functions.
* @param commit A pointer to a commit object.
* @param A pointer to where the number of parents will be stored.
* @return A dynamically allocated array containing the parents' commit IDs.
*/
char **get_prev_commits(void *helper, void *commit, int *n_prev) {
    if (n_prev == NULL) {
        return NULL;
    }
    *n_prev = 0;
    struct commit *c = (struct commit *)commit;
    struct helper *svc = (struct helper *)helper;
    // Return NULL if either the commit or its first parent is NULL.
    if (c == NULL || c->parent == NULL_ID) {
        return NULL;
    }
    char **prev_commits;
    if (c->parent2 != NULL_ID) {
        *n_prev = 2;
        prev_commits = (char **)malloc(2 * sizeof(char *));
        prev_commits[0] = svc->commits[c->parent].commit_id;
        prev_commits[1] = svc->commits[c->parent2].commit_id;
    } else {
        *n_prev = 1;
        prev_commits = (char **)malloc(sizeof(char *));
        prev_commits[0] = svc->commits[c->parent].commit_id;
    }
    return prev_commits;
}

/**
* Prints the details of a commit.
*
* @param helper Data structure to pass program data between functions.
* @param commit_id The ID of the commit to be printed out.
*/
void print_commit(void *helper, char *commit_id) {
    struct commit *c = (struct commit *)get_commit(helper, commit_id);
    if (c == NULL) {
        printf("Invalid commit id\n");
        return;
    }
    struct helper *svc = (struct helper *)helper;

    // Get the changes between the commit's files and its parent's files.
    struct change *changes;
    size_t n_changes;
    if (c->parent == NULL_ID) {
        get_changes(helper, &changes, &n_changes, NULL, 0, c->files, c->n_files);
    } else if (svc->commits[c->parent].files == NULL) {
        get_changes(helper, &changes, &n_changes, NULL, 0, c->files, c->n_files);
    } else {
        get_changes(helper, &changes, &n_changes, svc->commits[c->parent].files, svc->commits[c->parent].n_files,
                                                            c->files, c->n_files);
    }

    // Print the commit details
    printf("%s [%s]: %s\n", c->commit_id, c->branch_name, c->message);
    for (size_t i=0; i<n_changes; i++) {
        if (changes[i].added_file != NULL && changes[i].removed_file == NULL) {
            printf("    + %s\n", changes[i].added_file->file_name);
        } else if (changes[i].added_file == NULL && changes[i].removed_file != NULL) {
            printf("    - %s\n", changes[i].removed_file->file_name);
        } else {
            printf("    / %s [%10d -> %10d]\n", changes[i].removed_file->file_name,
                                           changes[i].removed_file->hash,
                                           changes[i].added_file->hash);
        }
    }
    printf("\n    Tracked files (%ld):\n", c->n_files);
    for (size_t i=0; i<c->n_files; i++) {
        printf("    [%10d] %s\n", c->files[i].hash, c->files[i].file_name);
    }
}

/**
* Creates a new branch in the version control system.
*
* @param helper Data structure to pass program data between functions.
* @param branch_name The name of the branch.
* @return If successful returns 0, otherwise returns a negative value.
*/
int svc_branch(void *helper, char *branch_name) {
    if (branch_name == NULL) {
        return -1;
    }
    // Check that the name contains only valid characters
    for (char *ptr = branch_name; *ptr != '\0'; ptr++) {
        if (!isalnum(*ptr) && *ptr != '_' && *ptr != '/' && *ptr != '-') {
            return -1;
        }
    }
    struct helper *svc = (struct helper *)helper;

    // Check that the branch name does not already exist
    for (size_t i=0; i<svc->n_branches; i++) {
        if (strcmp(svc->branches[i].branch_name, branch_name) == 0) {
            return -2;
        }
    }
    // Check that there are no uncommitted changed in the index
    if (uncommitted_changes(helper) == 1) {
        return -3;
    }
    // Create the new branch, set its reference commit and add it to the
    // branches array
    char *branch_name_copy = str_dup(helper, branch_name);
    struct branch b = {branch_name_copy, NULL_ID};
    if (svc->head != NULL_ID) {
        b.ref_commit = svc->branches[svc->head].ref_commit;
    }
    svc->branches = array_add(helper, svc->branches, &svc->n_branches,
                              &svc->branches_cap, &b, sizeof(struct branch));
    return 0;
}

/**
* Changes the head to a different branch in the version control system.
*
* @param helper Data structure to pass program data between functions.
* @param branch_name The name of the branch to switch to.
* @return If successful returns 0, otherwise returns a negative value.
*/
int svc_checkout(void *helper, char *branch_name) {
    if (branch_name == NULL) {
        return -1;
    }
    struct helper *svc = (struct helper *)helper;

    // Find the branch by looping through the branches array
    size_t branch_index = NULL_ID;
    for (size_t i=0; i<svc->n_branches; i++) {
        if (strcmp(svc->branches[i].branch_name, branch_name) == 0) {
            branch_index = i;
        }
    }
    if (branch_index == NULL_ID) {
        return -1;
    }
    if (uncommitted_changes(helper) == 1) {
        return -2;
    }
    // Set the head branch
    svc->head = branch_index;

    // Update the list of tracked files in the index
    struct commit new_ref = svc->commits[svc->branches[svc->head].ref_commit];
    if (svc->branches[svc->head].ref_commit != NULL_ID) {
        svc->index = files_dup(helper, new_ref.files, new_ref.n_files);
        svc->index_size = new_ref.n_files;
        svc->index_cap = new_ref.n_files;
    } else {
        svc->index = NULL;
        svc->index_size = 0;
        svc->index_cap = 0;
    }
    // Restore the working directory to the files in the new branch
    update_working_directory(svc->index, svc->index_size, 1);
    return 0;
}

/**
* Returns a list of all branch names in the version control system.
*
* @param helper Data structure to pass program data between functions.
* @param n_branches Pointer to where the number of branches will be stored.
* @return A dynamically allocated array of branch names.
*/
char **list_branches(void *helper, int *n_branches) {
    if (n_branches == NULL) {
        return NULL;
    }
    struct helper *svc = (struct helper *)helper;

    // Create an array to store the list of branch names
    char **branches = (char **)malloc((svc->n_branches)*sizeof(char *));

    // Copy a reference to all branch names and store them in the array
    for (size_t i=0; i<svc->n_branches; i++) {
        struct branch *b = &(svc->branches[i]);
        branches[i] = b->branch_name;
        printf("%s\n", b->branch_name);
    }
    *n_branches = svc->n_branches;
    return branches;
}

/**
* Adds a file to the index (the currently tracked files).
*
* @param file_name The file path of the file to be added.
* @return If successful returns 0, otherwise returns a negative value.
*/
int svc_add(void *helper, char *file_name) {
    if (file_name == NULL) {
        return -1;
    }
    struct helper *svc = helper;

    // Check if the file is already staged in the index
    if (svc->index != NULL) {
        for (size_t i=0; i<svc->index_size; i++) {
            if (strcmp(svc->index[i].file_name, file_name) == 0) {
                return -2;
            }
        }
    }
    // Check that the file exists
    if (file_exists(file_name) == 0) {
        return -3;
    }
    // Create file
    int hash = hash_file(helper, file_name);
    char *file_name_copy = str_dup(helper, file_name);
    struct file f = {hash, file_name_copy};

    // Add file to index
    svc->index = array_add(helper, svc->index, &svc->index_size,
                           &svc->index_cap, &f, sizeof(struct file));
    return hash;
}

/**
* Removes a file from the index (list of tracked files).
*
* @param helper Data structure to pass program data between functions.
* @param file_name The path of the file to be removed.
* @return If successful returns 0, otherwise returns a negative value.
*/
int svc_rm(void *helper, char *file_name) {
    if (file_name == NULL) {
        return -1;
    }
    // Cast the void pointer to the correct type
    struct helper *svc = helper;

    // Check that the file is being tracked
    if (svc->index != NULL) {
        for (size_t i=0; i<svc->index_size; i++) {
            if (strcmp(svc->index[i].file_name, file_name) == 0) {
                int hash = svc->index[i].hash;

                // Remove the file from the index
                svc->index[i].file_name = NULL;
                svc->index_size--;
                svc->index[i] = svc->index[svc->index_size];
                memset(svc->index + svc->index_size, 0, sizeof(struct file));
                return hash;
            }
        }
    }
    return -2;
}

/**
* Resets the current branch to a previous commit.
*
* @param helper Data structure to pass program data between functions.
* @param commit_id The ID of the commit to reset to.
* @return If successful returns 0, otherwise returns a negative value.
*/
int svc_reset(void *helper, char *commit_id) {
    if (commit_id == NULL) {
        return -1;
    }
    struct helper *svc = (struct helper *)helper;

    // Find the index of the target commit by looping through the commits array
    size_t target_index = NULL_ID;
    for (size_t i=0; i<svc->n_commits; i++) {
        struct commit *c = svc->commits + i;
        if (strcmp(c->commit_id, commit_id) == 0) {
            target_index = i;
            break;
        }
    }
    if (target_index == NULL_ID) {
        return -2;
    }

    // Set the head to point to the target commit
    struct branch *head = svc->branches + svc->head;
    head->ref_commit = target_index;

    // Update the index to match the files in the target commit
    struct commit target = svc->commits[head->ref_commit];
    svc->index = files_dup(helper, target.files, target.n_files);
    svc->index_size = target.n_files;
    svc->index_cap = target.n_files;

    // Restore the working directory to contain the target commit files
    update_working_directory(svc->index, svc->index_size, 1);
    return 0;
}

/**
* Merges another branch into the currently active branch.
*
* @param helper Data structure to pass program data between functions.
* @param branch_name The name of the branch to be merged into the current one.
* @param resolutions Array of resolutions to modify files in the merge.
* @param n_resolutions The size of the resolutions array.
* @return The commit ID of the merged commit.
*/
char *svc_merge(void *helper, char *branch_name,
                struct resolution *resolutions, int n_resolutions) {

    if (branch_name == NULL) {
        printf("Invalid branch name\n");
        return NULL;
    }
    struct helper *svc = (struct helper *)helper;

    // Loop through the branches to find the target branch
    struct branch *merge_branch = NULL;
    for (size_t i=0; i<svc->n_branches; i++) {
        if (strcmp(svc->branches[i].branch_name, branch_name) == 0) {
            merge_branch = svc->branches + i;
            break;
        }
    }
    if (merge_branch == NULL) {
        printf("Branch not found\n");
        return NULL;
    }
    if (strcmp(svc->branches[svc->head].branch_name, branch_name) == 0) {
        printf("Cannot merge a branch with itself\n");
        return NULL;
    }
    if (uncommitted_changes(helper) == 1) {
        printf("Changes must be committed\n");
        return NULL;
    }

    // The below algorithm is a modified version of the algorithm used in
    // get_changes() to find the difference between two file object arrays.
    // The index contains the currently tracked files. All the files in the
    // target branch are added to the index, with conflicts being resolved.
    // Once a full file list is constructed, the task is reduced to a commit
    // and commit can be called to complete the rest of the merge.
    size_t index_len = svc->index_size;
    struct file *target_files = svc->commits[merge_branch->ref_commit].files;
    size_t target_len = svc->commits[merge_branch->ref_commit].n_files;

    // Update the working directory to contain the files in the target branch
    // in preparation for the merge so all files are accessible.
    update_working_directory(target_files, target_len, 0);

    size_t i_target = 0;
    size_t i_index = 0;
    while (1) {
        // Exit the loop if all files have been traversed
        if (i_target == target_len && i_index == index_len) {
            break;
        }
        // If all files in the target branch have been traversed, then there
        // are no more files to add to the index.
        if (i_target == target_len) {
            break;
        }
        // If all the files in the index have been traversed but there are still
        // files remaining in the target branch, then those files must be added
        // to the index.
        else if (i_index == index_len) {
            // Add all remaining files to the index
            while (i_target != target_len) {
                char *file_name = str_dup(helper, target_files[i_target].file_name);
                struct file new_file = {target_files[i_target].hash, file_name};
                svc->index = array_add(helper, svc->index, &svc->index_size,
                            &svc->index_cap, &new_file, sizeof(struct file));
                i_target++;
            }
            break;
        }

        // Get the files at the current indices for i_target and i_index
        struct file *tar_file = target_files + i_target;
        struct file *idx_file = svc->index + i_index;

        // Compare the two files alphabetically
        int cmp = file_cmp(tar_file, idx_file);

        // If the file is in both commmits, check the resolutions array
        // to resolve the conflict
        if (cmp == 0) {
            for (int i=0; i<n_resolutions; i++) {
                if (strcmp(resolutions[i].file_name, idx_file->file_name) == 0) {
                    if (resolutions[i].resolved_file == NULL) {
                        svc->index[i_index].file_name = NULL;
                        for (size_t j=i_index; j<svc->index_size-1; j++) {
                            svc->index[j] = svc->index[j+1];
                        }
                        svc->index_size--;
                        i_index--;
                        index_len--;
                    } else {
                        file_copy(resolutions[i].resolved_file, resolutions[i].file_name);
                    }
                    break;
                }
            }
            i_index++;
            i_target++;
        }

        // If the target file is ahead of the index file alphabetically, then
        // this means the index file is not in the target's list of files.
        // In this case, nothing needs to be added, but resolutions must still
        // be checked.
        else if (cmp > 0) {
            for (int i=0; i<n_resolutions; i++) {
                if (strcmp(resolutions[i].file_name, idx_file->file_name) == 0) {
                    if (resolutions[i].resolved_file == NULL) {
                        for (size_t j=i_index; j<svc->index_size-1; j++) {
                            svc->index[j] = svc->index[j+1];
                        }
                        svc->index_size--;
                        i_index--;
                        index_len--;
                    } else {
                        file_copy(resolutions[i].resolved_file, resolutions[i].file_name);
                    }
                    break;
                }
            }
            i_index++;
        }

        // If the target file is behind the index file alphabetically, then
        // the target file is not in the index's file list. In this case the
        // target file must be added to the index's file list after resolutions
        // are solved.
        else {
            int add = 1;
            for (int i=0; i<n_resolutions; i++) {
                if (strcmp(resolutions[i].file_name, tar_file->file_name) == 0) {
                    if (resolutions[i].resolved_file == NULL) {
                        add = 0;
                    } else {
                        file_copy(resolutions[i].resolved_file,
                                  resolutions[i].file_name);
                    }
                    break;
                }
            }
            // The file is added if it was not resolved to NULL
            if (add == 1) {
                char *file_name = str_dup(helper, tar_file->file_name);
                struct file new_file = {tar_file->hash, file_name};
                svc->index = array_add(helper, svc->index, &svc->index_size,
                               &svc->index_cap, &new_file, sizeof(struct file));
            }
            i_target++;
        }
    }

    // Construct the commit message and call the commit function with the
    // finalised file list in the index.
    char commit_msg[150];
    sprintf(commit_msg, "Merged branch %s", branch_name);
    char *commit_id = svc_commit(helper, commit_msg);
    svc->commits[svc->n_commits-1].parent2 = merge_branch->ref_commit;

    printf("Merge successful\n");
    return commit_id;
}
