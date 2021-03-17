#include <assert.h>
#include "svc.h"

/* Compile:
clang -o test svc.c tester.c -O0 -std=gnu11 -lm -Wextra -Wall -g -fsanitize=address
Note: Removed Werror flag.
*/

void printfile() {
    int fd = open("hello.py", O_RDONLY);
    size_t pagesize = getpagesize();
    char *region = mmap((void *)(pagesize * (1 << 20)), pagesize,
                        PROT_READ, MAP_FILE | MAP_PRIVATE, fd, 0);
    fwrite(region, 1, pagesize, stdout);
    int unmap_result = munmap(region, pagesize);
    close(fd);
}

void file_copy1(char *file_path, char *new_file_path) {
    FILE *in = fopen(file_path, "r");
    FILE *out = fopen(new_file_path, "w");

    int c;
    while ((c=fgetc(in)) != EOF) {
        fputc(c, out);
    }

    fclose(out);
    fclose(in);
}

void small() {
    void *helper = svc_init();

    svc_add(helper, "hello.py");
    char *id = svc_commit(helper, "Initial commit");
    void *c = get_commit(helper, id);
    print_commit(helper, id);
    svc_add(helper, "Tests/test1.in");
    id = svc_commit(helper, "Second commit");
    c = get_commit(helper, id);
    print_commit(helper, id);

    cleanup(helper);
}

int test_example1() {
    void *helper = svc_init();
    assert(hash_file(helper, "hello.py") == 2027);
    assert(hash_file(helper, "fake.c") == -2);
    assert(svc_commit(helper, "No changes") == NULL);
    assert(svc_add(helper, "hello.py") == 2027);
    assert(svc_add(helper, "Tests/test1.in") == 564);
    assert(svc_add(helper, "Tests/test1.in") == -2);
    char *str = svc_commit(helper, "Initial commit");
    // printf("%s\n", str);
    cleanup(helper);
    return 0;
}

int test_example2() {
    void *helper = svc_init();
    svc_add(helper, "hello.py");

    char *str = svc_commit(helper, "Initial commit");

    svc_add(helper, "test_1.txt");

    // svc_branch(helper, "random_branch");

    // svc_checkout(helper, "random_branch");

    svc_commit(helper, "commit 2");

    svc_add(helper, "Tests/diff.txt");

    svc_commit(helper, "commit 3");

    svc_reset(helper, str);

    // assert(svc_add(helper, "Tests/test1.in") == -2);
    // char *str = svc_commit(helper, "Initial commit");
    // printf("%s\n", str);

    cleanup(helper);
    return 0;
}

int test_example21() {
    void *helper = svc_init();

    file_copy("COMP2017/c.c", "COMP2017/svc.c");
    file_copy("COMP2017/h.h", "COMP2017/svc.h");

    assert(svc_add(helper, "COMP2017/svc.h") == 5007);
    assert(svc_add(helper, "COMP2017/svc.c") == 5217);
    assert(strcmp(svc_commit(helper, "Initial commit"), "7b3e30") == 0);
    assert(svc_branch(helper, "random_branch") == 0);
    assert(svc_checkout(helper, "random_branch") == 0);

    file_copy("COMP2017/c0.c", "COMP2017/svc.c");
    assert(hash_file(helper, "COMP2017/svc.c") == 4798);
    // printf("%d\n", svc_rm(helper, "COMP2017/svc.h") == 5007);
    assert(svc_rm(helper, "COMP2017/svc.h") == 5007);

    char *id = svc_commit(helper, "Implemented svc_init");
    // print_commit(helper, id);
    // if (id == NULL) {
    //     printf("%s", id);
    // } else {
    //     printf("1NULL\n");
    // }
    assert(strcmp(id, "73eacd") == 0);
    assert(svc_reset(helper, "7b3e30") == 0);
    file_copy("COMP2017/c0.c", "COMP2017/svc.c");
    id = svc_commit(helper, "Implemented svc_init");
    // if (id == NULL) {
    //     printf("%s", id);
    // } else {
    //     printf("2NULL\n");
    // }
    // print_commit(helper, id);
    assert(strcmp(id, "24829b") == 0);

    void *commit = get_commit(helper, "24829b");

    int n_prev;
    char **prev_commits = get_prev_commits(helper, commit, &n_prev);
    free(prev_commits);

    printf("n_prev: %d\n", n_prev);
    assert(n_prev == 1);
    assert(svc_checkout(helper, "master") == 0);

    resolution *resolutions = malloc(sizeof(resolution));
    resolutions[0].file_name = "COMP2017/svc.c";
    resolutions[0].resolved_file = "resolutions/svc.c";

    svc_merge(helper, "random_branch", resolutions, 1);

    free(resolutions);

    commit = get_commit(helper, "48eac3");
    prev_commits = get_prev_commits(helper, commit, &n_prev);
    // printf("n_prev: %d\n", n_prev);
    assert(n_prev == 2);
    free(prev_commits);



    // assert(svc_add(helper, "Tests/test1.in") == -2);
    // char *str = svc_commit(helper, "Initial commit");
    // printf("%s\n", str);

    cleanup(helper);
    return 0;
}


int test_1() {
    FILE *f = fopen("test_1.txt", "w");
    fputs("1", f);
    fclose(f);
    void *helper = svc_init();

    svc_add(helper, "test_1.txt");
    printf("\nCommit 1:\n");
    svc_commit(helper, "hello");
    f = fopen("test_1.txt", "w");
    fputs("fdjlahsguharjwhngfaiurhfnahf", f);
    fclose(f);

    printf("\nCommit 2:\n");
    char *str = svc_commit(helper, "2nd commit");
    // printf("%s\n", str);

    cleanup(helper);
    return 0;
}

