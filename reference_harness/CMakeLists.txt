add_executable(reference_harness
    Source/main.cpp
)

target_link_libraries(reference_harness PRIVATE
    reference_core
)
