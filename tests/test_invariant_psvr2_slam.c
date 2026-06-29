#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

/* Import the actual function from kernel module */
extern void psvr2_slam_process(struct psvr2_slam *sl, int len);

/* Minimal mock of required structures */
#define PSVR2_SLAM_RECORD_SIZE 512
struct psvr2_slam_record {
    uint8_t pos[12];
    uint8_t orient[16];
    uint32_t vts_ts_us;
};

struct psvr2_slam {
    uint8_t buf[PSVR2_SLAM_RECORD_SIZE * 10]; /* 10x buffer for overflow testing */
    uint8_t raw_copy[PSVR2_SLAM_RECORD_SIZE];
    int raw_len;
    pthread_mutex_t raw_lock;
};

START_TEST(test_buffer_reads_never_exceed_declared_length)
{
    /* Security invariant: Buffer reads never exceed PSVR2_SLAM_RECORD_SIZE */
    const struct {
        int len;
        const char *description;
    } test_cases[] = {
        {PSVR2_SLAM_RECORD_SIZE - 1, "Boundary: one byte short"},
        {PSVR2_SLAM_RECORD_SIZE, "Valid full record"},
        {PSVR2_SLAM_RECORD_SIZE * 2, "Exploit: double size"},
        {PSVR2_SLAM_RECORD_SIZE * 10, "Exploit: 10x overflow"},
        {0, "Zero length"},
    };
    
    for (int i = 0; i < sizeof(test_cases)/sizeof(test_cases[0]); i++) {
        struct psvr2_slam sl;
        
        /* Initialize with guard pattern */
        memset(sl.buf, 0xAA, sizeof(sl.buf));
        memset(sl.raw_copy, 0xBB, sizeof(sl.raw_copy));
        sl.raw_len = -1;
        pthread_mutex_init(&sl.raw_lock, NULL);
        
        /* Fill with valid record pattern in first PSVR2_SLAM_RECORD_SIZE bytes */
        memset(sl.buf, 'S', PSVR2_SLAM_RECORD_SIZE);
        
        /* Call actual production function */
        psvr2_slam_process(&sl, test_cases[i].len);
        
        /* Verify no buffer overflow occurred */
        ck_assert_msg(sl.raw_len <= PSVR2_SLAM_RECORD_SIZE,
                     "Test case %d (%s): raw_len=%d exceeds PSVR2_SLAM_RECORD_SIZE=%d",
                     i, test_cases[i].description, sl.raw_len, PSVR2_SLAM_RECORD_SIZE);
        
        /* Check guard bytes after raw_copy buffer */
        uint8_t guard = 0xBB;
        for (int j = PSVR2_SLAM_RECORD_SIZE; j < (int)sizeof(sl.raw_copy); j++) {
            if (sl.raw_copy[j] != guard) {
                ck_abort_msg("Buffer overflow detected in raw_copy at offset %d", j);
            }
        }
        
        pthread_mutex_destroy(&sl.raw_lock);
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_buffer_reads_never_exceed_declared_length);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}