#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <errno.h>
#ifndef _WIN32
#include <termios.h>
#include <sys/ioctl.h>
#else
#include "win32fixes.h"
#endif

#if (ULONG_MAX == 4294967295UL)
#define MEMTEST_32BIT
#elif (ULONG_MAX == 18446744073709551615ULL)
#define MEMTEST_64BIT
#else
#error "ULONG_MAX value not supported."
#endif

#ifndef MEMTEST_32BIT
#define ULONG_ONEZERO 0xaaaaaaaaaaaaaaaaUL
#define ULONG_ZEROONE 0x5555555555555555UL
#else
#define ULONG_ONEZERO 0xaaaaaaaaUL
#define ULONG_ZEROONE 0x55555555UL
#endif

#ifdef _WIN32
typedef struct winsize
{
	unsigned short ws_row;
	unsigned short ws_col;
};
#endif

static struct winsize ws;
size_t progress_printed; /* Printed chars in screen-wide progress bar. */
size_t progress_full; /* How many chars to write to fill the progress bar. */

void memtest_progress_start(char *title, int pass) {
    int j;

    printf("\x1b[H\x1b[2J");    /* Cursor home, clear screen. */
    /* Fill with dots. */
    for (j = 0; j < ws.ws_col*(ws.ws_row-2); j++) printf(".");
    printf("Please keep the test running several minutes per GB of memory.\n");
    printf("Also check http://www.memtest86.com/ and http://pyropus.ca/software/memtester/");
    printf("\x1b[H\x1b[2K");          /* Cursor home, clear current line.  */
    printf("%s [%d]\n", title, pass); /* Print title. */
    progress_printed = 0;
    progress_full = ws.ws_col*(ws.ws_row-3);
    fflush(stdout);
}

void memtest_progress_end(void) {
    printf("\x1b[H\x1b[2J");    /* Cursor home, clear screen. */
}

void memtest_progress_step(size_t curr, size_t size, char c) {
    size_t chars = (curr*progress_full)/size, j;

    for (j = 0; j < chars-progress_printed; j++) {
        printf("%c",c);
        progress_printed++;
    }
    fflush(stdout);
}

/* Test that addressing is fine. Every location is populated with its own
 * address, and finally verified. This test is very fast but may detect
 * ASAP big issues with the memory subsystem. */
void memtest_addressing(unsigned long *l, size_t bytes) {
    unsigned long words = (unsigned long)(bytes/sizeof(unsigned long));
    unsigned long j, *p;

    /* Fill */
    p = l;
    for (j = 0; j < words; j++) {
        *p = (unsigned long)p;
        p++;
        if ((j & 0xffff) == 0) memtest_progress_step(j,words*2,'A');
    }
    /* Test */
    p = l;
    for (j = 0; j < words; j++) {
        if (*p != (unsigned long)p) {
            printf("\n*** MEMORY ADDRESSING ERROR: %p contains %lu\n",
                (void*) p, *p);
            exit(1);
        }
        p++;
        if ((j & 0xffff) == 0) memtest_progress_step(j+words,words*2,'A');
    }
}

/* Fill words stepping a single page at every write, so we continue to
 * touch all the pages in the smallest amount of time reducing the
 * effectiveness of caches, and making it hard for the OS to transfer
 * pages on the swap. */
void memtest_fill_random(unsigned long *l, size_t bytes) {
    unsigned long step = (unsigned long)(4096/sizeof(unsigned long));
    unsigned long words = (unsigned long)(bytes/sizeof(unsigned long)/2);
    unsigned long iwords = words/step;  /* words per iteration */
    unsigned long off, w, *l1, *l2;

    assert((bytes & 4095) == 0);
    for (off = 0; off < step; off++) {
        l1 = l+off;
        l2 = l1+words;
        for (w = 0; w < iwords; w++) {
#ifdef MEMTEST_32BIT
            *l1 = *l2 = ((unsigned long)     (rand()&0xffff)) |
                        (((unsigned long)    (rand()&0xffff)) << 16);
#else
            *l1 = *l2 = ((unsigned long)     (rand()&0xffff)) |
                        (((unsigned long)    (rand()&0xffff)) << 16) |
                        (((unsigned long)    (rand()&0xffff)) << 32) |
                        (((unsigned long)    (rand()&0xffff)) << 48);
#endif
            l1 += step;
            l2 += step;
            if ((w & 0xffff) == 0)
                memtest_progress_step(w+iwords*off,words,'R');
        }
    }
}