int test_add_remove() {
    void *helper = svc_init();
    struct helper *svc = (struct helper *)helper;

    svc_add(helper, "Tests/diff.txt");
    printf("size: %d, cap: %d\n", svc->index_size, svc->index_cap);

    // for (int i=0; i<svc->index_size; i++) {
    //     printf("%s\n", svc->index[i]->file_name);
    // }
    assert(svc_add(helper, "Tests/diff.txt") == -2);

    svc_add(helper, "Tests/diff0.txt");
    printf("size: %d, cap: %d\n", svc->index_size, svc->index_cap);
    // for (int i=0; i<svc->index_size; i++) {
    //     printf("%s\n", svc->index[i]->file_name);
    // }

    svc_commit(helper, "hello");


    svc_add(helper, "Tests/diff3.txt");
    printf("size: %d, cap: %d\n", svc->index_size, svc->index_cap);


    svc_add(helper, "Tests/diff4.txt");
    printf("size: %d, cap: %d\n", svc->index_size, svc->index_cap);

    // svc_rm(helper, "Tests/diff.txt");

    svc_commit(helper, "hello2");

    svc_rm(helper, "Tests/diff4.txt");
    printf("size: %d, cap: %d\n", svc->index_size, svc->index_cap);

    svc_commit(helper, "After remove");

    cleanup(helper);
    return 0;
}

int test_branches() {
    void *helper = svc_init();

    svc_branch(helper, "branch1");
    svc_branch(helper, "branch2");
    svc_branch(helper, "branch3");
    svc_branch(helper, "branch4");
    svc_branch(helper, "branch5");
    svc_branch(helper, "branch6");
    svc_branch(helper, "branch7");
    svc_branch(helper, "branch8");

    int n_branches;
    char **branches = list_branches(helper, &n_branches);
    printf("n_branches: %d\n", n_branches);
    for (int i=0; i<n_branches; i++) {
        printf("%s\n", branches[i]);
    }
    free(branches);

    cleanup(helper);
    return 0;
}

int test_hash_file() {
    void *helper = svc_init();

    int hash;

    hash = hash_file(helper, "hello.py");
    // printf("hash: %d\n", hash);

    cleanup(helper);

    return 0;
}

int test_hash_file_big() {
    void *helper = svc_init();

    int hash;

    hash = hash_file(helper, "Tests/diff.txt");
    // printf("hash: %d\n", hash);

    cleanup(helper);

    return 0;
}


// size_t n_pages = 0;
// size_t page_size;
// void *mem = NULL;
// size_t offset;
//
// void mem_init() {
//     page_size = sysconf(_SC_PAGE_SIZE);
//     assert(page_size == 4096);
// }
//
// void *allocate(size_t n) {
//     if (mem == NULL) {
//         int fd = open("/dev/zero", O_RDWR);
//         mem = mmap(NULL, 1, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
//         n_pages++;
//     }
//     // else if (offset + n >= page_size *n_pages) {
//     //     mem = mremap(mem, n_pages*page_size, (n_pages+1)*page_size, MREMAP_MAYMOVE);
//     // }
//
//     void *addr = mem + offset;
//     offset += n;
//
//     return addr;
// }

int main(int argc, char **argv) {

    // TODO: write your own tests here
    // Hint: you can use assert(EXPRESSION) if you want
    // e.g.  assert((2 + 3) == 5);
    // test_1();
    // test_add_remove();
    test_example1();
    // small();
    // printf("%d\n", PROT_READ);

    // file_copy("Tests/diff.txt", "Tests/diff1000.txt");

    // int fd = open("/dev/zero", O_RDWR);
    // char *mem = mmap(NULL, sysconf(_SC_PAGE_SIZE), PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    //
    // strcpy(mem, "Hello");
    // printf("%s\n", mem);
    //
    // printf("strcmp: %d\n", strcmp(mem, "Hello"));



    // printfile();

    // clock_t begin, end;
    //
    //
    // begin = clock();
    // char *string = allocate(10000);
    // end = clock();
    // for (int i=0; i<1; i++) {
    //     strcpy(string, "hello my name is david.");
    //     printf("%s\n", string);
    // }
    // printf("Execution Time: %f seconds.\n", (double)(end-begin)/CLOCKS_PER_SEC);
    // munmap(mem, n_pages*page_size);
    //
    // begin = clock();
    // char *str = malloc(100);
    // end = clock();
    // for (int i=0; i<1; i++) {
    //     strcpy(str, "hello my name is david.");
    //     printf("%s\n", str);
    // }
    // printf("Execution Time: %f seconds.\n", (double)(end-begin)/CLOCKS_PER_SEC);
    // free(str);

    // void *helper = svc_init();
    // int hash;
    // clock_t begin1, end1;
    // //
    // //
    // // begin = clock();
    // //
    // // for (int i=0; i<1000; i++) {
    // //     hash = hash_file(helper, "hello.py");
    // // }
    // // printf("%d\n", hash);
    // //
    // // end = clock();
    // // printf("Execution Time: %f seconds.\n", (double)(end-begin)/CLOCKS_PER_SEC);
    // //
    // //
    // //
    // begin1 = clock();
    //
    // for (int i=0; i<100; i++) {
    //     hash = hash_file(helper, "Tests/diff.txt");
    // }
    // printf("%d\n", hash);
    //
    // end1 = clock();
    // printf("Execution Time: %f seconds.\n", (double)(end1-begin1)/CLOCKS_PER_SEC);
    //
    // cleanup(helper);
    return 0;
}
