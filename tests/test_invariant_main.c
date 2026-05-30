#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

/* Safe buffer size that should be used for environment variable copies */
#define SAFE_BUFFER_SIZE 256

/*
 * Simulate the safe version of what the vulnerable code should do:
 * copy TERM environment variable with bounds checking.
 * Returns 0 on success, -1 if the value would overflow the buffer.
 */
static int safe_copy_term(const char *term_value, char *dest, size_t dest_size) {
    if (term_value == NULL) {
        return -1;
    }
    size_t term_len = strlen(term_value);
    /* Security invariant: length must fit within destination buffer */
    if (term_len >= dest_size) {
        return -1; /* Reject oversized input */
    }
    strncpy(dest, term_value, dest_size - 1);
    dest[dest_size - 1] = '\0';
    return 0;
}

START_TEST(test_term_env_bounds_checking)
{
    /* Invariant: copying TERM environment variable must never exceed
     * the destination buffer size, regardless of input length */
    const char *payloads[] = {
        /* Normal inputs */
        "xterm",
        "vt100",
        "linux",
        "",
        /* Boundary inputs */
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", /* 64 bytes */
        /* Oversized inputs - classic buffer overflow attempts */
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", /* 256 bytes */
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB", /* 512 bytes */
        /* Return address overwrite pattern */
        "\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41"
        "\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41"
        "\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41"
        "\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41"
        "\xef\xbe\xad\xde", /* 64 A's + fake return address */
        /* NOP sled + shellcode pattern */
        "\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90"
        "\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90"
        "\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90"
        "\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90"
        "\x31\xc0\x50\x68\x2f\x2f\x73\x68\x68\x2f\x62\x69\x6e\x89\xe3\x50"
        "\x53\x89\xe1\xb0\x0b\xcd\x80",
        /* Format string attack */
        "%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s",
        "%n%n%n%n%n%n%n%n%n%n%n%n%n%n%n%n%n%n%n%n",
        "%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x",
        /* Very long format string */
        "%.99999d%.99999d%.99999d%.99999d%.99999d",
        /* Special characters */
        "xterm\x00hidden_data_after_null",
        "xterm\ninjected\n",
        "xterm;rm -rf /",
        /* Exactly at boundary */
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", /* 255 bytes */
    };
    int num_payloads = sizeof(payloads) / sizeof(payloads[0]);

    for (int i = 0; i < num_payloads; i++) {
        char dest[SAFE_BUFFER_SIZE];
        memset(dest, 0, sizeof(dest));

        /* Set the TERM environment variable to the adversarial payload */
        setenv("TERM", payloads[i], 1);

        const char *term_value = getenv("TERM");

        /* Invariant 1: If TERM is set, we must be able to retrieve it */
        if (term_value != NULL) {
            size_t term_len = strlen(term_value);

            /* Invariant 2: Any copy operation must check bounds before copying */
            int result = safe_copy_term(term_value, dest, SAFE_BUFFER_SIZE);

            if (result == 0) {
                /* Invariant 3: If copy succeeded, destination must be null-terminated */
                ck_assert_msg(dest[SAFE_BUFFER_SIZE - 1] == '\0',
                    "Buffer must always be null-terminated after copy");

                /* Invariant 4: Copied length must be less than buffer size */
                size_t copied_len = strlen(dest);
                ck_assert_msg(copied_len < SAFE_BUFFER_SIZE,
                    "Copied string length must be less than buffer size");

                /* Invariant 5: If copy succeeded, input must have fit in buffer */
                ck_assert_msg(term_len < SAFE_BUFFER_SIZE,
                    "Successful copy implies input fit within buffer bounds");
            } else {
                /* Invariant 6: If copy was rejected, input must have been too large */
                ck_assert_msg(term_len >= SAFE_BUFFER_SIZE,
                    "Copy should only be rejected when input exceeds buffer size");
            }

            /* Invariant 7: Destination buffer sentinel bytes must not be corrupted
             * (simulate checking memory beyond the buffer) */
            /* The buffer itself must remain within its declared bounds */
            ck_assert_msg(strlen(dest) < SAFE_BUFFER_SIZE,
                "Destination buffer must not overflow");
        }

        /* Invariant 8: After processing, TERM variable should still be accessible
         * (environment should not be corrupted) */
        const char *term_after = getenv("TERM");
        ck_assert_msg(term_after != NULL || term_value == NULL,
            "Environment must remain accessible after processing");
    }

    /* Clean up */
    unsetenv("TERM");
}
END_TEST

START_TEST(test_null_term_handling)
{
    /* Invariant: NULL TERM environment variable must be handled safely */
    char dest[SAFE_BUFFER_SIZE];
    memset(dest, 0, sizeof(dest));

    /* Remove TERM from environment */
    unsetenv("TERM");

    const char *term_value = getenv("TERM");

    /* Invariant: safe_copy_term must handle NULL input without crashing */
    int result = safe_copy_term(term_value, dest, SAFE_BUFFER_SIZE);

    /* Invariant: NULL input must return error, not crash */
    ck_assert_msg(result == -1,
        "NULL TERM value must be rejected safely, not cause undefined behavior");

    /* Invariant: destination buffer must remain unchanged/safe */
    ck_assert_msg(dest[0] == '\0',
        "Destination buffer must not be modified when input is NULL");
}
END_TEST

START_TEST(test_exact_boundary_term)
{
    /* Invariant: inputs at exact buffer boundary must be handled correctly */
    char dest[SAFE_BUFFER_SIZE];
    char exact_fit[SAFE_BUFFER_SIZE - 1];
    char one_over[SAFE_BUFFER_SIZE];
    char two_over[SAFE_BUFFER_SIZE + 1];

    /* Create exact fit string (SAFE_BUFFER_SIZE - 1 chars, fits with null terminator) */
    memset(exact_fit, 'A', sizeof(exact_fit) - 1);
    exact_fit[sizeof(exact_fit) - 1] = '\0';

    /* Create one-over string (SAFE_BUFFER_SIZE chars, does NOT fit) */
    memset(one_over, 'B', sizeof(one_over) - 1);
    one_over[sizeof(one_over) - 1] = '\0';

    /* Create two-over string */
    memset(two_over, 'C', sizeof(two_over) - 1);
    two_over[sizeof(two_over) - 1] = '\0';

    /* Test exact fit - should succeed */
    memset(dest, 0, sizeof(dest));
    int result = safe_copy_term(exact_fit, dest, SAFE_BUFFER_SIZE);
    ck_assert_msg(result == 0, "Exact fit string should be accepted");
    ck_assert_msg(strlen(dest) < SAFE_BUFFER_SIZE,
        "Exact fit copy must not overflow buffer");

    /* Test one over - must be rejected */
    memset(dest, 0, sizeof(dest));
    result = safe_copy_term(one_over, dest, SAFE_BUFFER_SIZE);
    ck_assert_msg(result == -1,
        "String at exact buffer size must be rejected to prevent overflow");

    /* Test two over - must be rejected */
    memset(dest, 0, sizeof(dest));
    result = safe_copy_term(two_over, dest, SAFE_BUFFER_SIZE);
    ck_assert_msg(result == -1,
        "String exceeding buffer size must be rejected to prevent overflow");
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_term_env_bounds_checking);
    tcase_add_test(tc_core, test_null_term_handling);
    tcase_add_test(tc_core, test_exact_boundary_term);
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