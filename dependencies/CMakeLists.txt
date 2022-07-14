# Enable fetching content
include(FetchContent)

# Fetch the picoquic library, which brings in picotls with it
FetchContent_Declare(
    picoquic
    GIT_REPOSITORY  https://github.com/private-octopus/picoquic.git
    GIT_TAG         master
)

# Set the option to force picoquic to fetch the picotls
set(PICOQUIC_FETCH_PTLS ON)

# Make dependencies available
FetchContent_MakeAvailable(picoquic)
