# 每次 DLL 更新时重写 RC，确保 EXE 不会继续内嵌旧版本的 DLL。
file(TO_CMAKE_PATH "${DLL}" dll_path)
file(TO_CMAKE_PATH "${MANIFEST}" manifest_path)
file(WRITE "${OUT}" "#include <windows.h>\n101 RCDATA \"${dll_path}\"\n1 RT_MANIFEST \"${manifest_path}\"\n")
