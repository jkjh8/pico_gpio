# 빌드할 때마다 실행되어 현재 시각으로 build_time.h를 생성한다.
# __DATE__/__TIME__은 해당 .c 파일이 재컴파일될 때만 갱신되어 부정확하므로
# (파일이 안 바뀐 빌드에서는 옛 시각이 남음) 링크 직전에 항상 새로 생성한다.
string(TIMESTAMP _now "%Y-%m-%d %H:%M:%S")
file(WRITE "${OUT}" "#ifndef BUILD_TIME_H\n#define BUILD_TIME_H\n#define BUILD_TIMESTAMP \"${_now}\"\n#endif\n")
