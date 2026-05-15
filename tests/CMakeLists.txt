add_executable(fastcsa_hybrid_re_test "${CMAKE_CURRENT_SOURCE_DIR}/hybrid_re_test.cpp")
target_link_libraries(fastcsa_hybrid_re_test PRIVATE fastcsa::fastcsa Catch2::Catch2WithMain)
add_test(NAME fastcsa_hybrid_re_test COMMAND fastcsa_hybrid_re_test)

add_executable(fastcsa_small_test "${CMAKE_CURRENT_SOURCE_DIR}/csa_small_test.cpp")
target_link_libraries(fastcsa_small_test PRIVATE fastcsa::fastcsa Catch2::Catch2WithMain)
add_test(NAME fastcsa_small_test COMMAND fastcsa_small_test)

add_executable(fastcsa_dataset_validator "${CMAKE_CURRENT_SOURCE_DIR}/dataset_validator.cpp")
target_link_libraries(fastcsa_dataset_validator PRIVATE fastcsa::fastcsa)
