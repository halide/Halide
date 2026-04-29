tests(GROUPS failing_with_issue
      EXPECT_FAILURE
      SOURCES
      3292_async_specialize.cpp
      3293_storage_folding_async.cpp
      # 3357_vectorize_pred.cpp  # moved to test/correctness/vectorize_pred.cpp
      # 4283_store_at_gpu.cpp  # TODO: test file missing, tracked in issue #4283
      )
