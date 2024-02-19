add_subdirectory(file-permissions)

set(ALL_EXERCISES
    file-permissions
)

# Add the overall target that builds all the programs.
add_custom_target(exercises ALL DEPENDS ${ALL_EXERCISES})
