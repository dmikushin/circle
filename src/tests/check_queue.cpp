#include <check.h>
#include <stdlib.h>
#include <string>

#include "libcircle.hpp"
#include "queue.hpp"

START_TEST
(test_queue_init_free)
{
    int free_result = -1;

    circle::internal_queue_t* q;
    circle::init(0, NULL, circle::RuntimeDefaultFlags);

    q = circle::internal_queue_init();
    fail_if(q == NULL, "Initializing a queue failed.");

    free_result = circle::internal_queue_free(q);
    fail_unless(free_result >= 0, "Queue was not null after free.");

    circle::finalize();
}
END_TEST

START_TEST
(test_queue_pop_empty)
{
    int free_result = -1;
    std::vector<uint8_t> result;

    circle::internal_queue_t* q;
    circle::init(0, NULL, circle::RuntimeDefaultFlags);

    q = circle::internal_queue_init();
    fail_if(q == NULL, "Initializing a queue failed.");

    circle::internal_queue_pop(q, result);
    fail_if(result.size() > 0, \
            "Something was poped from an empty queue.");

    free_result = circle::internal_queue_free(q);
    fail_unless(free_result, "Circle context was not null after free.");

    circle::finalize();
}
END_TEST

START_TEST
(test_queue_single_push_pop)
{
    int free_result = -1;
    const std::string test_string = "Here's a test string!";
    std::vector<uint8_t> result;

    circle::internal_queue_t* q;
    circle::init(0, NULL, circle::RuntimeDefaultFlags);

    q = circle::internal_queue_init();
    fail_if(q == NULL, "Initializing a queue failed.");

    circle::internal_queue_push(q, std::vector<uint8_t>(test_string.begin(), test_string.end()));
    fail_unless(q->count == 1, \
                "Queue count was not correct after a single push.");

    circle::internal_queue_pop(q, result);
    fail_unless(q->count == 0, \
                "Queue count was not correct after poping the last element.");

    fail_unless(strcmp(test_string, result) == 0, \
                "Result poped from the queue does not match original.");

    free_result = circle::internal_queue_free(q);
    fail_unless(free_result, "Circle context was not null after free.");

    circle::finalize();
}
END_TEST

START_TEST
(test_queue_multiple_push_pop)
{
    int free_result = -1;
    std::vector<uint8_t> result;

    const std::string test_strings[] = {
        "first test string",
        "second test string",
        "third test string",
        "fourth test string",
        "fifth test string",
        "sixth test string",
        "seventh test string",
        "eighth test string",
        "nineth test string",
        "tenth test string"
    };

    circle::internal_queue_t* q;
    circle::init(0, NULL, circle::RuntimeDefaultFlags);

    q = circle::internal_queue_init();
    fail_unless(q != NULL, "Initializing a queue failed.");

    /* Warm it up a bit */
    circle::internal_queue_push(q, test_strings[0]);
    circle::internal_queue_pop(q, result);
    circle::internal_queue_push(q, test_strings[1]);
    circle::internal_queue_pop(q, result);

    fail_unless(strcmp(test_strings[1], result) == 0, \
                "The queue pop was not the expected result.");

    fail_unless(q->count == 0, \
                "Queue count was not correct after two pushes and two pops.");

    /* Now lets try multiple ones */
    circle::internal_queue_push(q, test_strings[2]);
    circle::internal_queue_push(q, test_strings[3]);
    circle::internal_queue_push(q, test_strings[4]);
    circle::internal_queue_push(q, test_strings[5]);
    circle::internal_queue_push(q, test_strings[6]);
    circle::internal_queue_push(q, test_strings[7]); // count = 6
    circle::internal_queue_pop(q, result);
    circle::internal_queue_pop(q, result);
    circle::internal_queue_pop(q, result);
    circle::internal_queue_pop(q, result); // count = 2
    circle::internal_queue_push(q, test_strings[8]);
    circle::internal_queue_push(q, test_strings[9]);
    circle::internal_queue_push(q, test_strings[0]); // count = 5
    circle::internal_queue_pop(q, result);
    circle::internal_queue_pop(q, result);
    circle::internal_queue_pop(q, result);
    circle::internal_queue_pop(q, result);
    circle::internal_queue_pop(q, result); // count = 0

    fail_unless(strcmp(test_strings[2], result) == 0, \
                "The queue pop was not the expected result.");

    fail_unless(q->count == 0, \
                "Queue count was not correct after several operations.");

    /* Lets just try a few randomly */
    circle::internal_queue_push(q, test_strings[1]);
    circle::internal_queue_pop(q, result);
    circle::internal_queue_pop(q, result); // count = 0
    circle::internal_queue_push(q, test_strings[2]);
    circle::internal_queue_pop(q, result);
    circle::internal_queue_push(q, test_strings[3]);
    circle::internal_queue_push(q, test_strings[4]);
    circle::internal_queue_push(q, test_strings[5]); // count = 3
    circle::internal_queue_pop(q, result); // count = 2

    fail_unless(strcmp(test_strings[5], result) == 0, \
                "The queue pop was not the expected result.");

    fail_unless(q->count == 2, \
                "Queue count was not correct after several operations.");

    free_result = circle::internal_queue_free(q);
    fail_unless(free_result, "Circle context was not null after free.");

    circle::finalize();
}
END_TEST

Suite*
check_queue_suite(void)
{
    Suite* s = suite_create("check_queue");
    TCase* tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_queue_init_free);
    tcase_add_test(tc_core, test_queue_pop_empty);
    tcase_add_test(tc_core, test_queue_single_push_pop);
    tcase_add_test(tc_core, test_queue_multiple_push_pop);

    suite_add_tcase(s, tc_core);

    return s;
}

int
main(void)
{
    int number_failed;

    Suite* s = check_queue_suite();
    SRunner* sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* EOF */
