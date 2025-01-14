cmake_minimum_required(VERSION 3.13)

add_library(levk-test INTERFACE)
target_link_libraries(levk-test INTERFACE levk::levk-compile-options levk::levk-link-options)

# Hash
add_executable(test-hash hash_test.cpp)
target_link_libraries(test-hash PRIVATE dtest::main levk::levk-core levk-test)
add_test(Hash test-hash)

# StaticAny
add_executable(test-any any_test.cpp)
target_link_libraries(test-any PRIVATE dtest::main levk::levk-core levk-test)
add_test(ktl::fixed_any test-any)

# fixed_vector
add_executable(test-fv fixed_vector_test.cpp)
target_link_libraries(test-fv PRIVATE dtest::main levk::levk-core levk-test)
add_test(ktl::fixed_vector test-fv)

# delegate
add_executable(test-delegate delegate_test.cpp)
target_link_libraries(test-delegate PRIVATE dtest::main levk::levk-core levk-test)
add_test(ktl::delegate test-delegate)

# pipe-hash
add_executable(test-pipe-hash pipe_hash_test.cpp)
target_link_libraries(test-pipe-hash PRIVATE dtest::main levk::levk-graphics levk-test)
add_test(pipe-hash test-pipe-hash)
