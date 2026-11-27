#!/bin/bash
set -e
cd ~/transport_verify

echo "=========================================================="
echo "           PART 3A VERIFICATION SUITE                     "
echo "=========================================================="

echo "--- 1. Data Analysis and Forest MAE Consistency ---"
bash scripts/analyze_data.sh

echo ""
echo "--- 2. Unit Tests (make clean && make test) ---"
cd backend
make clean >/dev/null 2>&1
make test 2>&1 | grep -E "Tests run|Assertions run"
echo ""

echo "--- 3. Valgrind Memory Check ---"
make clean >/dev/null 2>&1
make valgrind 2>&1 | grep -E "ERROR SUMMARY|All heap blocks" | sed 's/==[0-9]*== //' | sort | uniq -c
echo ""

echo "--- 4. AddressSanitizer Check ---"
make clean >/dev/null 2>&1
make asan > /tmp/asan.log 2>&1
echo "asan exit=$?"
grep -E "fsanitize" /tmp/asan.log | head -1
echo "AddressSanitizer/runtime error count:" $(grep -c -E "AddressSanitizer|runtime error" /tmp/asan.log)
tail -3 /tmp/asan.log
echo ""

echo "--- 5. Frozen Files Check (sha256sum -c ~/part2_frozen.sha) ---"
sha256sum -c ~/part2_frozen.sha
echo ""

echo "--- 6. Test Functions in tests/test_demand_model.c ---"
grep -n "^static void test_" tests/test_demand_model.c
echo ""

echo "--- 7. Reproducibility Check (train_model.py into /tmp) ---"
cd ~/transport_verify
bash scripts/check_reproducibility.sh
echo ""
