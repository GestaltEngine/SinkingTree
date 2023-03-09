set(ASAN_COMPILE_FLAGS -g -fsanitize=address,undefined -fno-sanitize-recover=all)
set(ASAN_LINK_FLAGS -g -fsanitize=address,undefined)

add_executable(test test.cpp)
target_link_libraries(test cc_map Catch2::Catch2WithMain)
target_compile_options(test PUBLIC ${ASAN_COMPILE_FLAGS})
target_link_options(test PUBLIC ${ASAN_LINK_FLAGS})

set(FAST_COMPILE_FLAGS -O2 -g)

add_executable(bench bench.cpp)
target_link_libraries(bench cc_map Catch2::Catch2WithMain)
target_compile_options(bench PUBLIC ${FAST_COMPILE_FLAGS})

set(TSAN_COMPILE_FLAGS -g -fsanitize=thread -fno-sanitize-recover=all)
set(TSAN_LINK_FLAGS -g -fsanitize=thread)

add_executable(cc_test_tsan cc_test.cpp)
target_link_libraries(cc_test_tsan cc_map Catch2::Catch2WithMain)
target_compile_options(cc_test_tsan PUBLIC ${TSAN_COMPILE_FLAGS})
target_link_options(cc_test_tsan PUBLIC ${TSAN_LINK_FLAGS})


add_executable(cc_test_asan cc_test.cpp)
target_link_libraries(cc_test_asan cc_map Catch2::Catch2WithMain)
target_compile_options(cc_test_asan PUBLIC ${ASAN_COMPILE_FLAGS})
target_link_options(cc_test_asan PUBLIC ${ASAN_LINK_FLAGS})
