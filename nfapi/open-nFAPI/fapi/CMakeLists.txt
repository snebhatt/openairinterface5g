add_library(nr_fapi_common
        src/nr_fapi.c
)
target_include_directories(nr_fapi_common PUBLIC inc)
target_link_libraries(nr_fapi_common PUBLIC nfapi_common)

add_library(nr_fapi_p5
        src/nr_fapi_p5.c
        src/nr_fapi_p5_utils.c
)
target_include_directories(nr_fapi_p5 PUBLIC inc)
target_link_libraries(nr_fapi_p5 PUBLIC nr_fapi_common)

add_library(nr_fapi_p7
        src/nr_fapi_p7.c
        src/nr_fapi_p7_utils.c
)
target_include_directories(nr_fapi_p7 PUBLIC inc)
target_link_libraries(nr_fapi_p7 PUBLIC nr_fapi_common)

if (OAI_AERIAL)
    target_compile_definitions(nr_fapi_p5 PRIVATE ENABLE_AERIAL)
    target_compile_definitions(nr_fapi_p7 PRIVATE ENABLE_AERIAL)
endif ()