/* Like memtest_fill_random() but uses the two specified values to fill
 * memory, in an alternated way (v1|v2|v1|v2|...) */
void memtest_fill_value(unsigned long *l, size_t bytes, unsigned long v1,
                        unsigned long v2, char sym)
{
    unsigned long step = (unsigned long)(4096/sizeof(unsigned long));
    unsigned long words = (unsigned long)(bytes/sizeof(unsigned long)/2);
    unsigned long iwords = words/step;  /* words per iteration */
    unsigned long off, w, *l1, *l2, v;

    assert((bytes & 4095) == 0);
    for (off = 0; off < step; off++) {
        l1 = l+off;
        l2 = l1+words;
        v = (off & 1) ? v2 : v1;
        for (w = 0; w < iwords; w++) {
#ifdef MEMTEST_32BIT
            *l1 = *l2 = ((unsigned long)     (rand()&0xffff)) |
                        (((unsigned long)    (rand()&0xffff)) << 16);
#else
            *l1 = *l2 = ((unsigned long)     (rand()&0xffff)) |
                        (((unsigned long)    (rand()&0xffff)) << 16) |
                        (((unsigned long)    (rand()&0xffff)) << 32) |
                        (((unsigned long)    (rand()&0xffff)) << 48);
#endif
            l1 += step;
            l2 += step;
            if ((w & 0xffff) == 0)
                memtest_progress_step(w+iwords*off,words,sym);
        }
    }
}

void memtest_compare(unsigned long *l, size_t bytes) {
    unsigned long words = (unsigned long)(bytes/sizeof(unsigned long)/2);
    unsigned long w, *l1, *l2;

    assert((bytes & 4095) == 0);
    l1 = l;
    l2 = l1+words;
    for (w = 0; w < words; w++) {
        if (*l1 != *l2) {
            printf("\n*** MEMORY ERROR DETECTED: %p != %p (%lu vs %lu)\n",
                (void*)l1, (void*)l2, *l1, *l2);
            exit(1);
        }
        l1 ++;
        l2 ++;
        if ((w & 0xffff) == 0) memtest_progress_step(w,words,'=');
    }
}

void memtest_compare_times(unsigned long *m, size_t bytes, int pass, int times) {
    int j;

    for (j = 0; j < times; j++) {
        memtest_progress_start("Compare",pass);
        memtest_compare(m,bytes);
        memtest_progress_end();
    }
}

void memtest_test(size_t megabytes, int passes) {
    size_t bytes = megabytes*1024*1024;
    unsigned long *m = malloc(bytes);
    int pass = 0;

    if (m == NULL) {
        fprintf(stderr,"Unable to allocate %zu megabytes: %s",
            megabytes, strerror(errno));
        exit(1);
    }
    while (pass != passes) {
        pass++;

        memtest_progress_start("Addressing test",pass);
        memtest_addressing(m,bytes);
        memtest_progress_end();

        memtest_progress_start("Random fill",pass);
        memtest_fill_random(m,bytes);
        memtest_progress_end();
        memtest_compare_times(m,bytes,pass,4);

        memtest_progress_start("Solid fill",pass);
        memtest_fill_value(m,bytes,0,(unsigned long)-1,'S');
        memtest_progress_end();
        memtest_compare_times(m,bytes,pass,4);

        memtest_progress_start("Checkerboard fill",pass);
        memtest_fill_value(m,bytes,ULONG_ONEZERO,ULONG_ZEROONE,'C');
        memtest_progress_end();
        memtest_compare_times(m,bytes,pass,4);
    }
}

void memtest(size_t megabytes, int passes) {
#ifdef _WIN32
	HANDLE hOut;
    CONSOLE_SCREEN_BUFFER_INFO b;

	hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (GetConsoleScreenBufferInfo(hOut, &b)) {
		ws.ws_col = b.dwSize.X;
		ws.ws_row = b.dwSize.Y;
	} else {
        ws.ws_col = 80;
        ws.ws_row = 20;
	}
#else
    if (ioctl(1, TIOCGWINSZ, &ws) == -1) {
        ws.ws_col = 80;
        ws.ws_row = 20;
    }
#endif
    memtest_test(megabytes,passes);
    printf("\nYour memory passed this test.\n");
    printf("Please if you are still in doubt use the following two tools:\n");
    printf("1) memtest86: http://www.memtest86.com/\n");
    printf("2) memtester: http://pyropus.ca/software/memtester/\n");
    exit(0);
}
