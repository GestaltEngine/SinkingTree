set(HEADER_FILES
    commons.h
    hazard_ptr.h
    mutexed_std.h
    runner.h
    unordered_cc_map.h
    hashers.h
)

add_library(cc_map INTERFACE ${HEADER_FILES})
