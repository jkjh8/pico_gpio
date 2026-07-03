# 빌드할 때마다 실행되어 현재 시각으로 build_time.h를 생성한다.
# __DATE__/__TIME__은 해당 .c 파일이 재컴파일될 때만 갱신되어 부정확하므로
# (파일이 안 바뀐 빌드에서는 옛 시각이 남음) 링크 직전에 항상 새로 생성한다.
string(TIMESTAMP _now "%Y-%m-%d %H:%M:%S")
file(WRITE "${OUT}" "#ifndef BUILD_TIME_H\n#define BUILD_TIME_H\n#define BUILD_TIMESTAMP \"${_now}\"\n#endif\n")

# 파일명용 시각(콜론/공백 없음)도 같은 시점에 기록 — 산출물 파일명이
# 펌웨어에 박힌 BUILD_TIMESTAMP와 정확히 일치하도록 보장 (copy_timestamped.cmake에서 사용)
string(TIMESTAMP _now_fs "%Y%m%d_%H%M%S")
get_filename_component(_dir "${OUT}" DIRECTORY)
file(WRITE "${_dir}/build_time_fs.txt" "${_now_fs}")
