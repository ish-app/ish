#!/bin/bash
cd "$(dirname "$0")"
cd ../../
mkdir -p e2e_out

FS=./e2e_out/testfs
VERBOSE=false
YES_MODE=false
TEST_PATTERN=""
SUMMARY_LOG=./e2e_out/summary.txt
ALPINE_IMAGE="http://dl-cdn.alpinelinux.org/alpine/v3.11/releases/x86/alpine-minirootfs-3.11.2-x86.tar.gz"
rm -f "$SUMMARY_LOG"

while getopts "hvyf:e:" OPTION; do
    case $OPTION in
        v)
            VERBOSE=true
            ;;
        f)
            if [ -d "$OPTARG" ]; then
                FS="$OPTARG"
            else
                echo "No such directory $OPTARG"
            fi
            ;;
        e)
            TEST_PATTERN="$OPTARG"
            ;;
        y)
            YES_MODE=true
            ;;
        h)
            echo "iSH E2E Testing"
            echo "==============="
            echo "Automates e2e tests defined in tests/e2e."
            echo ""
            echo "  -h        Display help text."
            echo "  -v        Verbosely show test output as it is received."
            echo "  -f fs     Use \"fs\" as the -f option for iSH. Default \"alpine\"."
            echo "  -e pat    Use pattern \"pat\" to pattern match tests to run."
            echo "            Syntax is same as grep -E for local system."
            echo "  -y        Accept creation of test file system if it does not exist"
            echo ""
            echo "Example: Run the \"hello\" test in the alpine2 fake file system."
            echo "$ bash e2e.bash -f alpine2 -e ^hello$"
            exit 0
            ;;
    esac
done

ISH="./build/ish -f $FS"

if [ ! -d "$FS" ]; then
    if [ "$YES_MODE" = "true" ]; then
        INSTALL="Yes";
    else
        echo "File System \"$FS\" does not exist. Automatically set up now?"
        select yn in "Yes" "No"; do
            INSTALL="$yn";
            break;
        done
    fi
    echo install? $INSTALL
    case "$INSTALL" in
        Yes)
            echo
            echo "### Setting up test file system..."
            echo "###### Downloading Alpine"
            wget "$ALPINE_IMAGE" -O e2e_out/alpine.tar.gz
            echo "###### Unpacking Alpine"
            ./build/tools/fakefsify e2e_out/alpine.tar.gz "$FS"
            echo "###### Configuring iSH and installing base libraries"
            grep -E "^nameserver" /etc/resolv.conf | head -1 | $ISH /bin/sed -n "w /etc/resolv.conf"
            $ISH /bin/sh -c "apk update && apk add build-base python2 python3"
            ;;
        No) exit 1;;
    esac
fi

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
        $ISH /usr/bin/env sh -c "source /etc/profile && cd /tmp/e2e/$1 && sh test.sh" 2>&1 | tee -a "$SUMMARY_LOG" | tee "$ACTUAL_LOG"
    else
        $ISH /usr/bin/env sh -c "source /etc/profile && cd /tmp/e2e/$1 && sh test.sh" 2>&1 | tee -a "$SUMMARY_LOG" > "$ACTUAL_LOG"
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

for e2e_test in $(ls tests/e2e | grep -E "^[a-zA-Z0-9_]+$" | grep -E "$TEST_PATTERN"); do
    if [ -d "tests/e2e/$e2e_test" ]; then
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

if [ "$NUM_TOTAL" -eq 0 ]; then
    printf "### No tests were run" | tee -a "$SUMMARY_LOG" 
    exit 0
fi

NUM_PASSES=$((NUM_TOTAL-NUM_FAILS))
PERCENT_PASSES=$(echo "scale=2; 100 * $NUM_PASSES / $NUM_TOTAL" | bc)
PASSED_STR="$NUM_PASSES/$NUM_TOTAL ($PERCENT_PASSES%)"
printf "### Passed: %-22s ###\n" "$PASSED_STR" | tee -a "$SUMMARY_LOG"

if [ "$NUM_FAILS" -eq 0 ]; then
    exit 0
else
    exit 1
fi
