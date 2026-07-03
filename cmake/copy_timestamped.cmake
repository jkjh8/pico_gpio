# main.bin / main.uf2를 dist/pico_gpio_v{버전}_{빌드시각}.bin/.uf2로 복사한다.
# 시각은 gen_build_time.cmake가 기록한 값을 읽어 펌웨어 내부 BUILD_TIMESTAMP와 일치.
# 이전 산출물은 지우고 최신 것만 유지 (필요하면 아래 REMOVE 블록을 삭제해 누적 보관 가능).
file(READ "${BIN_DIR}/build_info/build_time_fs.txt" _ts)
set(_dist "${BIN_DIR}/dist")
file(MAKE_DIRECTORY "${_dist}")

file(GLOB _old "${_dist}/pico_gpio_*")
if(_old)
    file(REMOVE ${_old})
endif()

set(_base "pico_gpio_v${FW_VERSION}_${_ts}")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy "${BIN_DIR}/main.bin" "${_dist}/${_base}.bin")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy "${BIN_DIR}/main.uf2" "${_dist}/${_base}.uf2")
message(STATUS "dist: ${_base}.bin / ${_base}.uf2")
