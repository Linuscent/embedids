# Create the EmbedIDS library
add_library(embedids
    embedids.c
)

# Include directories for the library
target_include_directories(embedids 
    PUBLIC 
        ${CMAKE_CURRENT_BINARY_DIR}/../include
)

# Set library properties
set_target_properties(embedids PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    PUBLIC_HEADER "${CMAKE_CURRENT_BINARY_DIR}/../include/embedids.h"
)

# Link any required libraries (none for this simple implementation)
# target_link_libraries(embedids PRIVATE pthread)
