# Vendored dependency targets (header-only interface libraries). Consumed via wrappers in
# libs/ — engine code should not include these directly outside its wrapping module.
add_library(third_party_cdt INTERFACE)
target_include_directories(third_party_cdt INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/cdt/include)

add_library(third_party_nanoflann INTERFACE)
target_include_directories(third_party_nanoflann INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/nanoflann/include)
