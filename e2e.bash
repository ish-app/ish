#!/bin/bash
cd "$(dirname "$0")"
mkdir -p e2e_out

FS=./alpine
VERBOSE=false
SUMMARY_LOG=./e2e_out/summary.txt
rm -f "$SUMMARY_LOG"

while getopts "vf:" OPTION; do
    case $OPTION in
        v)
            VERBOSE=true
            ;;
        f)
            if [ -d "$OPTARG" ]; then
                FS=$OPTARG
            else
                echo "No such directory $OPTARG"
            fi
            ;;
    esac
done

ISH="./build/ish -f $FS"

NUM_TOTAL=0
NUM_FAILS=0

test_setup() {
    $ISH /bin/rm -rf /tmp/e2e/$1
    $ISH /bin/mkdir -p /tmp/e2e/$1
    rm -rf ./e2e_out/$1
    mkdir -p ./e2e_out/$1
    tar -cf - -C tests/e2e $1 | $ISH /bin/tar xf - -C /tmp/e2e
}

test_teardown() {
    # Yes, this can overwrite actual.txt. This is a feature I guess. /shrug
    $ISH /bin/tar -cf - -C /tmp/e2e $1 | tar -xf - -C ./e2e_out
}

run_test() {
    ACTUAL_LOG="e2e_out/$1/actual.txt"
    if [ "$VERBOSE" = "true" ]; then
        $ISH /usr/bin/env sh -c "source /etc/profile && cd /tmp/e2e/$1 && sh test.sh" | tee -a "$SUMMARY_LOG" | tee "$ACTUAL_LOG"
    else
        $ISH /usr/bin/env sh -c "source /etc/profile && cd /tmp/e2e/$1 && sh test.sh" | tee -a "$SUMMARY_LOG" > "$ACTUAL_LOG"
    fi
}

validate_test() {
    EXPECTED_LOG="tests/e2e/$1/expected.txt"
    ACTUAL_LOG="e2e_out/$1/actual.txt"
    DIFF_LOG="e2e_out/$1/diff.txt"
    diff -uw "$EXPECTED_LOG" "$ACTUAL_LOG" > $DIFF_LOG
    DIFF_EXIT_CODE=$?
    case "$DIFF_EXIT_CODE" in
        0)
            # :)
            ;;
        1)
            NUM_FAILS=$((NUM_FAILS+1))
            echo "!!! Failed: $1" | tee -a "$SUMMARY_LOG"
            cat "$DIFF_LOG" | tee -a "$SUMMARY_LOG"
            ;;
        2)
            NUM_FAILS=$((NUM_FAILS+1))
            echo "Something went wrong when trying to validate $1." | tee -a "$SUMMARY_LOG"
            ;;
    esac
}

for e2e_test in $(ls tests/e2e | grep -E "^[a-zA-Z0-9_]+$"); do
    if [ -d tests/e2e/$e2e_test ]; then
        let "NUM_TOTAL+=1"
        printf "### Running Test: "%-16s" ###\n" "$e2e_test" | tee -a "$SUMMARY_LOG"
        test_setup $e2e_test
        run_test $e2e_test
        test_teardown $e2e_test
        validate_test $e2e_test
    else
        echo $e2e_test is not a directory! Skipping... | tee -a "$SUMMARY_LOG"
    fi
done

NUM_PASSES=$((NUM_TOTAL-NUM_FAILS))
PERCENT_PASSES=$(echo "scale=2; 100 * $NUM_PASSES / $NUM_TOTAL" | bc)
PASSED_STR="$NUM_PASSES/$NUM_TOTAL ($PERCENT_PASSES%)"
printf "### Passed: %-22s ###\n" "$PASSED_STR" | tee -a "$SUMMARY_LOG"

if [ "$NUM_FAILS" -eq 0 ]; then
    exit 0
else
    exit 1
fi
