# Convenience wrapper around CMake.
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
  CMAKE_FLAGS += -DUSE_SYSTEMD=ON
endif

all:
	cmake -B build $(CMAKE_FLAGS) && cmake --build build
