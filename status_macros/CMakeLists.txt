add_library(status_macros STATIC status_macros.h status_builder.h
                                 status_builder.cc source_location.h)
target_link_libraries(status_macros absl::status)
