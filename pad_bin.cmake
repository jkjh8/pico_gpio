# Pad main.bin to 256-byte (FLASH_PAGE_SIZE) boundary for OTA compatibility
# BIN_DIR is passed from CMakeLists.txt via -DBIN_DIR=...
set(BIN_FILE "${BIN_DIR}/main.bin")

if(NOT EXISTS "${BIN_FILE}")
    message(STATUS "pad_bin: ${BIN_FILE} not found, skipping")
    return()
endif()

find_program(PYTHON python)
if(NOT PYTHON)
    find_program(PYTHON python3)
endif()

if(NOT PYTHON)
    message(WARNING "pad_bin: Python not found, skipping padding")
    return()
endif()

execute_process(
    COMMAND ${PYTHON} -c
        "import os; f='${BIN_FILE}'; s=os.path.getsize(f); r=s%256; pad=(256-r)%256;\
 open(f,'ab').write(b'\\x00'*pad) if pad else None;\
 print(f'pad_bin: {s} -> {s+pad} bytes (+{pad} padding)' if pad else f'pad_bin: already aligned ({s} bytes)')"
    RESULT_VARIABLE PAD_RESULT
    OUTPUT_VARIABLE PAD_OUTPUT
    ERROR_VARIABLE PAD_ERROR
)

if(NOT PAD_RESULT EQUAL 0)
    message(WARNING "pad_bin failed: ${PAD_ERROR}")
else()
    message(STATUS "${PAD_OUTPUT}")
endif()
